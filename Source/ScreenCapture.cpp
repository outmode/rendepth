// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include "ScreenCapture.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace {
bool setCaptureError(std::string& destination, const char* message) {
	destination = message;
	return false;
}
}

#if defined(__linux__)
#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <unistd.h>
#endif

struct ScreenCapture::Impl {
	std::mutex mutex;
	std::shared_ptr<VideoFrame> newestFrame;
	std::thread thread;
	bool active = false;
	bool stopRequested = false;
	std::string error;

	#if defined(__linux__)
	GstElement* pipeline = nullptr;
	GstElement* sink = nullptr;
	int pipewireFd = -1;
	std::string portalSession;
	#endif
};

#if defined(__linux__)
namespace {
constexpr const char* portalName = "org.freedesktop.portal.Desktop";
constexpr const char* portalPath = "/org/freedesktop/portal/desktop";
constexpr const char* screenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr const char* requestInterface = "org.freedesktop.portal.Request";

struct RequestWaiter {
	GMainLoop* loop = nullptr;
	GVariant* results = nullptr;
};

void portalResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar*, GVariant* parameters, gpointer data) {
	auto* waiter = static_cast<RequestWaiter*>(data);
	guint response = 1;
	GVariant* results = nullptr;
	g_variant_get(parameters, "(u@a{sv})", &response, &results);
	if (response == 0) waiter->results = results;
	else g_variant_unref(results);
	g_main_loop_quit(waiter->loop);
}

GVariant* waitForPortalRequest(GDBusConnection* connection, const char* path) {
	RequestWaiter waiter;
	waiter.loop = g_main_loop_new(nullptr, FALSE);
	const guint subscription = g_dbus_connection_signal_subscribe(connection,
		portalName, requestInterface, "Response", path, nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE, portalResponse, &waiter, nullptr);
	g_main_loop_run(waiter.loop);
	g_dbus_connection_signal_unsubscribe(connection, subscription);
	g_main_loop_unref(waiter.loop);
	return waiter.results;
}

