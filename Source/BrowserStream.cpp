// Copyright (c) 2026 Outmode; SPDX-License-Identifier: MIT
#include "BrowserStream.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <gst/video/video-converter.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t maxSDP = 64 * 1024;

// Keep display frames in YUV. Only mono conversion needs a small RGB input,
// at the maximum rate/size used by Rendepth's existing live depth models.
struct InferenceConverter {
	GstVideoConverter* converter = nullptr;
	GstVideoInfo input{}, output{};
	~InferenceConverter() { if (converter) gst_video_converter_free(converter); }
	bool fill(const GstVideoInfo& info, const GstVideoFrame& source, VideoFrame& frame) {
		if (!converter || !gst_video_info_is_equal(&input, &info)) {
			if (converter) gst_video_converter_free(converter);
			input = info;
			const double scale = std::min(1.0, 392.0 / std::max(info.width, info.height));
			gst_video_info_set_format(&output, GST_VIDEO_FORMAT_RGBA,
				std::max(1, static_cast<int>(info.width * scale)),
				std::max(1, static_cast<int>(info.height * scale)));
			output.fps_n = info.fps_n; output.fps_d = info.fps_d;
			converter = gst_video_converter_new(&input, &output, nullptr);
			if (!converter) return false;
		}
		auto* buffer = gst_buffer_new_allocate(nullptr, output.size, nullptr);
		if (!buffer) return false;
		GstVideoFrame target{};
		if (!gst_video_frame_map(&target, &output, buffer, GST_MAP_WRITE)) {
			gst_buffer_unref(buffer);
			return false;
		}
		gst_video_converter_frame(converter, &source, &target);
		frame.inferenceWidth = output.width; frame.inferenceHeight = output.height;
		frame.inferenceRGBA.resize(static_cast<size_t>(output.width) * output.height * 4);
		for (int row = 0; row < output.height; ++row)
			std::memcpy(frame.inferenceRGBA.data() + row * output.width * 4,
				GST_VIDEO_FRAME_COMP_DATA(&target, 0) + row * GST_VIDEO_FRAME_COMP_STRIDE(&target, 0), output.width * 4);
		gst_video_frame_unmap(&target);
		gst_buffer_unref(buffer);
		return true;
	}
};

void publish(const std::filesystem::path& path, const std::string& text) {
	const auto pending = path.string() + ".pending";
	std::ofstream output(pending, std::ios::binary);
	output << text;
	output.close();
	if (!output) throw std::runtime_error("Could not write browser connection response.");
	std::filesystem::rename(pending, path);
}

// Promise callbacks run on GStreamer's signalling thread. Keep their state owned
// by the promise, including cancellation, so stop never waits indefinitely.
struct Promise {
	std::atomic<bool>* ready = new std::atomic<bool>(false);
	GstPromise* value = gst_promise_new_with_change_func(
		[](GstPromise*, gpointer data) { static_cast<std::atomic<bool>*>(data)->store(true); },
		ready, [](gpointer data) { delete static_cast<std::atomic<bool>*>(data); });
	~Promise() { gst_promise_interrupt(value); gst_promise_unref(value); }
	void wait(const std::atomic<bool>& cancelled) {
		const auto deadline = Clock::now() + std::chrono::seconds(10);
		while (!ready->load()) {
			if (cancelled || Clock::now() >= deadline)
				throw std::runtime_error("WebRTC negotiation cancelled or timed out.");
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		if (gst_promise_wait(value) != GST_PROMISE_RESULT_REPLIED)
			throw std::runtime_error("WebRTC negotiation did not complete.");
		const auto* reply = gst_promise_get_reply(value);
		if (reply && gst_structure_has_field(reply, "error")) {
			GError* error = nullptr;
			gst_structure_get(reply, "error", G_TYPE_ERROR, &error, nullptr);
			const std::string text = error ? error->message : "WebRTC negotiation failed.";
			g_clear_error(&error);
			throw std::runtime_error(text);
		}
	}
};
}

struct BrowserStream::Impl {
	std::atomic<bool> cancelled{false}, active{false};
	std::mutex mutex;
	std::shared_ptr<VideoFrame> newest;
	std::thread worker;

