// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "GettingStarted.h"
#include "Core.h"
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace GettingStarted {
namespace {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using Font = std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)>;
Surface canvas{nullptr, SDL_DestroySurface};
Surface page{nullptr, SDL_DestroySurface};
Surface packed{nullptr, SDL_DestroySurface};
Surface depthPage{nullptr, SDL_DestroySurface};
Surface depthCanvas{nullptr, SDL_DestroySurface};
constexpr SDL_Color depthBackground{192, 192, 192, 255};
constexpr SDL_Color depthWhite{255, 255, 255, 255};
Layout currentLayout = Layout::Single;
int outputWidth = 0;
float currentScale = 1.0f;
int scroll = 0, scrollLimit = 0, focus = 1, pressed = -1, hover = -1;
bool dirty = true;
std::uint64_t version = 0;
SDL_Rect checkbox{}, button{};
bool currentLight = false;
SDL_Color background{20, 20, 20, 255};
SDL_Color foreground{245, 245, 245, 255};
SDL_Color muted{190, 190, 190, 255};
constexpr SDL_Color accent{255, 23, 111, 255};

// Fill an opaque rectangle using the destination's pixel format.
void fill(SDL_Surface* target, SDL_Rect rect, SDL_Color color) {
    SDL_FillSurfaceRect(target, &rect, SDL_MapSurfaceRGBA(target, color.r, color.g, color.b, color.a));
}

// Rasterize a label and retain its surface until it has been composited.
Surface text(TTF_Font* font, const std::string& value, SDL_Color color) {
    return Surface(TTF_RenderText_Blended(font, value.c_str(), value.size(), color), SDL_DestroySurface);
}

// Place a text surface on the opaque page so antialiased edges blend only once.
void blit(SDL_Surface* target, SDL_Surface* source, int x, int y) {
    if (!source) return;
    const SDL_Rect destination{x, y, source->w, source->h};
    SDL_BlitSurface(source, nullptr, target, &destination);
}

struct Run {
    std::string words;
    IconType icon = IconType::None;
    Run(const char* value) : words(value) {}
    Run(IconType value) : icon(value) {}
};

// Wrap words and real atlas icons together, keeping icons aligned with the text's line height.
int paragraph(SDL_Surface* target, SDL_Surface* atlas, TTF_Font* font,
              int left, int top, int width, const std::vector<Run>& runs, float scale, SDL_Color ink) {
    const int lineHeight = TTF_GetFontHeight(font) + static_cast<int>(6 * scale);
    const int iconSize = lineHeight - static_cast<int>(2 * scale);
    int space = 0, unused = 0;
    TTF_GetStringSize(font, " ", 1, &space, &unused);
    int x = left, y = top;
    for (const auto& run : runs) {
        if (run.icon != IconType::None) {
            if (x != left && x + iconSize > left + width) { x = left; y += lineHeight; }
            const int tile = static_cast<int>(run.icon);
            // Trim the atlas gutter so the visible glyph is comparable to the surrounding letters.
            const int tileWidth = atlas->w / 8, tileHeight = atlas->h / 8;
            const SDL_Rect source{tile % 8 * tileWidth + tileWidth / 12,
                tile / 8 * tileHeight + tileHeight / 12, tileWidth - tileWidth / 6, tileHeight - tileHeight / 6};
            const SDL_Rect destination{x, y, iconSize, iconSize};
            if (target) SDL_BlitSurfaceScaled(atlas, &source, target, &destination, SDL_SCALEMODE_LINEAR);
            x += iconSize + space;
        } else {
            std::istringstream words(run.words);
            std::string word;
            while (words >> word) {
                auto pixels = text(font, word, ink);
                if (!pixels) return -1;
                if (x != left && x + pixels->w > left + width) { x = left; y += lineHeight; }
                if (target) blit(target, pixels.get(), x, y);
                x += pixels->w + space;
            }
        }
    }
    return y + lineHeight;
}

// Build the short guide at the current width, measuring first so small windows can scroll all content.
bool buildPage(int width, float scale, const std::filesystem::path& assets,
               Surface& page, SDL_Color background, SDL_Color foreground, SDL_Color muted, bool depth = false) {
    Font body(TTF_OpenFont((assets / "Lato.ttf").string().c_str(), 18 * scale), TTF_CloseFont);
    Font heading(TTF_OpenFont((assets / "Lato.ttf").string().c_str(), 19 * scale), TTF_CloseFont);
    Font title(TTF_OpenFont((assets / "Lato.ttf").string().c_str(), 28 * scale), TTF_CloseFont);
    Surface atlas(IMG_Load((assets / "AppIcons.png").string().c_str()), SDL_DestroySurface);
    if (!body || !heading || !title || !atlas) return false;
    if (depth) {
        // Depth uses icon coverage only; discard shaded RGB edges from the color atlas.
        atlas.reset(SDL_ConvertSurface(atlas.get(), SDL_PIXELFORMAT_RGBA32));
        if (!atlas) return false;
        for (int y = 0; y < atlas->h; ++y) {
            auto* row = static_cast<Uint8*>(atlas->pixels) + y * atlas->pitch;
            for (int x = 0; x < atlas->w; ++x) row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
        }
    }
    SDL_SetSurfaceColorMod(atlas.get(), foreground.r, foreground.g, foreground.b);
    TTF_SetFontStyle(heading.get(), TTF_STYLE_BOLD);
    TTF_SetFontStyle(title.get(), TTF_STYLE_BOLD);
    const int padding = static_cast<int>(28 * scale);
    const int contentWidth = std::min(width - padding * 2, static_cast<int>(700 * scale));
    if (contentWidth <= 0) return false;
    const int left = (width - contentWidth) / 2;
    const std::vector<std::pair<const char*, std::vector<Run>>> sections{
        {"1. Open your media", {"Click", IconType::File, "at the top-left to open a photo or video. You can also drag a file into the window."}},
        {"2. Bring it into 3D", {"Click", IconType::Stereo_3D, "at the bottom-center to switch to 3D. The first conversion may need to download a model. Video conversion requires Pro."}},
        {"3. Make the depth feel right", {"Open", IconType::Settings, "near the lower-left. Adjust", IconType::Glasses, "Separation,", IconType::Focus, "Depth, and", IconType::Layers, "Parallax with the sliders. Start with small changes."}},
        {"4. Choose your view and save", {"Use", IconType::Options, "at the lower-left for the 3D mode, quality, and other settings. Use", IconType::Fullscreen, "for fullscreen and", IconType::Save, "to save the current image or video frame."}}
    };
    Surface titlePixels(TTF_RenderText_Blended_Wrapped(title.get(), "Getting started with Rendepth", 0, foreground, contentWidth), SDL_DestroySurface);
    auto subtitle = text(body.get(), "A little depth. A whole new view.", muted);
    if (!titlePixels || !subtitle) return false;
    const int logoSize = static_cast<int>(64 * scale);
    const int titleY = padding + logoSize + static_cast<int>(16 * scale);
    const int subtitleY = titleY + titlePixels->h + static_cast<int>(8 * scale);
    const int bodyY = subtitleY + subtitle->h + static_cast<int>(28 * scale);
    // The same layout pass measures and paints, so wrapping and scroll bounds cannot disagree.
    auto layout = [&](SDL_Surface* target) {
        int y = bodyY;
        for (const auto& [label, runs] : sections) {
            auto labelPixels = text(heading.get(), label, foreground);
            if (!labelPixels) return -1;
            if (target) blit(target, labelPixels.get(), left, y);
            y += labelPixels->h + static_cast<int>(7 * scale);
            y = paragraph(target, atlas.get(), body.get(), left, y, contentWidth, runs, scale, muted);
            if (y < 0) return -1;
            y += static_cast<int>(20 * scale);
        }
        y = paragraph(target, atlas.get(), body.get(), left, y, contentWidth,
            {"Move the mouse to reveal controls. Press F1 to toggle this guide."}, scale, muted);
        return y < 0 ? -1 : y + padding;
    };
    const int pageHeight = layout(nullptr);
    if (pageHeight <= 0) return false;
    page.reset(SDL_CreateSurface(width, pageHeight, SDL_PIXELFORMAT_RGBA32));
    if (!page) return false;
    fill(page.get(), {0, 0, width, pageHeight}, background);
    const int logo = static_cast<int>(IconType::Logo_White);
    const SDL_Rect source{logo % 8 * (atlas->w / 8), logo / 8 * (atlas->h / 8), atlas->w / 8, atlas->h / 8};
    const SDL_Rect destination{(width - logoSize) / 2, padding, logoSize, logoSize};
    SDL_BlitSurfaceScaled(atlas.get(), &source, page.get(), &destination, SDL_SCALEMODE_LINEAR);
    blit(page.get(), titlePixels.get(), (width - titlePixels->w) / 2, titleY);
    blit(page.get(), subtitle.get(), (width - subtitle->w) / 2, subtitleY);
    return layout(page.get()) > 0;
}

// Resolve footer hit targets in the same pixel coordinates used by the rendered page.
int hit(float x, float y) {
    const SDL_Point point{static_cast<int>(x), static_cast<int>(y)};
    if (SDL_PointInRect(&point, &checkbox)) return 0;
    if (SDL_PointInRect(&point, &button)) return 1;
    return -1;
}
}