GVariant* callPortalRequest(GDBusConnection* connection, GDBusProxy* proxy,
	const char* method, GVariant* parameters) {
	GError* error = nullptr;
	GVariant* reply = g_dbus_proxy_call_sync(proxy, method, parameters,
		G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
	if (reply == nullptr) {
		SDL_Log("Screen capture %s failed: %s", method,
			error != nullptr ? error->message : "unknown error");
		if (error != nullptr) g_error_free(error);
		return nullptr;
	}
	const char* path = nullptr;
	g_variant_get(reply, "(&o)", &path);
	const std::string requestPath(path);
	g_variant_unref(reply);
	return waitForPortalRequest(connection, requestPath.c_str());
}

void closePortalSession(const std::string& session) {
	if (session.empty()) return;
	GError* error = nullptr;
	auto* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
	if (connection == nullptr) {
		if (error != nullptr) g_error_free(error);
		return;
	}
	auto* reply = g_dbus_connection_call_sync(connection, portalName, session.c_str(),
		"org.freedesktop.portal.Session", "Close", nullptr, nullptr,
		G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
	if (reply != nullptr) {
		g_variant_unref(reply);
	} else if (error != nullptr) {
		SDL_Log("Could not close ScreenCast portal session: %s", error->message);
	}
	if (error != nullptr) g_error_free(error);
	g_object_unref(connection);
}

GVariantBuilder* portalOptions(const char* token) {
	auto* options = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(options, "{sv}", "handle_token", g_variant_new_string(token));
	return options;
}

} // namespace
#endif

ScreenCapture::ScreenCapture() : impl(std::make_unique<Impl>()) {}

ScreenCapture::~ScreenCapture() {
	stop();
}

bool ScreenCapture::start(SDL_Window* parentWindow, std::string& error) {
	stop();
	if (parentWindow == nullptr) return setCaptureError(error, "Rendepth window is unavailable.");

#if defined(__linux__)
	static std::once_flag gstInitialized;
	std::call_once(gstInitialized, [] { gst_init(nullptr, nullptr); });

	GError* dbusError = nullptr;
	auto* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &dbusError);
	if (connection == nullptr) {
		error = dbusError != nullptr ? dbusError->message : "Could not connect to session bus.";
		if (dbusError != nullptr) g_error_free(dbusError);
		return false;
	}
	auto* proxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr,
		portalName, portalPath, screenCastInterface, nullptr, &dbusError);
	if (proxy == nullptr) {
		error = dbusError != nullptr ? dbusError->message : "Could not connect to ScreenCast portal.";
		if (dbusError != nullptr) g_error_free(dbusError);
		g_object_unref(connection);
		return false;
	}

	const std::string token = "rendepth_" + std::to_string(getpid());
	auto* options = portalOptions(token.c_str());
	g_variant_builder_add(options, "{sv}", "session_handle_token",
		g_variant_new_string((token + "_session").c_str()));
	auto* createResults = callPortalRequest(connection, proxy, "CreateSession",
		g_variant_new("(@a{sv})", g_variant_builder_end(options)));
	g_variant_builder_unref(options);
	if (createResults == nullptr) {
		g_object_unref(proxy); g_object_unref(connection);
		return setCaptureError(error, "ScreenCast session creation was cancelled.");
	}
	GVariant* sessionValue = g_variant_lookup_value(createResults, "session_handle",
		G_VARIANT_TYPE_STRING);
	if (sessionValue == nullptr)
		sessionValue = g_variant_lookup_value(createResults, "session_handle",
			G_VARIANT_TYPE_OBJECT_PATH);
	g_variant_unref(createResults);
	if (sessionValue == nullptr) {
		g_object_unref(proxy); g_object_unref(connection);
		return setCaptureError(error, "ScreenCast portal returned no session handle.");
	}
	const std::string session(g_variant_get_string(sessionValue, nullptr));
	g_variant_unref(sessionValue);
	impl->portalSession = session;

	options = portalOptions((token + "_select").c_str());
	// Offer both monitor and window sources. Monitor capture is convenient for
	// ordinary desktop content, while window capture avoids losing updates from
	// hardware video overlays used by fullscreen/maximized browsers.
	g_variant_builder_add(options, "{sv}", "types", g_variant_new_uint32(3));
	g_variant_builder_add(options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
	auto* selectResults = callPortalRequest(connection, proxy, "SelectSources",
		g_variant_new("(o@a{sv})", session.c_str(), g_variant_builder_end(options)));
	g_variant_builder_unref(options);
	if (selectResults == nullptr) {
		closePortalSession(session);
		g_object_unref(proxy); g_object_unref(connection);
		return setCaptureError(error, "ScreenCast source selection was cancelled.");
	}
	g_variant_unref(selectResults);

	options = portalOptions((token + "_start").c_str());
	const auto properties = SDL_GetWindowProperties(parentWindow);
	const char* handle = SDL_GetStringProperty(properties,
		SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING, nullptr);
	const std::string parent = handle != nullptr ? "wayland:" + std::string(handle) : "";
	auto* startResults = callPortalRequest(connection, proxy, "Start",
		g_variant_new("(os@a{sv})", session.c_str(), parent.c_str(),
			g_variant_builder_end(options)));
	g_variant_builder_unref(options);
	if (startResults == nullptr) {
		closePortalSession(session);
		g_object_unref(proxy); g_object_unref(connection);
		return setCaptureError(error, "ScreenCast start was cancelled.");
	}
	GVariant* streams = g_variant_lookup_value(startResults, "streams", G_VARIANT_TYPE_ARRAY);
	g_variant_unref(startResults);
	if (streams == nullptr || g_variant_n_children(streams) == 0) {
		if (streams != nullptr) g_variant_unref(streams);
		closePortalSession(session);
		g_object_unref(proxy); g_object_unref(connection);
		return setCaptureError(error, "ScreenCast portal returned no streams.");
	}
	GVariant* stream = g_variant_get_child_value(streams, 0);
	guint32 nodeId = 0;
	g_variant_get(stream, "(u@a{sv})", &nodeId, nullptr);
	g_variant_unref(stream);
	g_variant_unref(streams);

	GUnixFDList* fdList = nullptr;
	auto* remoteOptions = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
	GVariant* fdReply = g_dbus_proxy_call_with_unix_fd_list_sync(proxy,
		"OpenPipeWireRemote", g_variant_new("(o@a{sv})", session.c_str(),
			g_variant_builder_end(remoteOptions)), G_DBUS_CALL_FLAGS_NONE, -1,
		nullptr, &fdList, nullptr, &dbusError);
	g_variant_builder_unref(remoteOptions);
	g_object_unref(proxy);
	g_object_unref(connection);
	if (fdReply == nullptr || fdList == nullptr) {
		error = dbusError != nullptr ? dbusError->message : "Could not open PipeWire remote.";
		if (dbusError != nullptr) g_error_free(dbusError);
		if (fdReply != nullptr) g_variant_unref(fdReply);
		if (fdList != nullptr) g_object_unref(fdList);
		closePortalSession(session);
		return false;
	}
	int fdIndex = -1;
	g_variant_get(fdReply, "(h)", &fdIndex);
	impl->pipewireFd = g_unix_fd_list_get(fdList, fdIndex, &dbusError);
	g_variant_unref(fdReply);
	g_object_unref(fdList);
	if (impl->pipewireFd < 0) {
		closePortalSession(session);
		return setCaptureError(error, "ScreenCast returned an invalid PipeWire descriptor.");
	}

	const std::string description = "pipewiresrc fd=" + std::to_string(impl->pipewireFd) +
		" always-copy=true do-timestamp=true keepalive-time=33" +
		" path=" + std::to_string(nodeId) +
		" ! videoconvert ! video/x-raw,format=RGBA ! appsink name=sink sync=false max-buffers=1 drop=true";
	GError* pipelineError = nullptr;
	impl->pipeline = gst_parse_launch(description.c_str(), &pipelineError);
	if (impl->pipeline == nullptr) {
		error = pipelineError != nullptr ? pipelineError->message : "Could not create PipeWire pipeline.";
		if (pipelineError != nullptr) g_error_free(pipelineError);
		close(impl->pipewireFd); impl->pipewireFd = -1;
		closePortalSession(session);
		return false;
	}
	impl->sink = gst_bin_get_by_name(GST_BIN(impl->pipeline), "sink");
	if (impl->sink == nullptr) {
		gst_object_unref(impl->pipeline); impl->pipeline = nullptr;
		close(impl->pipewireFd); impl->pipewireFd = -1;
		closePortalSession(session);
		return setCaptureError(error, "Could not create PipeWire frame sink.");
	}
	{
		std::lock_guard lock(impl->mutex);
		impl->stopRequested = false;
		impl->active = true;
	}
	gst_element_set_state(impl->pipeline, GST_STATE_PLAYING);
	impl->thread = std::thread([this] {
		while (true) {
			{
				std::lock_guard lock(impl->mutex);
				if (impl->stopRequested) break;
			}
			auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(impl->sink),
				100 * GST_MSECOND);
			if (sample == nullptr) continue;
			auto* caps = gst_sample_get_caps(sample);
			auto* structure = caps != nullptr ? gst_caps_get_structure(caps, 0) : nullptr;
			int width = 0, height = 0;
			if (structure == nullptr || !gst_structure_get_int(structure, "width", &width) ||
				!gst_structure_get_int(structure, "height", &height)) {
				gst_sample_unref(sample); continue;
			}
			auto* buffer = gst_sample_get_buffer(sample);
			GstMapInfo map{};
			if (buffer == nullptr || !gst_buffer_map(buffer, &map, GST_MAP_READ)) {
				gst_sample_unref(sample); continue;
			}
			auto frame = std::make_shared<VideoFrame>();
			frame->width = frame->outputWidth = width;
			frame->height = frame->outputHeight = height;
			frame->inferenceWidth = width;
			frame->inferenceHeight = height;
			frame->presentationTime = std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			frame->generation = 1;
			frame->inferenceRGBA.assign(map.data, map.data + std::min<size_t>(map.size,
				static_cast<size_t>(width) * height * 4));
			frame->planes[0] = frame->inferenceRGBA;
			gst_buffer_unmap(buffer, &map);
			gst_sample_unref(sample);
			if (frame->inferenceRGBA.size() < static_cast<size_t>(width) * height * 4) continue;
			std::lock_guard lock(impl->mutex);
			impl->newestFrame = std::move(frame);
		}
	});
	return true;