	void receive(const std::filesystem::path& directory, bool prepareDepth) {
		InferenceConverter inference;
		auto nextInference = Clock::time_point{};
		GstElement* pipeline = nullptr;
		GstElement* peer = nullptr;
		GstElement* sink = nullptr;
		GstBus* bus = nullptr;
		try {
			std::ifstream input(directory / "offer.sdp", std::ios::binary);
			std::string offer(maxSDP + 1, '\0');
			input.read(offer.data(), offer.size());
			offer.resize(input.gcount());
			if (offer.empty() || offer.size() > maxSDP || offer.find('\0') != std::string::npos)
				throw std::runtime_error("Invalid browser SDP offer.");
			GstSDPMessage* sdp = nullptr;
			gst_sdp_message_new(&sdp);
			const auto parsed = gst_sdp_message_parse_buffer(
				reinterpret_cast<const guint8*>(offer.data()), offer.size(), sdp);
			if (parsed != GST_SDP_OK || gst_sdp_message_medias_len(sdp) != 1 ||
				std::strcmp(gst_sdp_media_get_media(gst_sdp_message_get_media(sdp, 0)), "video") != 0) {
				gst_sdp_message_free(sdp);
				throw std::runtime_error("Expected one WebRTC video track.");
			}
			auto* description = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
			GError* parseError = nullptr;
			pipeline = gst_parse_launch(
				"webrtcbin name=peer bundle-policy=max-bundle latency=20 "
				"rtpvp8depay name=depay ! vp8dec ! video/x-raw,format=I420 "
				"! appsink name=frames sync=false max-buffers=1 drop=true enable-last-sample=false", &parseError);
			if (parseError || !pipeline) {
				const std::string text = parseError ? parseError->message : "Could not create WebRTC receiver.";
				g_clear_error(&parseError);
				gst_webrtc_session_description_free(description);
				throw std::runtime_error(text);
			}
			peer = gst_bin_get_by_name(GST_BIN(pipeline), "peer");
			// This receiver is on the same machine: do not discover routers, create
			// UPnP mappings or gather TCP listeners for a local UDP video stream.
			GObject* ice = nullptr;
			g_object_get(peer, "ice-agent", &ice, nullptr);
			if (ice) {
				if (g_object_class_find_property(G_OBJECT_GET_CLASS(ice), "ice-tcp"))
					g_object_set(ice, "ice-tcp", FALSE, nullptr);
				if (g_object_class_find_property(G_OBJECT_GET_CLASS(ice), "agent")) {
					GObject* agent = nullptr;
					g_object_get(ice, "agent", &agent, nullptr);
					if (agent) {
						if (g_object_class_find_property(G_OBJECT_GET_CLASS(agent), "upnp"))
							g_object_set(agent, "upnp", FALSE, nullptr);
						g_object_unref(agent);
					}
				}
				g_object_unref(ice);
			}
			sink = gst_bin_get_by_name(GST_BIN(pipeline), "frames");
			// Preserve the offered transport-wide sequence extension. Advertising
			// transport-cc without its extmap leaves Firefox without bandwidth
			// feedback, even though the sender permits a much higher bitrate.
			auto* receiveCaps = gst_caps_from_string(
				"application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000");
			const auto* media = gst_sdp_message_get_media(sdp, 0);
			for (guint i = 0; i < gst_sdp_media_attributes_len(media); ++i) {
				const auto* attribute = gst_sdp_media_get_attribute(media, i);
				if (std::strcmp(attribute->key, "extmap") != 0 || !attribute->value) continue;
				gchar* end = nullptr;
				const auto id = g_ascii_strtoull(attribute->value, &end, 10);
				if (id < 1 || id > 255 || !end || *end != ' ') continue;
				constexpr auto twcc = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";
				if (std::strcmp(end + 1, twcc) != 0) continue;
				const auto field = "extmap-" + std::to_string(id);
				gst_caps_set_simple(receiveCaps, field.c_str(), G_TYPE_STRING, twcc, nullptr);
			}
			auto* depay = gst_bin_get_by_name(GST_BIN(pipeline), "depay");
			g_signal_connect(peer, "pad-added", G_CALLBACK(+[](GstElement*, GstPad* pad, gpointer data) {
				if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
				auto* target = gst_element_get_static_pad(GST_ELEMENT(data), "sink");
				if (!gst_pad_is_linked(target)) gst_pad_link(pad, target);
				gst_object_unref(target);
			}), depay);
			gst_object_unref(depay); // Pipeline owns it until streaming has stopped.
			g_signal_connect_data(peer, "on-new-transceiver", G_CALLBACK(+[](GstElement*, GstWebRTCRTPTransceiver* transceiver, gpointer caps) {
				g_object_set(transceiver, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
					"codec-preferences", caps, nullptr);
			}), receiveCaps, +[](gpointer caps, GClosure*) { gst_caps_unref(static_cast<GstCaps*>(caps)); },
				static_cast<GConnectFlags>(0));
			bus = gst_element_get_bus(pipeline);
			if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
				gst_webrtc_session_description_free(description);
				throw std::runtime_error("Could not start WebRTC. Check the GStreamer nice, DTLS and SRTP plugins.");
			}
			{
				Promise remote;
				g_signal_emit_by_name(peer, "set-remote-description", description, remote.value);
				gst_webrtc_session_description_free(description);
				remote.wait(cancelled);
			}
			{
				Promise answer;
				g_signal_emit_by_name(peer, "create-answer", nullptr, answer.value);
				answer.wait(cancelled);
				GstWebRTCSessionDescription* local = nullptr;
				gst_structure_get(gst_promise_get_reply(answer.value), "answer",
					GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &local, nullptr);
				if (!local) throw std::runtime_error("Could not create WebRTC answer.");
				// These are our receive limits, not Firefox's receive limits from
				// the offer. 3840x1080 needs 240 * 68 VP8 macroblocks.
				auto* answerMedia = const_cast<GstSDPMedia*>(gst_sdp_message_get_media(local->sdp, 0));
				if (!answerMedia || gst_sdp_media_formats_len(answerMedia) != 1 ||
					gst_sdp_media_get_port(answerMedia) == 0) {
					gst_webrtc_session_description_free(local);
					throw std::runtime_error("Could not negotiate the VP8 video track.");
				}
				for (guint i = gst_sdp_media_attributes_len(answerMedia); i > 0; --i) {
					if (std::strcmp(gst_sdp_media_get_attribute(answerMedia, i - 1)->key, "fmtp") == 0)
						gst_sdp_media_remove_attribute(answerMedia, i - 1);
				}
				const std::string format = std::string(gst_sdp_media_get_format(answerMedia, 0)) +
					" max-fs=16320;max-fr=60";
				gst_sdp_media_add_attribute(answerMedia, "fmtp", format.c_str());
				Promise setLocal;
				g_signal_emit_by_name(peer, "set-local-description", local, setLocal.value);
				gst_webrtc_session_description_free(local);
				setLocal.wait(cancelled);
			}
			bool answered = false;
			auto oversizedSince = Clock::time_point{};
			const auto deadline = Clock::now() + std::chrono::seconds(10);
			std::error_code heartbeatError;
			auto heartbeatStamp = std::filesystem::last_write_time(directory / "heartbeat", heartbeatError);
			const bool monitorHost = !heartbeatError; // Older hosts and standalone receiver tests omit it.
			auto hostSeen = Clock::now();
			auto nextHostCheck = hostSeen;
			auto disconnectedSince = Clock::time_point{};
			while (!cancelled) {
				std::error_code ec;
				if (!std::filesystem::is_directory(directory, ec)) break;
				if (monitorHost && Clock::now() >= nextHostCheck) {
					const auto stamp = std::filesystem::last_write_time(directory / "heartbeat", ec);
					if (ec) break;
					if (stamp != heartbeatStamp) { heartbeatStamp = stamp; hostSeen = Clock::now(); }
					if (Clock::now() - hostSeen >= std::chrono::seconds(5))
						throw std::runtime_error("The browser connection closed.");
					nextHostCheck = Clock::now() + std::chrono::seconds(1);
				}
				if (auto* message = gst_bus_pop_filtered(bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
					GError* error = nullptr;
					if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) gst_message_parse_error(message, &error, nullptr);
					const std::string text = error ? error->message : "Browser video stream ended.";
					g_clear_error(&error);
					gst_message_unref(message);
					throw std::runtime_error(text);
				}
				if (!answered) {
					GstWebRTCICEGatheringState state;
					g_object_get(peer, "ice-gathering-state", &state, nullptr);
					if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
						GstWebRTCSessionDescription* local = nullptr;
						g_object_get(peer, "local-description", &local, nullptr);
						if (!local) throw std::runtime_error("Missing WebRTC answer.");
						gchar* text = gst_sdp_message_as_text(local->sdp);
						const std::string answer(text);
						g_free(text);
						gst_webrtc_session_description_free(local);
						publish(directory / "answer.sdp", answer);
						answered = true;
					} else if (Clock::now() >= deadline) throw std::runtime_error("WebRTC candidate gathering timed out.");
				}
				GstWebRTCPeerConnectionState connection;
				g_object_get(peer, "connection-state", &connection, nullptr);
				if (connection == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED)
					throw std::runtime_error("The local WebRTC connection failed.");
				if (connection == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED) break;
				if (connection == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED) {
					if (disconnectedSince == Clock::time_point{}) disconnectedSince = Clock::now();
					if (Clock::now() - disconnectedSince >= std::chrono::seconds(5))
						throw std::runtime_error("The local WebRTC connection was lost.");
				} else disconnectedSince = {};
				auto* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 20 * GST_MSECOND);
				if (!sample) continue;
				GstVideoInfo info;
				GstVideoFrame mapped;
				if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) ||
					GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_I420 ||
					info.width <= 0 || info.height <= 0) {
					gst_sample_unref(sample);
					throw std::runtime_error("Browser sent an invalid video format.");
				}
				// Source-size changes can race the sender's scaling update. Hold the
				// last valid picture briefly instead of tearing down the whole peer.
				if (info.width > 3840 || info.height > 1080) {
					gst_sample_unref(sample);
					if (oversizedSince == Clock::time_point{}) oversizedSince = Clock::now();
					if (Clock::now() - oversizedSince > std::chrono::seconds(5))
						throw std::runtime_error("Browser video stayed above the supported 3840x1080 stream size.");
					continue;
				}
				oversizedSince = {};
				if (gst_video_frame_map(&mapped, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
					auto frame = std::make_shared<VideoFrame>();
					frame->format = VideoFrame::Format::YUV420P;
					frame->fullRange = info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
					frame->colorSpace = info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT709
						? VideoFrame::ColorSpace::BT709 : VideoFrame::ColorSpace::BT601;
					frame->width = frame->outputWidth = info.width;
					frame->height = frame->outputHeight = info.height;
					frame->generation = 1;
					frame->presentationTime = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
					for (int plane = 0; plane < 3; ++plane) {
						const int width = GST_VIDEO_FRAME_COMP_WIDTH(&mapped, plane);
						const int height = GST_VIDEO_FRAME_COMP_HEIGHT(&mapped, plane);
						frame->planes[plane].resize(static_cast<size_t>(width) * height);
						for (int row = 0; row < height; ++row)
							std::memcpy(frame->planes[plane].data() + row * width,
								GST_VIDEO_FRAME_COMP_DATA(&mapped, plane) + row * GST_VIDEO_FRAME_COMP_STRIDE(&mapped, plane), width);
					}
					if (prepareDepth && Clock::now() >= nextInference) {
						if (!inference.fill(info, mapped, *frame)) {
							gst_video_frame_unmap(&mapped);
							gst_sample_unref(sample);
							throw std::runtime_error("Could not prepare Firefox video for depth conversion.");
						}
						nextInference = Clock::now() + std::chrono::milliseconds(50);
					}
					gst_video_frame_unmap(&mapped);
					std::lock_guard lock(mutex);
					newest = std::move(frame);
				}
				gst_sample_unref(sample);
			}
		} catch (const std::exception& error) {
			if (!cancelled) {
				try { publish(directory / "error", error.what()); } catch (...) {}
			}
		}
		if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
		if (bus) gst_object_unref(bus);
		if (sink) gst_object_unref(sink);
		if (peer) gst_object_unref(peer);
		if (pipeline) gst_object_unref(pipeline);
		active = false;
	}
};

