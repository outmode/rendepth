// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "DiscTitleMenu.h"
#include "BlurayReader.h"
#include "DvdReader.h"
#include "Core.h"
#include "Style.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef RENDEPTH_ENABLE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}
#endif

namespace {
using Surface = std::shared_ptr<SDL_Surface>;
struct Title {
    int id;
    std::string label;
    double duration;
    int chapters;
    int audioTracks;
    Surface preview;
    bool previewComplete = false;
    int number = 0;
    bool commentaryAvailable = false;
    std::vector<std::string> languages;
    bool mainFeatureCandidate = false;
    bool likelyMainFeature = false;
    bool mvc = false;
    std::string displayName;
};
struct Scan {
    std::mutex mutex;
    std::atomic<bool> cancel{false}, done{false};
    std::vector<Title> titles;
    std::string name, error;
    unsigned revision = 0;
    bool autoPlayMainFeature = false;
    std::optional<int> automaticSelection;
};
// Format a title duration as minutes and seconds, adding hours when needed.
std::string durationText(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    return total >= 3600 ? std::format("{}:{:02}:{:02}", total / 3600, total / 60 % 60, total % 60)
        : std::format("{}:{:02}", total / 60, total % 60);
}
// Combine duration and chapter count into a compact title-card label.
std::string chapterText(const Title& title) {
    return std::format("{} / {} {}", durationText(title.duration), title.chapters,
        title.chapters == 1 ? "Chapter" : "Chapters");
}
// Mark a clearly dominant feature-length title as a likely main feature when the metadata permits it.
void suggestMainFeature(std::vector<Title>& titles, bool allowDurationGuess) {
    // A dominant, feature-length title is a useful hint, not an authored name.
    // Avoid guessing on episodic discs or discs with multiple similar-length cuts.
    if (!titles.empty() && titles.front().duration >= 40 * 60 &&
        (titles.size() == 1 || titles.front().duration > titles[1].duration * 1.2)) {
        if (allowDurationGuess) titles.front().mainFeatureCandidate = true;
        if (titles.front().mainFeatureCandidate) {
            titles.front().likelyMainFeature = true;
            titles.front().label = "Likely Main Feature";
        }
    }
}
#ifdef RENDEPTH_ENABLE_FFMPEG
// Read the selected playlist's title and audio metadata for the disc menu.
void readTitleMetadata(AVFormatContext* format, int stream, Title& title) {
    // Stream/container tags refer to this selected playlist. Blu-ray menu-title
    // numbers are a different namespace and must not be matched to playlist IDs.
    auto* titleTag = av_dict_get(format->streams[stream]->metadata, "title", nullptr, 0);
    if (!titleTag || !titleTag->value[0]) titleTag = av_dict_get(format->metadata, "title", nullptr, 0);
    if (titleTag && titleTag->value[0]) title.label = titleTag->value;
    int audioCount = 0;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const auto* audio = format->streams[i];
        if (audio->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        ++audioCount;
        title.commentaryAvailable |= (audio->disposition & AV_DISPOSITION_COMMENT) != 0;
        const auto* language = av_dict_get(audio->metadata, "language", nullptr, 0);
        if (language && language->value[0] && std::string(language->value) != "und" &&
            std::find(title.languages.begin(), title.languages.end(), language->value) == title.languages.end())
            title.languages.emplace_back(language->value);
    }
    title.audioTracks = std::max(title.audioTracks, audioCount);
}
struct PreviewInput {
    AVFormatContext* format = nullptr;
    AVIOContext* io = nullptr;
    AVCodecContext* codec = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    // Release all FFmpeg resources owned by a thumbnail probe.
    ~PreviewInput() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    }
};
struct PreviewDeadline {
    Scan* scan;
    std::chrono::steady_clock::time_point end;
    // Stop preview decoding when the scan is cancelled or its time budget expires.
    bool stopped() const { return scan->cancel || std::chrono::steady_clock::now() > end; }
};
// Decode a representative title thumbnail within a deadline, preferring a visible frame over a dark
// opening.
template<class Reader>
Surface thumbnail(Reader& reader, Title& title, Scan& scan, const char* demuxer) {
    std::string error;
    if (!reader.selectTitle(title.id, error) || scan.cancel) return {};
    PreviewDeadline deadline{&scan, std::chrono::steady_clock::now() + std::chrono::seconds(8)};
    // Probe from the title start so video parameter sets and audio core frames
    // are available. Seeking before probing can begin on dependent packets.
    struct Source { Reader* reader; PreviewDeadline* deadline; } source{&reader, &deadline};
    PreviewInput input;
    input.io = reader.createAVIOContext();
    input.format = avformat_alloc_context();
    if (!input.io || !input.format || !input.frame || !input.packet) return {};
    input.io->opaque = &source;
    input.io->read_packet = [](void* opaque, uint8_t* buffer, int size) {
        auto* source = static_cast<Source*>(opaque);
        return source->deadline->stopped() ? AVERROR_EXIT : Reader::readPacket(source->reader, buffer, size);
    };
    input.io->seek = nullptr;
    input.io->seekable = 0;
    input.format->skip_estimate_duration_from_pts = 1;
    input.format->pb = input.io;
    input.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    input.format->interrupt_callback = {[](void* p) { return static_cast<PreviewDeadline*>(p)->stopped() ? 1 : 0; }, &deadline};
    input.format->probesize = 2 * 1024 * 1024;
    input.format->max_analyze_duration = 2 * AV_TIME_BASE;
    if (avformat_open_input(&input.format, nullptr, av_find_input_format(demuxer), nullptr) < 0 ||
        avformat_find_stream_info(input.format, nullptr) < 0 || deadline.stopped()) return {};
    const AVCodec* decoder = nullptr;
    const int stream = av_find_best_stream(input.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (stream < 0) return {};
    readTitleMetadata(input.format, stream, title);
    input.codec = avcodec_alloc_context3(decoder);
    if (!input.codec || avcodec_parameters_to_context(input.codec, input.format->streams[stream]->codecpar) < 0) return {};
    input.codec->thread_count = 2;
    if (avcodec_open2(input.codec, decoder, nullptr) < 0) return {};
    // Capture a scene around 10% into this title instead of its opening logo.
    // Use the disc reader's seek (Blu-ray access points / DVD sector estimate)
    // rather than FFmpeg's byte search through the custom, non-seekable IO.
    // Keep the probed codec parameters, but discard all buffered opening data.
    if (std::isfinite(title.duration) && title.duration > 0.0 &&
        reader.seekTime(title.duration * 0.1)) {
        avio_flush(input.io);
        input.io->pos = reader.bytePosition();
        input.io->eof_reached = 0;
        input.io->error = 0;
        avformat_flush(input.format);
        avcodec_flush_buffers(input.codec);
    }
    Surface fallback;
    for (int packets = 0; packets < 6000 && !deadline.stopped(); ++packets) {
        if (av_read_frame(input.format, input.packet) < 0) break;
        const int sent = input.packet->stream_index == stream ? avcodec_send_packet(input.codec, input.packet) : AVERROR(EAGAIN);
        av_packet_unref(input.packet);
        if (sent < 0) continue;
        while (avcodec_receive_frame(input.codec, input.frame) >= 0 && !deadline.stopped()) {
            const int w = input.frame->width, h = input.frame->height;
            if (w <= 0 || h <= 0) continue;
            const int outW = 480, outH = std::max(1, static_cast<int>(480.0 * h / w));
            Surface surface(SDL_CreateSurface(outW, outH, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            if (!surface) return {};
            SwsContext* scaler = sws_getContext(w, h, static_cast<AVPixelFormat>(input.frame->format),
                outW, outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler) return {};
            uint8_t* pixels[] = {static_cast<uint8_t*>(surface->pixels)};
            int pitch[] = {surface->pitch};
            sws_scale(scaler, input.frame->data, input.frame->linesize, 0, h, pixels, pitch);
            sws_freeContext(scaler);
            fallback = surface;
            // Avoid a black transition if the next decoded frame has a picture.
            uint64_t brightness = 0;
            for (int y = 0; y < outH; y += 8) for (int x = 0; x < outW; x += 8) {
                const auto* p = pixels[0] + y * pitch[0] + x * 4;
                brightness += p[0] + p[1] + p[2];
            }
            if (brightness > static_cast<uint64_t>((outH + 7) / 8 * ((outW + 7) / 8) * 24)) return surface;
        }
    }
    return fallback;
}
#endif

// Scan disc titles on a worker and publish metadata and previews incrementally for the menu.
template<class Reader>
void scanDisc(Reader& reader, const std::filesystem::path& path, const std::shared_ptr<Scan>& scan, const char* demuxer) {
    std::string error;
    if (!reader.open(path, error)) {
        std::lock_guard lock(scan->mutex);
        scan->error = error.empty() ? "Could not read disc" : error;
        ++scan->revision;
        return;
    }
    std::vector<Title> titles;
    for (size_t i = 0; i < reader.titles().size(); ++i) {
        const auto& t = reader.titles()[i];
        int id;
        if constexpr (std::is_same_v<Reader, BlurayReader>) id = t.index;
        else id = static_cast<int>(i);
        int number = id + 1;
        if constexpr (std::is_same_v<Reader, DvdReader>) number = t.titleNum;
        titles.push_back({id, std::format("Title {}", number), t.duration, t.chapterCount, t.audioTrackCount});
        auto& title = titles.back();
        title.number = number;
        title.languages = t.audioLanguages;
        if constexpr (std::is_same_v<Reader, BlurayReader>) {
            title.mainFeatureCandidate = t.mainFeatureCandidate;
            title.mvc = t.mvc;
            title.displayName = t.name;
        }
        else title.commentaryAvailable = t.commentaryAvailable;
    }
    std::stable_sort(titles.begin(), titles.end(), [](const auto& a, const auto& b) { return a.duration > b.duration; });
    if (titles.empty()) {
        std::lock_guard lock(scan->mutex);
        scan->error = "No playable disc titles found";
        ++scan->revision;
        return;
    }
    suggestMainFeature(titles, std::is_same_v<Reader, DvdReader>);
    {
        std::lock_guard lock(scan->mutex);
        scan->name = reader.discTitle();
        scan->titles = titles;
        if (scan->autoPlayMainFeature) {
            // Prefer the disc reader's main-feature hint; use its longest
            // title choice when the disc supplies no hint (including DVDs).
            const auto main = std::find_if(titles.begin(), titles.end(), [](const auto& title) {
                return title.mainFeatureCandidate;
            });
            scan->automaticSelection = main != titles.end() ? main->id : reader.activeTitle();
#ifdef RENDEPTH_ENABLE_MVC
            const auto selected = std::find_if(titles.begin(), titles.end(), [&](const auto& title) {
                return title.id == *scan->automaticSelection;
            });
            if (selected != titles.end() && !selected->mvc) {
                // Some 3D discs advertise the mono playlist as their main
                // title. Prefer a matching feature-length stereo variant.
                const auto stereo = std::find_if(titles.begin(), titles.end(), [&](const auto& title) {
                    return title.mvc && std::abs(title.duration - selected->duration) < 1.0 &&
                        title.chapters == selected->chapters;
                });
                if (stereo != titles.end()) scan->automaticSelection = stereo->id;
            }
#endif
        }
        ++scan->revision;
    }
    // Autoplay needs only playlist metadata. Thumbnail reads would delay
    // playback and compete with the player's reads from the optical drive.
    if (scan->autoPlayMainFeature) return;
    for (size_t i = 0; i < titles.size() && !scan->cancel; ++i) {
        Surface preview;
#ifdef RENDEPTH_ENABLE_FFMPEG
        preview = thumbnail(reader, titles[i], *scan, demuxer);
#endif
        if (scan->cancel) break;
        std::lock_guard lock(scan->mutex);
        scan->titles[i].preview = std::move(preview);
        scan->titles[i].previewComplete = true;
        scan->titles[i].label = titles[i].label;
        scan->titles[i].commentaryAvailable = titles[i].commentaryAvailable;
        scan->titles[i].languages = titles[i].languages;
        scan->titles[i].audioTracks = titles[i].audioTracks;
        ++scan->revision;
    }
}
}

struct DiscTitleMenu::Impl {
    struct Worker { std::shared_ptr<Scan> scan; std::thread thread; };
    std::vector<Worker> workers;
    std::shared_ptr<Scan> scan;
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUTexture* depthTexture = nullptr;
    TTF_Font* fallbackFont = nullptr;
    bool checkedFallbackFont = false;
    bool active = false, visible = false, dirty = true;
    int width = 0, height = 0, page = 0, focus = 0, columns = 3, perPage = 6;
    int hovered = -1;
    unsigned revision = 0;
    std::optional<int> pending;
    BackgroundStyle backgroundStyle = Dark;
    std::vector<SDL_Rect> cards;
    // Request cancellation of the current disc scan.
    void stop() { if (scan) scan->cancel = true; }
    // Join completed scan workers without waiting for scans that are still active.
    void reap() {
        for (auto it = workers.begin(); it != workers.end();) {
            if (it->scan->done) { it->thread.join(); it = workers.erase(it); }
            else ++it;
        }
    }
};
// Allocate the menu's private scan, layout, and rendering state.
DiscTitleMenu::DiscTitleMenu() : impl(std::make_unique<Impl>()) {}
// Cancel and join every remaining scan worker before destroying menu state.
DiscTitleMenu::~DiscTitleMenu() {
    for (auto& worker : impl->workers) worker.scan->cancel = true;
    for (auto& worker : impl->workers) worker.thread.join();
}
// Begin a background scan for a newly opened disc and reset menu navigation.
void DiscTitleMenu::open(const std::filesystem::path& path, bool autoPlayMainFeature) {
    close();
    impl->reap();
    impl->scan = std::make_shared<Scan>();
    impl->scan->autoPlayMainFeature = autoPlayMainFeature;
    impl->active = true; impl->dirty = true; impl->page = 0; impl->focus = 0; impl->hovered = -1;
    impl->pending.reset(); impl->revision = 0;
    auto scan = impl->scan;
    impl->workers.push_back({scan, std::thread([scan, path] {
        try {
            if (BlurayReader::isBluraySource(path)) { BlurayReader reader; scanDisc(reader, path, scan, "mpegts"); }
            else { DvdReader reader; scanDisc(reader, path, scan, "mpeg"); }
        } catch (const std::exception& e) {
            std::lock_guard lock(scan->mutex); scan->error = e.what(); ++scan->revision;
        }
        scan->done = true;
    })});
}
// Hide the menu and cancel its current scan and pending selection.
void DiscTitleMenu::close() { impl->stop(); impl->active = false; impl->visible = false; impl->pending.reset(); }
// Join scan workers and release menu textures and fallback fonts before renderer shutdown.
void DiscTitleMenu::shutdown(Context* context) {
    close();
    for (auto& worker : impl->workers) worker.scan->cancel = true;
    for (auto& worker : impl->workers) worker.thread.join();
    impl->workers.clear(); impl->scan.reset();
    if (impl->texture) SDL_ReleaseGPUTexture(context->device, impl->texture);
    impl->texture = nullptr;
    if (impl->depthTexture) SDL_ReleaseGPUTexture(context->device, impl->depthTexture);
    impl->depthTexture = nullptr;
    if (impl->fallbackFont) TTF_CloseFont(impl->fallbackFont);
    impl->fallbackFont = nullptr;
}
// Report whether the menu has a visible rendered page.
bool DiscTitleMenu::active() const { return impl->active; }
bool DiscTitleMenu::visible() const { return impl->visible; }
// Return a scan failure and close the failed menu.
std::string DiscTitleMenu::takeError() {
    if (!impl->active) return {};
    std::string error;
    {
        std::lock_guard lock(impl->scan->mutex);
        error = impl->scan->error;
    }
    if (!error.empty()) close();
    return error;
}
// Move between title pages with wraparound and reset focus for the new page.
void DiscTitleMenu::pageBy(int delta) {
    if (!visible() || impl->pending) return;
    std::lock_guard lock(impl->scan->mutex);
    const int pages = std::max(1, (static_cast<int>(impl->scan->titles.size()) + impl->perPage - 1) / impl->perPage);
    impl->page = (impl->page + delta % pages + pages) % pages;
    impl->focus = impl->page * impl->perPage;
    impl->hovered = -1;
    impl->dirty = true;
}
// Report whether the visible menu contains more than one page of titles.
bool DiscTitleMenu::hasPages() const {
    if (!visible()) return false;
    std::lock_guard lock(impl->scan->mutex);
    return impl->scan->titles.size() > static_cast<size_t>(impl->perPage);
}
// Choose a stable disc backdrop from the main-feature candidate or longest title.
std::shared_ptr<SDL_Surface> DiscTitleMenu::backgroundPreview() const {
    if (!visible()) return {};
    std::lock_guard lock(impl->scan->mutex);
    const auto& titles = impl->scan->titles;
    if (titles.empty()) return {};
    // Keep one backdrop for the disc, independent of hover, focus, and paging.
    // Titles are sorted by duration, so the longest is the fallback candidate.
    const auto feature = std::find_if(titles.begin(), titles.end(), [](const Title& title) {
        return title.mainFeatureCandidate;
    });
    return (feature != titles.end() ? *feature : titles.front()).preview;
}
// Expose the visible menu's color texture to the main renderer.
SDL_GPUTexture* DiscTitleMenu::texture() const { return impl->visible ? impl->texture : nullptr; }
// Expose the visible menu's depth texture for stereo presentation.
SDL_GPUTexture* DiscTitleMenu::depthTexture() const { return impl->visible ? impl->depthTexture : nullptr; }
// Describe the hovered title's duration, chapters, and available audio tracks.
std::string DiscTitleMenu::hoveredMetadata() const {
    if (!visible() || impl->hovered < 0) return {};
    std::lock_guard lock(impl->scan->mutex);
    const int index = impl->page * impl->perPage + impl->hovered;
    if (index < 0 || index >= static_cast<int>(impl->scan->titles.size())) return {};
    const auto& title = impl->scan->titles[index];
    auto details = chapterText(title);
    if (title.audioTracks > 0) details += std::format(" / {} Audio {}", title.audioTracks,
        title.audioTracks == 1 ? "Track" : "Tracks");
    for (auto language : title.languages) {
        if (!language.empty()) language[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(language[0])));
        details += " / " + language;
    }
    if (title.commentaryAvailable) details += " / Commentary Available";
    if (title.label != "Likely Main Feature" && title.label != std::format("Title {}", title.number))
        details += " / " + title.label;
    return details;
}
// Consume a manual or automatic title selection after the disc scan has completed.
std::optional<int> DiscTitleMenu::takeSelection() {
    if (!impl->active || !impl->scan || !impl->scan->done) return {};
    if (!impl->pending && impl->scan->autoPlayMainFeature) {
        std::lock_guard lock(impl->scan->mutex);
        impl->pending = impl->scan->automaticSelection;
    }
    if (!impl->pending || !impl->scan->done) return {};
    auto result = impl->pending;
    close();
    return result;
}

// Queue a title while the scan finishes, including a return to the title that was playing.
void DiscTitleMenu::requestSelection(int title) {
    if (!impl->active || title < 0) return;
    impl->pending = title;
    impl->stop();
    impl->dirty = true;
}

// Handle title-menu pointer and keyboard navigation and selection.
bool DiscTitleMenu::handleEvent(const SDL_Event& e, SDL_Window* window) {
    if (!visible()) return false;
    const bool pointer = e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP;
    const bool key = e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP;
    if (!pointer && !key && e.type != SDL_EVENT_MOUSE_WHEEL) return false;
    int count;
    { std::lock_guard lock(impl->scan->mutex); count = static_cast<int>(impl->scan->titles.size()); }
    auto choose = [&](int index) {
        if (impl->pending || index < 0 || index >= count) return;
        std::lock_guard lock(impl->scan->mutex);
        impl->pending = impl->scan->titles[index].id;
        impl->stop(); impl->dirty = true;
    };
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return false;
        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) choose(impl->focus);
        else if (e.key.key == SDLK_PAGEDOWN) { if (!e.key.repeat) pageBy(1); }
        else if (e.key.key == SDLK_PAGEUP) { if (!e.key.repeat) pageBy(-1); }
        else if (e.key.key == SDLK_TAB) {
            impl->hovered = -1;
            impl->focus = (impl->focus + ((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1) + std::max(1, count)) % std::max(1, count);
            impl->page = impl->focus / impl->perPage; impl->dirty = true;
        }
        else return false; // App shortcuts, including arrows and fullscreen, stay active.
        return true;
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        float dy = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        if (dy != 0) pageBy(dy < 0 ? 1 : -1);
        return true;
    } else if (pointer) {
        int w, h; SDL_GetWindowSize(window, &w, &h);
        float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
        float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
        // Each split viewport shows the whole menu. Its rendered cursor uses
        // the full window's mouse range, so hit testing must use that range too.
        SDL_Point p{static_cast<int>(x * impl->width / std::max(1, w)),
            static_cast<int>(y * impl->height / std::max(1, h))};
        int hover = -1;
        for (size_t i = 0; i < impl->cards.size(); ++i) if (SDL_PointInRect(&p, &impl->cards[i])) hover = static_cast<int>(i);
        if (impl->hovered != hover) { impl->hovered = hover; impl->dirty = true; }
        if (hover >= 0) impl->focus = impl->page * impl->perPage + hover;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (hover >= 0) { choose(impl->page * impl->perPage + hover); return true; }
        }
    }
    return false;
}

