// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT

#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {

constexpr const char* portalName = "org.freedesktop.portal.Desktop";
constexpr const char* portalPath = "/org/freedesktop/portal/desktop";
constexpr const char* screenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr const char* requestInterface = "org.freedesktop.portal.Request";

struct RequestResult {
	GMainLoop* loop = nullptr;
	GVariant* results = nullptr;
	GDBusConnection* connection = nullptr;
	GDBusProxy* proxy = nullptr;
	guint subscription = 0;
	GError* error = nullptr;
};

void requestResponse(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar*, GVariant* parameters, gpointer userData) {
	auto* result = static_cast<RequestResult*>(userData);
	guint response = 1;
	GVariant* values = nullptr;
	g_variant_get(parameters, "(u@a{sv})", &response, &values);
	if (response == 0) result->results = values;
	else g_variant_unref(values);
	g_main_loop_quit(result->loop);
}

GVariant* waitForRequest(GDBusConnection* connection, const char* requestPath) {
	RequestResult result;
	result.loop = g_main_loop_new(nullptr, FALSE);
	result.connection = connection;
	result.subscription = g_dbus_connection_signal_subscribe(connection, portalName,
		requestInterface, "Response", requestPath, nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE, requestResponse, &result, nullptr);
	g_main_loop_run(result.loop);
	g_dbus_connection_signal_unsubscribe(connection, result.subscription);
	g_main_loop_unref(result.loop);
	if (result.results == nullptr) {
		std::fprintf(stderr, "Portal request was cancelled or failed.\n");
	}
	return result.results;
}