BrowserStream::BrowserStream() : impl(std::make_unique<Impl>()) {}
BrowserStream::~BrowserStream() { stop(); }
bool BrowserStream::start(const std::string& directory, std::string& error, bool prepareDepth) {
	stop();
	std::error_code ec;
	if (!std::filesystem::is_regular_file(std::filesystem::path(directory) / "offer.sdp", ec)) {
		error = "Firefox WebRTC offer is missing. Reload the extension and restart the connection.";
		return false;
	}
	gst_init(nullptr, nullptr);
	for (const char* name : {"webrtcbin", "nicesrc", "nicesink", "dtlssrtpdec", "dtlssrtpenc", "rtpvp8depay", "vp8dec", "appsink"}) {
		auto* factory = gst_element_factory_find(name);
		if (!factory) {
			error = std::string("Firefox streaming needs the GStreamer plugin '") + name +
				"'. Install the WebRTC, libnice and VP8 plugins (see Browser/Firefox/README.md).";
			return false;
		}
		gst_object_unref(factory);
	}
	impl->cancelled = false;
	impl->active = true;
	impl->worker = std::thread([this, directory, prepareDepth] { impl->receive(directory, prepareDepth); });
	return true;
}
void BrowserStream::stop() {
	impl->cancelled = true;
	if (impl->worker.joinable()) impl->worker.join();
	impl->active = false;
	std::lock_guard lock(impl->mutex);
	impl->newest.reset();
}
bool BrowserStream::running() const { return impl->active; }
std::shared_ptr<VideoFrame> BrowserStream::takeFrame() {
	std::lock_guard lock(impl->mutex);
	return std::exchange(impl->newest, nullptr);
}