// Rebuild title cards and their color/depth textures when scan results or layout state change.
void DiscTitleMenu::update(Context* context, TTF_Font* font) {
    impl->reap();
    if (!impl->active || !font) return;
    if (impl->scan->autoPlayMainFeature) return;
    if (!impl->checkedFallbackFont) {
        impl->checkedFallbackFont = true;
        // Preserve disc names in their original language when a system CJK font exists.
        std::vector<std::filesystem::path> candidates{
            "/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
            "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc"};
#ifdef _WIN32
        if (const char* root = std::getenv("WINDIR")) candidates.insert(candidates.begin(), std::filesystem::path(root) / "Fonts/msgothic.ttc");
#endif
        for (const auto& path : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) impl->fallbackFont = TTF_OpenFont(path.string().c_str(), 20);
            if (impl->fallbackFont) break;
        }
    }
    // Full SBS uses an undistorted canvas per eye. Half SBS/RGBD retain
    // the full-width canvas, matching the rest of the compressed split UI.
    int w, h; SDL_GetWindowSize(context->window, &w, &h);
    if (context->fullscreen && context->mode == SBS_Full) w /= 2;
    w = std::max(1, w); h = std::max(1, h);
    std::vector<Title> titles;
    std::string name;
    {
        std::lock_guard lock(impl->scan->mutex);
        // The application presents failures through its standard centered error.
        if (!impl->scan->error.empty() || impl->scan->titles.empty()) return;
        if (!impl->dirty && impl->revision == impl->scan->revision && impl->width == w && impl->height == h &&
            impl->backgroundStyle == context->backgroundStyle) return;
        impl->revision = impl->scan->revision;
        titles = impl->scan->titles; name = impl->scan->name;
    }
    impl->width = w; impl->height = h; impl->dirty = false;
    impl->backgroundStyle = context->backgroundStyle;
    Surface canvas(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    Surface depthCanvas(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!canvas || !depthCanvas) return;
    // Match the options UI: 0.5 background, 0.75 blocks and 1.0 highlight.
    SDL_FillSurfaceRect(depthCanvas.get(), nullptr, SDL_MapSurfaceRGBA(depthCanvas.get(), 128, 128, 128, 255));
    Style style;
    auto theme = [&](Style::Color color) { auto c = style.getColor(color, Style::Alpha::Solid); return SDL_Color{Uint8(c.r*255), Uint8(c.g*255), Uint8(c.b*255), 255}; };
    const auto pink = theme(Style::Color::Pink), white = theme(Style::Color::White), gray = theme(Style::Color::Gray);
    const bool light = context->backgroundStyle == Light;
    const SDL_Color darkGray{Uint8(255-gray.r), Uint8(255-gray.g), Uint8(255-gray.b), 255};
    const SDL_Color heading = light ? darkGray : white;
    const SDL_Color caption = light ? darkGray : gray;
    const SDL_Color cardBackground = light ? white : SDL_Color{28,28,28,255};
    auto fill = [&](SDL_Rect rect, SDL_Color color) { SDL_FillSurfaceRect(canvas.get(), &rect, SDL_MapSurfaceRGBA(canvas.get(), color.r, color.g, color.b, color.a)); };
    // Transparent content lets the main renderer supply the user's background.
    fill({0,0,w,h}, {0,0,0,0});
    auto text = [&](const std::string& value, int x, int y, int size, SDL_Color color, int maxWidth,
                    SDL_Surface* target = nullptr) {
        auto* textFont = TTF_CopyFont(font);
        if (!textFont) return;
        TTF_SetFontSize(textFont, size);
        if (impl->fallbackFont) {
            TTF_SetFontSize(impl->fallbackFont, size);
            TTF_AddFallbackFont(textFont, impl->fallbackFont);
        }
        std::string label = value;
        int measured = 0;
        TTF_GetStringSize(textFont, label.c_str(), label.size(), &measured, nullptr);
        if (measured > maxWidth) {
            do {
                if (label.empty()) break;
                size_t last = label.size() - 1;
                while (last > 0 && (static_cast<unsigned char>(label[last]) & 0xc0) == 0x80) --last;
                label.resize(last);
                const auto shortened = label + "...";
                TTF_GetStringSize(textFont, shortened.c_str(), shortened.size(), &measured, nullptr);
            } while (measured > maxWidth);
            label += "...";
        }
        Surface rendered(TTF_RenderText_Blended(textFont, label.c_str(), label.size(), color), SDL_DestroySurface);
        TTF_CloseFont(textFont);
        if (!rendered) return;
        SDL_Rect source{0,0,std::max(0, std::min(maxWidth, rendered->w)),rendered->h};
        SDL_Rect dest{x,y,source.w,source.h}; SDL_BlitSurface(rendered.get(), &source, target ? target : canvas.get(), &dest);
    };
    const int margin = std::min(std::max(24, w / 5), 112), gap = 18;
    const int contentW = std::max(1, std::min(1200, w - margin * 2)), left = (w - contentW) / 2;
    impl->columns = w >= 1120 ? 3 : w >= 680 ? 2 : 1;
    const int cardW = (contentW - gap*(impl->columns-1)) / impl->columns;
    // Fit up to three rows while keeping thumbnails and metadata readable.
    const int rows = std::clamp((h - 235 + gap) / (72 + 87 + gap), 1, 3);
    const int imageH = std::min(cardW*9/16, std::max(72, (h-235-gap*(rows-1))/rows-87));
    const int cardH = imageH + 87;
    impl->perPage = rows * impl->columns;
    const int pages = std::max(1, (static_cast<int>(titles.size()) + impl->perPage-1) / impl->perPage);
    impl->page = std::clamp(impl->focus / impl->perPage, 0, pages-1);
    const int visibleCards = std::min(impl->perPage,
        static_cast<int>(titles.size()) - impl->page * impl->perPage);
    const int visibleRows = (visibleCards + impl->columns - 1) / impl->columns;
    // Center the heading, page status and populated card rows as one group.
    // Empty slots on a short page must not push the visible content upward.
    const int contentH = 73 + visibleRows * cardH + (visibleRows - 1) * gap + 2;
    const int top = std::max(0, (h - contentH) / 2);
    text(name.empty() ? "Blu-ray / DVD" : name, left, top, 28, heading, contentW);
    text(name.empty() ? "Blu-ray / DVD" : name, left, top, 28, {255,255,255,255}, contentW, depthCanvas.get());
    impl->cards.clear();
    for (int n = 0; n < impl->perPage; ++n) {
        int index = impl->page * impl->perPage + n;
        if (index >= static_cast<int>(titles.size())) break;
        const auto& title = titles[index];
        SDL_Rect card{left+(n%impl->columns)*(cardW+gap), top+73+(n/impl->columns)*(cardH+gap),cardW,cardH};
        impl->cards.push_back(card);
        bool selected = impl->hovered == n || (impl->hovered < 0 && impl->focus == index);
        fill({card.x-2,card.y-2,card.w+4,card.h+4}, selected ? pink : light ? gray : SDL_Color{72,72,72,255});
        const Uint8 depth = selected ? 255 : 191;
        const SDL_Rect depthCard{card.x-2, card.y-2, card.w+4, card.h+4};
        SDL_FillSurfaceRect(depthCanvas.get(), &depthCard,
            SDL_MapSurfaceRGBA(depthCanvas.get(), depth, depth, depth, 255));
        const bool hovered = impl->hovered == n;
        fill(card, hovered ? pink : cardBackground);
        SDL_Rect picture{card.x,card.y,card.w,imageH};
        fill(picture, light ? white : SDL_Color{20,20,20,255});
        if (title.preview) {
            float scale = std::min(float(picture.w)/title.preview->w,float(picture.h)/title.preview->h);
            SDL_Rect dest{0,0,int(title.preview->w*scale),int(title.preview->h*scale)};
            dest.x=picture.x+(picture.w-dest.w)/2; dest.y=picture.y+(picture.h-dest.h)/2;
            SDL_BlitSurfaceScaled(title.preview.get(), nullptr, canvas.get(), &dest, SDL_SCALEMODE_LINEAR);
        } else text(title.previewComplete ? "Preview unavailable" : "Loading preview...", card.x+18,card.y+imageH/2-10,16,caption,card.w-36);
        auto cardTitle = !title.displayName.empty()
            ? std::format("{} ({})", title.displayName, title.number)
            : title.label;
        if (title.mvc) cardTitle += " / Blu-ray 3D";
        text(cardTitle,card.x+16,card.y+imageH+9,20,hovered ? white : heading,card.w-32);
        text(chapterText(title),card.x+16,card.y+imageH+36,15,hovered ? white : caption,card.w-32);
    }
    const std::string pageStatus = impl->pending ? "Opening selected title..." : std::format("{} titles   /   Page {} of {}",titles.size(),impl->page+1,pages);
    text(pageStatus,left,top+36,16,pink,contentW);
    text(pageStatus,left,top+36,16,{255,255,255,255},contentW,depthCanvas.get());
    const bool colorUploaded = Core::uploadTexture(context, canvas.get(), &impl->texture, "Disc title browser") == 0;
    const bool depthUploaded = Core::uploadTexture(context, depthCanvas.get(), &impl->depthTexture, "Disc title browser depth") == 0;
    if (colorUploaded && depthUploaded)
        impl->visible = true;
    else
        impl->dirty = true;
}
