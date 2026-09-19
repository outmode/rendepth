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
};
struct Scan {
    std::mutex mutex;
    std::atomic<bool> cancel{false}, done{false};
    std::vector<Title> titles;
    std::string name, error;
    unsigned revision = 0;
};
std::string durationText(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    return total >= 3600 ? std::format("{}:{:02}:{:02}", total / 3600, total / 60 % 60, total % 60)
        : std::format("{}:{:02}", total / 60, total % 60);
}
#ifdef RENDEPTH_ENABLE_FFMPEG
struct PreviewInput {
    AVFormatContext* format = nullptr;
    AVIOContext* io = nullptr;
    AVCodecContext* codec = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
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
    bool stopped() const { return scan->cancel || std::chrono::steady_clock::now() > end; }
};
template<class Reader>
Surface thumbnail(Reader& reader, const Title& title, Scan& scan, const char* demuxer) {
    std::string error;
    if (!reader.selectTitle(title.id, error) || scan.cancel) return {};
    PreviewDeadline deadline{&scan, std::chrono::steady_clock::now() + std::chrono::seconds(8)};
    // libbluray seeks to access units, not arbitrary bytes. Seek using the
    // disc's time map, then let FFmpeg inspect a forward-only preview stream.
    reader.seekTime(title.duration * 0.2);
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
    input.codec = avcodec_alloc_context3(decoder);
    if (!input.codec || avcodec_parameters_to_context(input.codec, input.format->streams[stream]->codecpar) < 0) return {};
    input.codec->thread_count = 2;
    if (avcodec_open2(input.codec, decoder, nullptr) < 0) return {};
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

template<class Reader>
void scanDisc(Reader& reader, const std::filesystem::path& path, const std::shared_ptr<Scan>& scan, const char* demuxer) {
    std::string error;
    if (!reader.open(path, error)) {
        std::lock_guard lock(scan->mutex);
        scan->error = error;
        ++scan->revision;
        return;
    }
    std::vector<Title> titles;
    for (size_t i = 0; i < reader.titles().size(); ++i) {
        const auto& t = reader.titles()[i];
        int id;
        if constexpr (std::is_same_v<Reader, BlurayReader>) id = t.index;
        else id = static_cast<int>(i);
        int audioTracks = 0;
        if constexpr (std::is_same_v<Reader, BlurayReader>) audioTracks = t.audioTrackCount;
        titles.push_back({id, std::format("Title {}", id + 1), t.duration, t.chapterCount, audioTracks});
    }
    std::stable_sort(titles.begin(), titles.end(), [](const auto& a, const auto& b) { return a.duration > b.duration; });
    {
        std::lock_guard lock(scan->mutex);
        scan->name = reader.discTitle();
        scan->titles = titles;
        ++scan->revision;
    }
    for (size_t i = 0; i < titles.size() && !scan->cancel; ++i) {
        Surface preview;
#ifdef RENDEPTH_ENABLE_FFMPEG
        preview = thumbnail(reader, titles[i], *scan, demuxer);
#endif
        if (scan->cancel) break;
        std::lock_guard lock(scan->mutex);
        scan->titles[i].preview = std::move(preview);
        scan->titles[i].previewComplete = true;
        ++scan->revision;
    }
}
}