// Open the guide at its beginning with the dismissal button ready for keyboard activation.
void show() {
    visible = true; scroll = 0; focus = 1; pressed = hover = -1; dirty = true;
}

// Consume guide input without letting clicks or scroll gestures reach the media underneath.
Action handleEvent(const SDL_Event& event, SDL_Window* window) {
    int activate = -1;
    if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        int logicalWidth = 0, logicalHeight = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
        const float y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
        float pageX = x * pixelWidth / std::max(1, logicalWidth);
        if (currentLayout != Layout::Single && canvas && outputWidth > 1) {
            const int leftWidth = outputWidth / 2;
            const bool right = pageX >= leftWidth;
            pageX = (pageX - (right ? leftWidth : 0)) * canvas->w /
                (right ? outputWidth - leftWidth : leftWidth);
        }
        int target = hit(pageX, y * pixelHeight / std::max(1, logicalHeight));
        if (hover != target) { hover = target; dirty = true; }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            pressed = target;
            if (target >= 0) { focus = target; dirty = true; }
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            if (pressed == target) activate = target;
            pressed = -1;
        }
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        scroll = std::clamp(scroll - static_cast<int>(event.wheel.y * direction * 54 * currentScale), 0, scrollLimit);
        dirty = true;
    } else if (event.type == SDL_EVENT_KEY_DOWN) {
        const auto key = event.key.key;
        if (key == SDLK_TAB && !event.key.repeat) { focus = 1 - focus; dirty = true; }
        if ((key == SDLK_SPACE || key == SDLK_RETURN || key == SDLK_KP_ENTER) && !event.key.repeat) activate = focus;
        if ((key == SDLK_ESCAPE || key == SDLK_F1) && !event.key.repeat) activate = 1;
        const int step = static_cast<int>(48 * currentScale);
        if (key == SDLK_DOWN || key == SDLK_PAGEDOWN) scroll += key == SDLK_DOWN ? step : step * 6;
        if (key == SDLK_UP || key == SDLK_PAGEUP) scroll -= key == SDLK_UP ? step : step * 6;
        if (key == SDLK_HOME) scroll = 0;
        if (key == SDLK_END) scroll = scrollLimit;
        scroll = std::clamp(scroll, 0, scrollLimit); dirty = true;
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) pressed = -1;
    if (activate == 0) { dontShowAgain = !dontShowAgain; dirty = true; return Action::PreferenceChanged; }
    if (activate == 1) { visible = false; return Action::Dismiss; }
    return Action::None;
}