#else
	return setCaptureError(error, "Screen capture is currently implemented for Linux only.");
#endif
}

void ScreenCapture::stop() {
	if (impl == nullptr) return;
#if defined(__linux__)
	{
		std::lock_guard lock(impl->mutex);
		impl->stopRequested = true;
		impl->active = false;
	}
	if (impl->pipeline != nullptr)
		gst_element_set_state(impl->pipeline, GST_STATE_NULL);
	if (impl->thread.joinable()) impl->thread.join();
	if (impl->sink != nullptr) { gst_object_unref(impl->sink); impl->sink = nullptr; }
	if (impl->pipeline != nullptr) { gst_object_unref(impl->pipeline); impl->pipeline = nullptr; }
	if (impl->pipewireFd >= 0) { close(impl->pipewireFd); impl->pipewireFd = -1; }
	if (!impl->portalSession.empty()) {
		closePortalSession(impl->portalSession);
		impl->portalSession.clear();
	}
#endif
	std::lock_guard lock(impl->mutex);
	impl->newestFrame.reset();
}

bool ScreenCapture::running() const {
	std::lock_guard lock(impl->mutex);
	return impl->active;
}

std::shared_ptr<VideoFrame> ScreenCapture::takeFrame() {
	std::lock_guard lock(impl->mutex);
	return std::exchange(impl->newestFrame, nullptr);
}