struct DiscTitleMenu::Impl {
    struct Worker { std::shared_ptr<Scan> scan; std::thread thread; };
    std::vector<Worker> workers;
    std::shared_ptr<Scan> scan;
    SDL_GPUTexture* texture = nullptr;
    TTF_Font* fallbackFont = nullptr;
    bool checkedFallbackFont = false;
    bool visible = false, dirty = true;
    int width = 0, height = 0, page = 0, focus = 0, columns = 3, perPage = 6;
    int hovered = -1;
    unsigned revision = 0;
    std::optional<int> pending;
    std::vector<SDL_Rect> cards;
    SDL_Rect back{}, previous{}, next{};
    void stop() { if (scan) scan->cancel = true; }
    void reap() {
        for (auto it = workers.begin(); it != workers.end();) {
            if (it->scan->done) { it->thread.join(); it = workers.erase(it); }
            else ++it;
        }
    }
};
DiscTitleMenu::DiscTitleMenu() : impl(std::make_unique<Impl>()) {}
DiscTitleMenu::~DiscTitleMenu() {
    for (auto& worker : impl->workers) worker.scan->cancel = true;
    for (auto& worker : impl->workers) worker.thread.join();
}
void DiscTitleMenu::open(const std::filesystem::path& path) {
    close();
    impl->reap();
    impl->scan = std::make_shared<Scan>();
    impl->visible = true; impl->dirty = true; impl->page = 0; impl->focus = 0; impl->hovered = -1;
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
void DiscTitleMenu::close() { impl->stop(); impl->visible = false; impl->pending.reset(); }
void DiscTitleMenu::shutdown(Context* context) {
    close();
    for (auto& worker : impl->workers) worker.scan->cancel = true;
    for (auto& worker : impl->workers) worker.thread.join();
    impl->workers.clear(); impl->scan.reset();
    if (impl->texture) SDL_ReleaseGPUTexture(context->device, impl->texture);
    impl->texture = nullptr;
    if (impl->fallbackFont) TTF_CloseFont(impl->fallbackFont);
    impl->fallbackFont = nullptr;
}
bool DiscTitleMenu::visible() const { return impl->visible; }
SDL_GPUTexture* DiscTitleMenu::texture() const { return impl->visible ? impl->texture : nullptr; }
std::optional<int> DiscTitleMenu::takeSelection() {
    if (!impl->pending || !impl->scan->done) return {};
    auto result = impl->pending;
    close();
    return result;
}

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
    auto page = [&](int delta) {
        impl->page = std::clamp(impl->page + delta, 0, std::max(0, (count - 1) / impl->perPage));
        impl->focus = impl->page * impl->perPage;
        impl->hovered = -1; impl->dirty = true;
    };
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_ESCAPE) close();
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) choose(impl->focus);
        else if (e.key.key == SDLK_PAGEDOWN) page(1);
        else if (e.key.key == SDLK_PAGEUP) page(-1);
        else {
            int delta = e.key.key == SDLK_RIGHT ? 1 : e.key.key == SDLK_LEFT ? -1 :
                e.key.key == SDLK_DOWN ? impl->columns : e.key.key == SDLK_UP ? -impl->columns : 0;
            if (delta) {
                impl->hovered = -1;
                impl->focus = std::clamp(impl->focus + delta, 0, std::max(0, count - 1));
                impl->page = impl->focus / impl->perPage; impl->dirty = true;
            }
        }
    } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        float dy = e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        if (dy != 0) page(dy < 0 ? 1 : -1);
    } else if (pointer) {
        int w, h; SDL_GetWindowSize(window, &w, &h);
        float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
        float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
        SDL_Point p{static_cast<int>(x * impl->width / std::max(1, w)), static_cast<int>(y * impl->height / std::max(1, h))};
        int hover = -1;
        for (size_t i = 0; i < impl->cards.size(); ++i) if (SDL_PointInRect(&p, &impl->cards[i])) hover = static_cast<int>(i);
        if (impl->hovered != hover) { impl->hovered = hover; impl->dirty = true; }
        if (hover >= 0) impl->focus = impl->page * impl->perPage + hover;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (SDL_PointInRect(&p, &impl->back)) close();
            else if (SDL_PointInRect(&p, &impl->previous)) page(-1);
            else if (SDL_PointInRect(&p, &impl->next)) page(1);
            else if (hover >= 0) choose(impl->page * impl->perPage + hover);
        }
    }
    return true;
}

