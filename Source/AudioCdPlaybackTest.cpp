// Exercise the real reader, TOC detection, FFmpeg and player with a simulated
// Linux CD device. Only open/close/ioctl are replaced; playback is unmodified.
#include "AudioCdReader.h"
#include "DiscSource.h"
#include "VideoPlayer.h"
#include "Core.h"
#include <linux/cdrom.h>
#include <fcntl.h>
#include <cstdarg>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <set>
#include <unistd.h>

SDL_Surface* Core::orientSurface(SDL_Surface* surface, const std::string&) { return surface; }
static std::mutex deviceMutex;
static std::set<int> devices;
static bool simulateRemoval = false;
extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_close(int);
extern "C" int __real_ioctl(int, unsigned long, ...);
extern "C" int __wrap_open(const char* path, int flags, ...) {
    if (std::strcmp(path, "/dev/sr999") == 0) {
        const int fd = __real_open("/dev/null", O_RDONLY);
        std::lock_guard lock(deviceMutex); devices.insert(fd); return fd;
    }
    mode_t mode = 0;
    if (flags & O_CREAT) { va_list args; va_start(args, flags); mode = va_arg(args, int); va_end(args); }
    return __real_open(path, flags, mode);
}
extern "C" int __wrap_close(int fd) {
    { std::lock_guard lock(deviceMutex); devices.erase(fd); }
    return __real_close(fd);
}
extern "C" int __wrap_ioctl(int fd, unsigned long op, ...) {
    va_list args; va_start(args, op); void* data = va_arg(args, void*); va_end(args);
    std::lock_guard lock(deviceMutex);
    if (!devices.contains(fd)) return __real_ioctl(fd, op, data);
    if (simulateRemoval) { errno = EIO; return -1; }
    if (op == CDROMREADTOCHDR) {
        auto* h = static_cast<cdrom_tochdr*>(data); h->cdth_trk0 = 1; h->cdth_trk1 = 3; return 0;
    }
    if (op == CDROMREADTOCENTRY) {
        auto* e = static_cast<cdrom_tocentry*>(data);
        e->cdte_ctrl = e->cdte_track == 2 ? CDROM_DATA_TRACK : 0;
        e->cdte_addr.lba = e->cdte_track == CDROM_LEADOUT ? 2250 : (e->cdte_track - 1) * 750;
        return 0;
    }
    if (op == CDROMREADAUDIO) {
        auto* r = static_cast<cdrom_read_audio*>(data);
        assert(r->nframes > 0 && r->nframes <= 16);
        assert((r->addr.lba < 750 && r->addr.lba + r->nframes <= 750) ||
            (r->addr.lba >= 1500 && r->addr.lba + r->nframes <= 2250));
        // Silent first track, audible second track (including opposite stereo phases).
        for (int i = 0; i < r->nframes * 2352; i += 2) {
            const int16_t sample = r->addr.lba < 750 ? 0 : ((i / 2) % 2 ? -8192 : 8192);
            r->buf[i] = static_cast<uint8_t>(sample);
            r->buf[i + 1] = static_cast<uint8_t>(static_cast<uint16_t>(sample) >> 8);
        }
        return 0;
    }
    errno = EINVAL; return -1;
}
static bool waitAudio(VideoPlayer& player, double target = 0.0) {
    const auto end = SDL_GetTicks() + 4000;
    do {
        player.update();
        assert(player.takeError().empty());
        const double position = player.audioPlaybackPosition();
        if (player.bufferedAudioDuration() > 0.05 && position >= target && position < target + 1.0) return true;
        SDL_Delay(5);
    } while (SDL_GetTicks() < end);
    return false;
}
int main() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    assert(SDL_Init(SDL_INIT_AUDIO));
    assert(DiscSource::detect("cdda://sr999/") == DiscSource::Type::AudioCd);
    VideoPlayer player; std::string error;
    assert(player.open("cdda://sr999/", error));
    assert(player.audioCd() && player.discSource() && player.audioOnly() && player.hasAudio());
    assert(player.duration() == 20 && player.chapterCount() == 2 && player.chapterTime(1) == 10);
    assert(player.chapterAtTime(9.8) == 0 && player.chapterAtTime(10.0) == 1);
    assert(waitAudio(player));
    assert(player.audioWaveform() == AudioWaveform::Bars{});
    // A real timeline drag leaves playback paused while the user moves the thumb.
    SDL_Delay(100);
    player.setPlaying(false);
    SDL_Delay(100);
    player.seek(10.0);
    player.setPlaying(true);
    assert(waitAudio(player, 10.0));
    const auto waveformDeadline = SDL_GetTicks() + 1000;
    bool audibleWaveform = false;
    do {
        player.update();
        const auto bars = player.audioWaveform();
        audibleWaveform = std::any_of(bars.begin(), bars.end(), [](float value) { return value > 0.1f; });
        if (audibleWaveform) break;
        SDL_Delay(5);
    } while (SDL_GetTicks() < waveformDeadline);
    assert(audibleWaveform);
    // Pause/resume without a seek must also keep the decoder alive beyond
    // the three seconds of samples already queued before pausing.
    player.setPlaying(false);
    assert(player.audioWaveform() == AudioWaveform::Bars{});
    SDL_Delay(100);
    player.setPlaying(true);
    assert(waitAudio(player, 13.5));
    for (double target : {10.0, 2.0, 18.0, 0.0}) {
        player.setPlaying(false);
        auto generation = player.generation();
        for (int i = 0; i < 500; ++i) player.seek(i % 20, true);
        assert(player.generation() == generation && !player.playing());
        player.seek(target); assert(player.generation() == generation + 1);
        assert(!player.playing());
        player.setPlaying(true); assert(waitAudio(player, target));
    }
    for (int i = 0; i < 20; ++i) {
        const int chapter = i % 2;
        player.seekChapter(chapter);
        assert(waitAudio(player, chapter * 10.0));
        if (chapter == 0) assert(player.audioWaveform() == AudioWaveform::Bars{});
    }
    // Closing must wake a decoder parked in a paused, full audio queue.
    player.setPlaying(false);
    SDL_Delay(100);
    player.close();
    assert(player.audioWaveform() == AudioWaveform::Bars{});
    assert(!player.audioCd() && !player.discSource() && player.chapterCount() == 0);
    assert(devices.empty());
    simulateRemoval = true;
    assert(!player.open("cdda://sr999/", error) && !error.empty());
    assert(devices.empty());
    SDL_Quit();
    std::puts("Audio CD player: detection, decode, pause/resume, 20 track skips, 2000 drag previews, release seeks, paused close and removal passed");
}