GVariant* callRequest(GDBusConnection* connection, GDBusProxy* proxy,
	const char* method, GVariant* parameters) {
	GError* error = nullptr;
	GVariant* reply = g_dbus_proxy_call_sync(proxy, method, parameters,
		G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
	if (reply == nullptr) {
		std::fprintf(stderr, "%s failed: %s\n", method, error->message);
		g_error_free(error);
		return nullptr;
	}
	const char* requestPath = nullptr;
	g_variant_get(reply, "(&o)", &requestPath);
	std::string path(requestPath);
	g_variant_unref(reply);
	return waitForRequest(connection, path.c_str());
}

GVariantBuilder* optionsBuilder(const char* token) {
	auto* options = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(options, "{sv}", "handle_token", g_variant_new_string(token));
	return options;
}

} // namespace

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "Could not initialize SDL video: %s\n", SDL_GetError());
		return EXIT_FAILURE;
	}
	SDL_Window* parentWindow = SDL_CreateWindow("Rendepth Screen Capture Test",
		640, 480, SDL_WINDOW_HIDDEN);
	if (parentWindow == nullptr) {
		std::fprintf(stderr, "Could not create SDL parent window: %s\n", SDL_GetError());
		SDL_Quit();
		return EXIT_FAILURE;
	}
	gst_init(&argc, &argv);

	GError* error = nullptr;
	auto* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
	if (connection == nullptr) {
		std::fprintf(stderr, "Could not connect to the session bus: %s\n", error->message);
		g_error_free(error);
		SDL_DestroyWindow(parentWindow);
		SDL_Quit();
		return EXIT_FAILURE;
	}
	auto* proxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, nullptr,
		portalName, portalPath, screenCastInterface, nullptr, &error);
	if (proxy == nullptr) {
		std::fprintf(stderr, "Could not connect to the ScreenCast portal: %s\n", error->message);
		g_error_free(error);
		g_object_unref(connection);
		SDL_DestroyWindow(parentWindow);
		SDL_Quit();
		return EXIT_FAILURE;
	}

	const auto token = std::string("rendepth_") + std::to_string(getpid());
	auto* options = optionsBuilder(token.c_str());
	g_variant_builder_add(options, "{sv}", "session_handle_token",
		g_variant_new_string((token + "_session").c_str()));
	GVariant* reply = g_dbus_proxy_call_sync(proxy, "CreateSession",
		g_variant_new("(@a{sv})", g_variant_builder_end(options)),
		G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
	g_variant_builder_unref(options);
	if (reply == nullptr) {
		std::fprintf(stderr, "CreateSession failed: %s\n", error->message);
		g_error_free(error);
		g_object_unref(proxy);
		g_object_unref(connection);
		return EXIT_FAILURE;
	}
	const char* createRequest = nullptr;
	g_variant_get(reply, "(&o)", &createRequest);
	const std::string createPath(createRequest);
	g_variant_unref(reply);
	auto* createResults = waitForRequest(connection, createPath.c_str());
	if (createResults == nullptr) return EXIT_FAILURE;
	const auto* createText = g_variant_print(createResults, TRUE);
	std::fprintf(stderr, "CreateSession response: %s\n", createText);
	g_free(const_cast<char*>(createText));
	GVariant* sessionValue = g_variant_lookup_value(createResults, "session_handle",
		G_VARIANT_TYPE_STRING);
	if (sessionValue == nullptr)
		sessionValue = g_variant_lookup_value(createResults, "session_handle",
			G_VARIANT_TYPE_OBJECT_PATH);
	g_variant_unref(createResults);
	if (sessionValue == nullptr) {
		std::fprintf(stderr, "Portal did not return a session handle.\n");
		return EXIT_FAILURE;
	}
	const char* sessionPath = g_variant_get_string(sessionValue, nullptr);
	const std::string session(sessionPath);
	g_variant_unref(sessionValue);

	options = optionsBuilder((token + "_select").c_str());
	g_variant_builder_add(options, "{sv}", "types", g_variant_new_uint32(3));
	g_variant_builder_add(options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
	auto* selectResults = callRequest(connection, proxy, "SelectSources",
		g_variant_new("(o@a{sv})", session.c_str(), g_variant_builder_end(options)));
	g_variant_builder_unref(options);
	if (selectResults == nullptr) return EXIT_FAILURE;
	g_variant_unref(selectResults);

	options = optionsBuilder((token + "_start").c_str());
	const auto properties = SDL_GetWindowProperties(parentWindow);
	const char* parentHandle = SDL_GetStringProperty(properties,
		SDL_PROP_WINDOW_WAYLAND_XDG_TOPLEVEL_EXPORT_HANDLE_STRING, nullptr);
	const std::string parent = parentHandle != nullptr
		? std::string("wayland:") + parentHandle : std::string();
	auto* startResults = callRequest(connection, proxy, "Start",
		g_variant_new("(os@a{sv})", session.c_str(), parent.c_str(),
			g_variant_builder_end(options)));
	g_variant_builder_unref(options);
	if (startResults == nullptr) return EXIT_FAILURE;
	GVariant* streams = g_variant_lookup_value(startResults, "streams", G_VARIANT_TYPE_ARRAY);
	g_variant_unref(startResults);
	if (streams == nullptr || g_variant_n_children(streams) == 0) {
		std::fprintf(stderr, "Portal returned no monitor streams.\n");
		return EXIT_FAILURE;
	}
	GVariant* stream = g_variant_get_child_value(streams, 0);
	guint32 nodeId = 0;
	g_variant_get(stream, "(u@a{sv})", &nodeId, nullptr);
	g_variant_unref(stream);
	g_variant_unref(streams);

	GUnixFDList* fdList = nullptr;
	auto* remoteOptions = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);
	GVariant* fdHandle = g_dbus_proxy_call_with_unix_fd_list_sync(proxy,
		"OpenPipeWireRemote", g_variant_new("(o@a{sv})", session.c_str(),
			g_variant_builder_end(remoteOptions)),
		G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &fdList, nullptr, &error);
	g_variant_builder_unref(remoteOptions);
	if (fdHandle == nullptr || fdList == nullptr) {
		std::fprintf(stderr, "Could not obtain PipeWire file descriptor: %s\n",
			error != nullptr ? error->message : "unknown error");
		if (error != nullptr) g_error_free(error);
		return EXIT_FAILURE;
	}
	int fdIndex = -1;
	g_variant_get(fdHandle, "(h)", &fdIndex);
	const int pipewireFd = g_unix_fd_list_get(fdList, fdIndex, &error);
	g_variant_unref(fdHandle);
	g_object_unref(fdList);
	if (pipewireFd < 0) {
		std::fprintf(stderr, "Invalid PipeWire file descriptor.\n");
		return EXIT_FAILURE;
	}

	const std::string pipelineDescription = "pipewiresrc fd=" + std::to_string(pipewireFd) +
		" always-copy=true do-timestamp=true keepalive-time=33" +
		" path=" + std::to_string(nodeId) +
		" ! videoconvert ! video/x-raw,format=RGBA ! appsink name=sink sync=false max-buffers=1 drop=true";
	auto* pipeline = gst_parse_launch(pipelineDescription.c_str(), &error);
	if (pipeline == nullptr) {
		std::fprintf(stderr, "Could not create PipeWire pipeline: %s\n", error->message);
		g_error_free(error);
		return EXIT_FAILURE;
	}
	auto* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 10 * GST_SECOND);
	if (sample == nullptr) {
		std::fprintf(stderr, "No frame received from PipeWire.\n");
		gst_element_set_state(pipeline, GST_STATE_NULL);
		return EXIT_FAILURE;
	}
	auto* caps = gst_sample_get_caps(sample);
	const auto* structure = gst_caps_get_structure(caps, 0);
	int width = 0, height = 0;
	gst_structure_get_int(structure, "width", &width);
	gst_structure_get_int(structure, "height", &height);
	std::printf("Screen capture test passed: node=%u, frame=%dx%d RGBA\n", nodeId, width, height);
	gst_sample_unref(sample);
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(sink);
	gst_object_unref(pipeline);
	g_object_unref(proxy);
	g_object_unref(connection);
	return EXIT_SUCCESS;
}