void DiscTitleMenu::update(Context* context, TTF_Font* font) {
    impl->reap();
    if (!visible() || !font) return;
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
    // Compose at logical window resolution; the GPU scales this for high DPI.
    int w, h; SDL_GetWindowSize(context->window, &w, &h);
    w = std::max(1, w); h = std::max(1, h);
    std::vector<Title> titles;
    std::string name, error;
    {
        std::lock_guard lock(impl->scan->mutex);
        if (!impl->dirty && impl->revision == impl->scan->revision && impl->width == w && impl->height == h) return;
        impl->revision = impl->scan->revision;
        titles = impl->scan->titles; name = impl->scan->name; error = impl->scan->error;
    }
    impl->width = w; impl->height = h; impl->dirty = false;
    Surface canvas(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!canvas) return;
    Style style;
    auto theme = [&](Style::Color color) { auto c = style.getColor(color, Style::Alpha::Solid); return SDL_Color{Uint8(c.r*255), Uint8(c.g*255), Uint8(c.b*255), 255}; };
    const auto pink = theme(Style::Color::Pink), white = theme(Style::Color::White), gray = theme(Style::Color::Gray);
    auto fill = [&](SDL_Rect rect, SDL_Color color) { SDL_FillSurfaceRect(canvas.get(), &rect, SDL_MapSurfaceRGBA(canvas.get(), color.r, color.g, color.b, color.a)); };
    fill({0,0,w,h}, {12,18,27,255});
    auto text = [&](const std::string& value, int x, int y, int size, SDL_Color color, int maxWidth) {
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
        SDL_Rect dest{x,y,source.w,source.h}; SDL_BlitSurface(rendered.get(), &source, canvas.get(), &dest);
    };
    const int margin = std::clamp(w / 24, 16, 56), gap = 18;
    const int contentW = std::min(1200, w - margin * 2), left = (w - contentW) / 2;
    text("CHOOSE A TITLE", left, 36, 16, pink, contentW - 120);
    text(name.empty() ? "Blu-ray / DVD" : name, left, 65, 28, white, contentW - 120);
    text("Choose what to watch. Playback starts when you select a title.", left, 106, 16, gray, contentW);
    impl->back = {w-margin-100,36,100,38};
    fill(impl->back, theme(Style::Color::Blue)); text("Cancel", impl->back.x+20,44,16,white,80);
    impl->columns = w >= 1120 ? 3 : w >= 680 ? 2 : 1;
    const int cardW = (contentW - gap*(impl->columns-1)) / impl->columns;
    const int rows = h >= 860 ? 2 : 1;
    const int imageH = std::min(cardW*9/16, std::max(72, (h-235-gap*(rows-1))/rows-87));
    const int cardH = imageH + 87;
    impl->perPage = rows * impl->columns;
    const int pages = std::max(1, (static_cast<int>(titles.size()) + impl->perPage-1) / impl->perPage);
    impl->page = std::clamp(impl->focus / impl->perPage, 0, pages-1);
    impl->cards.clear();
    for (int n = 0; n < impl->perPage; ++n) {
        int index = impl->page * impl->perPage + n;
        if (index >= static_cast<int>(titles.size())) break;
        const auto& title = titles[index];
        SDL_Rect card{left+(n%impl->columns)*(cardW+gap), 151+(n/impl->columns)*(cardH+gap),cardW,cardH};
        impl->cards.push_back(card);
        bool selected = impl->hovered == n || (impl->hovered < 0 && impl->focus == index);
        fill({card.x-2,card.y-2,card.w+4,card.h+4}, selected ? pink : SDL_Color{38,49,64,255});
        fill(card, theme(Style::Color::Blue));
        SDL_Rect picture{card.x,card.y,card.w,imageH};
        fill(picture, {6,10,16,255});
        if (title.preview) {
            float scale = std::min(float(picture.w)/title.preview->w,float(picture.h)/title.preview->h);
            SDL_Rect dest{0,0,int(title.preview->w*scale),int(title.preview->h*scale)};
            dest.x=picture.x+(picture.w-dest.w)/2; dest.y=picture.y+(picture.h-dest.h)/2;
            SDL_BlitSurfaceScaled(title.preview.get(), nullptr, canvas.get(), &dest, SDL_SCALEMODE_LINEAR);
        } else text(title.previewComplete ? "Preview unavailable" : "Loading preview...", card.x+18,card.y+imageH/2-10,16,gray,card.w-36);
        text(title.label,card.x+16,card.y+imageH+9,20,white,card.w-32);
        text(std::format("{}   /   {} {}",durationText(title.duration),title.chapters,title.chapters == 1 ? "chapter" : "chapters"),card.x+16,card.y+imageH+36,15,gray,card.w-32);
        if (title.audioTracks > 0)
            text(std::format("{} audio {}", title.audioTracks, title.audioTracks == 1 ? "track" : "tracks"),card.x+16,card.y+imageH+59,15,gray,card.w-32);
    }
    if (titles.empty()) {
        text(error.empty() ? "Reading disc titles..." : "Could not open disc",left,170,24,white,contentW);
        if (!error.empty()) text(error,left,212,16,gray,contentW);
    }
    impl->previous = {left,h-55,100,35}; impl->next = {left+112,h-55,100,35};
    if (pages > 1) {
        fill(impl->previous,theme(Style::Color::Blue)); fill(impl->next,theme(Style::Color::Blue));
        text("Previous",left+12,h-49,16,impl->page > 0 ? white : gray,90);
        text("Next",left+137,h-49,16,impl->page+1 < pages ? white : gray,70);
    }
    std::string footer = impl->pending ? "Opening selected title..." : std::format("{} titles   /   Page {} of {}",titles.size(),impl->page+1,pages);
    text(footer,left+(pages > 1 ? 240 : 0),h-49,16,gray,contentW-(pages > 1 ? 240 : 0));
    Core::uploadTexture(context, canvas.get(), &impl->texture, "Disc title browser");
}