// Cache an opaque welcome page with a scrolling body and a fixed, keyboard-accessible footer.
SDL_Surface* render(int width, int height, float scale, const std::filesystem::path& assets, bool light, Layout layout) {
    if (width <= 0 || height <= 0) return nullptr;
    if (currentLayout != layout || outputWidth != width) dirty = true;
    currentLayout = layout;
    outputWidth = width;
    // Full SBS lays out each eye at its native width; half SBS and RGBD pack a full-width page.
    if (layout == Layout::SBSFull) width = std::max(1, width / 2);
    scale = std::clamp(scale, 0.75f, 3.0f);
    // Keep controls readable and within narrow windows even on a high-density display.
    scale = std::min(scale, width / 480.0f);
    if (width <= 0 || height <= 0) return nullptr;
    if (!canvas || canvas->w != width || canvas->h != height || currentScale != scale || currentLight != light) {
        currentLight = light;
        background = light ? SDL_Color{238, 238, 238, 255} : SDL_Color{20, 20, 20, 255};
        foreground = light ? SDL_Color{48, 48, 48, 255} : SDL_Color{245, 245, 245, 255};
        muted = light ? SDL_Color{80, 80, 80, 255} : SDL_Color{190, 190, 190, 255};
        canvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        depthPage.reset(); depthCanvas.reset();
        if (!canvas || !buildPage(width, scale, assets, page, background, foreground, muted)) {
            canvas.reset(); page.reset();
            return nullptr;
        }
        currentScale = scale; dirty = true;
    }
    if (!dirty) return layout == Layout::Single ? canvas.get() : packed.get();
    const int padding = static_cast<int>(28 * scale);
    const int footerHeight = static_cast<int>(86 * scale);
    const int viewport = std::max(1, height - footerHeight);
    scrollLimit = std::max(0, page->h - viewport);
    scroll = std::clamp(scroll, 0, scrollLimit);
    // Paint color and depth with identical geometry so text, icons, and controls stay aligned.
    for (int view = 0; view < (layout == Layout::RGBD ? 2 : 1); ++view) {
        const bool depth = view == 1;
        if (depth && !depthPage) {
            if (!buildPage(width, scale, assets, depthPage, depthBackground, depthWhite, depthWhite, true)) return nullptr;
        }
        if (depth && !depthCanvas) depthCanvas.reset(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));
        if (depth && !depthCanvas) return nullptr;
        auto& targetCanvas = depth ? depthCanvas : canvas;
        auto& targetPage = depth ? depthPage : page;
        const auto paper = depth ? depthBackground : background;
        const auto headingInk = depth ? depthWhite : foreground;
        const auto ink = depth ? depthWhite : muted;
        const auto highlight = depth ? depthWhite : accent;
        fill(targetCanvas.get(), {0, 0, width, height}, paper);
        const SDL_Rect source{0, scroll, width, std::min(viewport, page->h)};
        // Center the content in the space above the fixed footer; overflowing pages start at the top.
        const int contentTop = std::max(0, (viewport - page->h) / 2);
        const SDL_Rect destination{0, contentTop, width, source.h};
        SDL_BlitSurface(targetPage.get(), &source, targetCanvas.get(), &destination);
        if (scrollLimit > 0) {
            const int track = viewport - padding * 2;
            const int thumb = std::max(static_cast<int>(24 * scale), track * viewport / page->h);
            fill(targetCanvas.get(), {width - static_cast<int>(9 * scale), padding + (track - thumb) * scroll / scrollLimit,
                std::max(2, static_cast<int>(3 * scale)), thumb}, ink);
        }
        fill(targetCanvas.get(), {padding, viewport, width - padding * 2, 1}, depth ? depthWhite : light ? SDL_Color{200, 200, 200, 255} : SDL_Color{56, 56, 56, 255});
        Font font(TTF_OpenFont((assets / "Lato.ttf").string().c_str(), 17 * scale), TTF_CloseFont);
        if (!font) return nullptr;
        auto label = text(font.get(), "Don't show again", headingInk);
        auto done = text(font.get(), "Got it", {255, 255, 255, 255});
        if (!label || !done) return nullptr;
        const int size = static_cast<int>(22 * scale);
        const int centerY = viewport + footerHeight / 2;
        checkbox = {padding, centerY - size, label->w + size + static_cast<int>(18 * scale), size * 2};
        button = {width - padding - static_cast<int>(112 * scale), centerY - size,
            static_cast<int>(112 * scale), size * 2};
        const SDL_Color outline = depth ? depthWhite : focus == 0 || hover == 0 ? accent : muted;
        fill(targetCanvas.get(), {padding, centerY - size / 2, size, size}, outline);
        fill(targetCanvas.get(), {padding + 2, centerY - size / 2 + 2, size - 4, size - 4}, paper);
        if (dontShowAgain) {
            // Draw a check mark without depending on the font containing a checkbox glyph.
            for (int i = 0; i < size / 3; ++i)
                fill(targetCanvas.get(), {padding + size / 5 + i, centerY + i - size / 8, 2, 2}, highlight);
            for (int i = 0; i < size / 2; ++i)
                fill(targetCanvas.get(), {padding + size / 2 + i, centerY + size / 5 - i, 2, 2}, highlight);
        }
        blit(targetCanvas.get(), label.get(), padding + size + static_cast<int>(12 * scale), centerY - label->h / 2);
        fill(targetCanvas.get(), button, depth ? depthWhite : accent);
        blit(targetCanvas.get(), done.get(), button.x + (button.w - done->w) / 2, centerY - done->h / 2);
    }
    if (layout != Layout::Single) {
        if (!packed || packed->w != outputWidth || packed->h != height)
            packed.reset(SDL_CreateSurface(outputWidth, height, SDL_PIXELFORMAT_RGBA32));
        if (!packed) return nullptr;
        const int leftWidth = outputWidth / 2;
        const SDL_Rect left{0, 0, leftWidth, height};
        const SDL_Rect right{leftWidth, 0, outputWidth - leftWidth, height};
        SDL_BlitSurfaceScaled(canvas.get(), nullptr, packed.get(), &left, SDL_SCALEMODE_LINEAR);
        if (layout == Layout::RGBD) {
            SDL_BlitSurfaceScaled(depthCanvas.get(), nullptr, packed.get(), &right, SDL_SCALEMODE_LINEAR);
        } else {
            SDL_BlitSurfaceScaled(canvas.get(), nullptr, packed.get(), &right, SDL_SCALEMODE_LINEAR);
        }
    }
    dirty = false; ++version;
    return layout == Layout::Single ? canvas.get() : packed.get();
}

// Let the GPU cache detect changes without uploading the page every frame.
std::uint64_t revision() { return version; }

// Release CPU page resources before SDL shuts down.
void release() { canvas.reset(); page.reset(); packed.reset(); depthPage.reset(); depthCanvas.reset(); dirty = true; }
}
