// Copyright (c) 2026 Outmode
// SPDX-License-Identifier: MIT
#include "MvcDecoder.h"
#include "BlurayReader.h"
#include "BlurayPlaylist.h"
#include "MvcPacketAssembler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#ifdef RENDEPTH_ENABLE_MVC
#include <edge264.h>
extern "C" {
#include <libbluray/filesystem.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

struct MvcDecoder::Impl {
    struct Clip { BlurayPlaylist::MvcClip dependent; int64_t byteStart, byteEnd, timestampOffset; };
    BLURAY* disc = nullptr;
    BD_FILE_H* file = nullptr;
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    Edge264Decoder* decoder = nullptr;
    std::vector<Clip> clips;
    int currentClip = -1;
    bool baseRight = false, needSeek = true;
    int64_t position = 0, fileSize = 0, blockStart = -1;
    int blockSize = 0;
    // libbluray decrypts exactly one aligned 6144-byte unit per read.
    std::array<uint8_t, 6144> block{};
    std::map<int64_t, std::vector<uint8_t>> dependentPackets;
    MvcPacketAssembler dependentPicture;
    // Only the reader thread touches the dependent demuxer during playback.
    // Keep compressed data ahead of the decoder without borrowing edge264
    // pictures across threads. Both packet count and bytes are bounded.
    static constexpr size_t maxPendingPackets = 128;
    static constexpr size_t maxPendingBytes = 4 * 1024 * 1024;
    std::mutex inputMutex;
    std::condition_variable inputChanged;
    std::thread inputThread;
    std::atomic<bool> stopInput{false};
    size_t pendingBytes = 0;
    int inputResult = 0;
    std::multiset<int64_t> timestamps;
    std::deque<AVFrame*> frames;
    std::vector<void*> borrowedFrames;
    struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
    using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
    std::map<int, Frame> baseViews, dependentViews;
    int lastPoc = -1;
    std::function<bool()> interrupted;
    std::shared_ptr<std::mutex> discReadMutex;
    unsigned withoutStereo = 0;

    ~Impl() {
        closeFile();
        for (auto* frame : frames) av_frame_free(&frame);
        edge264_free(&decoder);
        if (disc) bd_close(disc);
    }
    bool cancelled() const {
        return stopInput || (interrupted && interrupted());
    }
    void stopReader() {
        stopInput = true;
        inputChanged.notify_all();
        if (inputThread.joinable()) inputThread.join();
    }
    void closeFile() {
        stopReader();
        avformat_close_input(&format);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
        if (file) file->close(file);
        file = nullptr; currentClip = -1; blockStart = -1;
        dependentPackets.clear();
        dependentPicture.reset();
        pendingBytes = 0; inputResult = 0; stopInput = false;
    }
    bool inputFull() const {
        return dependentPackets.size() >= maxPendingPackets || pendingBytes >= maxPendingBytes;
    }
    void queuePicture(MvcPacketAssembler::Packet picture) {
        {
            std::lock_guard lock(inputMutex);
            auto& destination = dependentPackets[picture.pts];
            pendingBytes -= destination.size();
            destination = std::move(picture.bytes);
            pendingBytes += destination.size();
        }
        inputChanged.notify_all();
    }
    bool appendPacket(const AVPacket& packet) {
        std::optional<MvcPacketAssembler::Packet> completed;
        const auto pts = packet.pts == AV_NOPTS_VALUE
            ? std::nullopt : std::optional<int64_t>(packet.pts);
        if (!dependentPicture.push(pts,
            {packet.data, static_cast<size_t>(packet.size)}, completed)) return false;
        if (completed) queuePicture(std::move(*completed));
        return true;
    }
    void readAhead(int stream) {
        AVPacket* packet = av_packet_alloc();
        int result = packet ? 0 : AVERROR(ENOMEM);
        try {
            while (packet && !cancelled()) {
                {
                    std::unique_lock lock(inputMutex);
                    while (inputFull() && !cancelled())
                        inputChanged.wait_for(lock, std::chrono::milliseconds(20));
                }
                if (cancelled()) break;
                result = av_read_frame(format, packet);
                if (result < 0) break;
                if (packet->stream_index == stream && !appendPacket(*packet)) {
                    result = AVERROR_INVALIDDATA;
                    break;
                }
                av_packet_unref(packet);
            }
            // Read errors and cancellation can leave a truncated picture.
            // Only a clean EOF makes the final pending picture publishable.
            if (result == AVERROR_EOF && !cancelled())
                if (auto picture = dependentPicture.finish()) queuePicture(std::move(*picture));
        } catch (const std::bad_alloc&) {
            result = AVERROR(ENOMEM);
        }
        av_packet_free(&packet);
        {
            std::lock_guard lock(inputMutex);
            inputResult = result < 0 ? result : AVERROR_EXIT;
        }
        inputChanged.notify_all();
    }
    bool takePacket(int64_t pts, std::vector<uint8_t>& bytes, std::string& error) {
        std::unique_lock lock(inputMutex);
        while (!dependentPackets.contains(pts)) {
            if (cancelled()) return false;
            if (inputResult || inputFull()) {
                error = inputResult && inputResult != AVERROR_EOF
                    ? "Could not read the MVC dependent view."
                    : "Could not synchronize the MVC dependent view.";
                return false;
            }
            inputChanged.wait_for(lock, std::chrono::milliseconds(20));
        }
        auto found = dependentPackets.find(pts);
        pendingBytes -= found->second.size();
        bytes = std::move(found->second);
        dependentPackets.erase(found);
        lock.unlock();
        inputChanged.notify_all();
        return true;
    }
    static int read(void* opaque, uint8_t* out, int size) {
        auto& s = *static_cast<Impl*>(opaque);
        // Keep each AVIO refill contiguous on the optical drive. Interleaving
        // these 6 KiB decrypt reads with base-view reads defeats read-ahead.
        std::unique_lock<std::mutex> discLock;
        if (s.discReadMutex) discLock = std::unique_lock(*s.discReadMutex);
        int copied = 0;
        while (copied < size && s.position < s.fileSize) {
            if (s.cancelled()) return copied ? copied : AVERROR_EXIT;
            const int64_t start = s.position / s.block.size() * s.block.size();
            if (start != s.blockStart) {
                if (s.file->seek(s.file, start, SEEK_SET) < 0) return AVERROR(EIO);
                auto n = s.file->read(s.file, s.block.data(), s.block.size());
                if (n <= 0) return copied ? copied : n < 0 ? AVERROR(EIO) : AVERROR_EOF;
                s.blockSize = static_cast<int>(n); s.blockStart = start;
            }
            const int offset = static_cast<int>(s.position - start);
            const int count = std::min(size - copied, s.blockSize - offset);
            if (count <= 0) return copied ? copied : AVERROR_EOF;
            std::memcpy(out + copied, s.block.data() + offset, count);
            copied += count; s.position += count;
        }
        return copied ? copied : AVERROR_EOF;
    }
    static int64_t seek(void* opaque, int64_t offset, int whence) {
        auto& s = *static_cast<Impl*>(opaque);
        if (whence == AVSEEK_SIZE) return s.fileSize;
        whence &= ~AVSEEK_FORCE;
        const int64_t origin = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? s.position :
            whence == SEEK_END ? s.fileSize : -1;
        if (origin < 0 || offset < -origin || offset > s.fileSize - origin) return AVERROR(EINVAL);
        s.position = origin + offset;
        return s.position;
    }
    bool openClip(int index, int64_t pts, std::string& error) {
        closeFile();
        const auto path = std::format("BDMV/STREAM/{}.m2ts", clips[index].dependent.clip);
        file = bd_open_file_dec(disc, path.c_str());
        if (!file) { error = "Could not open the MVC dependent-view stream."; return false; }
        fileSize = file->seek(file, 0, SEEK_END);
        if (fileSize <= 0) { error = "MVC dependent-view stream is empty."; return false; }
        position = 0;
        constexpr int bufferSize = 2 * 1024 * 1024;
        auto* buffer = static_cast<uint8_t*>(av_malloc(bufferSize));
        if (!buffer) { error = "Could not allocate MVC input buffer."; return false; }
        io = avio_alloc_context(buffer, bufferSize, 0, this, read, nullptr, seek);
        if (!io) { av_free(buffer); error = "Could not allocate MVC input."; return false; }
        format = avformat_alloc_context();
        if (!format) { error = "Could not allocate MVC demuxer."; return false; }
        format->pb = io;
        // FFmpeg's AVC parser does not delimit MVC slices. Keep the transport
        // PES boundaries and timestamps, and let edge264 parse the NALs.
        format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOPARSE | AVFMT_FLAG_NOFILLIN;
        format->skip_estimate_duration_from_pts = 1;
        format->interrupt_callback = {[](void* p) {
            return static_cast<Impl*>(p)->cancelled() ? 1 : 0;
        }, this};
        if (avformat_open_input(&format, nullptr, av_find_input_format("mpegts"), nullptr) < 0) {
            error = "Could not open the MVC transport stream."; return false;
        }
        currentClip = index;
        // Discover the video PID without trying to decode the dependent view
        // in FFmpeg. Seeking uses MPEG-TS timestamps and the aligned disc I/O.
        AVPacket* packet = av_packet_alloc();
        if (!packet) { error = "Could not allocate MVC packet."; return false; }
        int stream = -1;
        for (int n = 0; n < 128 && av_read_frame(format, packet) >= 0; ++n) {
            if (format->streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                stream = packet->stream_index;
                if (!appendPacket(*packet)) {
                    av_packet_free(&packet);
                    error = "MVC dependent picture exceeds the input limit."; return false;
                }
                av_packet_unref(packet); break;
            }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        if (stream < 0) { error = "MVC dependent video stream was not found."; return false; }
        const int64_t beginning = int64_t(clips[index].dependent.inTime) * 2;
        if (pts > beginning + 90000) {
            if (av_seek_frame(format, stream, std::max(beginning, pts - 90000), AVSEEK_FLAG_BACKWARD) < 0) {
                error = "Could not seek the MVC dependent view."; return false;
            }
            dependentPackets.clear();
            dependentPicture.reset();
            pendingBytes = 0;
        }
        try {
            inputThread = std::thread([this, stream] { readAhead(stream); });
        } catch (const std::system_error&) {
            error = "Could not start MVC disc read-ahead."; return false;
        }
        return true;
    }
    bool pairViews(std::string& error, bool drain = false) {
        // The decoder may return a base picture alongside a different dependent
        // picture. Own both views and match their POCs before publishing. A small
        // lookahead also restores display order for reordered B pictures.
        while (!baseViews.empty() && (drain || baseViews.size() > 4)) {
            auto base = baseViews.begin();
            auto dependent = dependentViews.find(base->first);
            if (dependent == dependentViews.end()) {
                if (drain || baseViews.size() > 32) {
                    error = "MVC stream has an unmatched stereo picture."; return false;
                }
                break;
            }
            auto* left = baseRight ? dependent->second.get() : base->second.get();
            auto* right = baseRight ? base->second.get() : dependent->second.get();
            if (timestamps.empty() || left->width != right->width || left->height != right->height) {
                error = "MVC stereo views have incompatible timing or dimensions."; return false;
            }
            Frame out(av_frame_alloc());
            if (!out) { error = "Could not allocate MVC frame."; return false; }
            out->format = AV_PIX_FMT_YUV420P;
            out->width = left->width * 2; out->height = left->height;
            out->color_range = AVCOL_RANGE_MPEG; out->colorspace = AVCOL_SPC_BT709;
            out->best_effort_timestamp = *timestamps.begin(); timestamps.erase(timestamps.begin());
            if (av_frame_get_buffer(out.get(), 32) < 0) {
                error = "Could not allocate stereo frame planes."; return false;
            }
            for (int p = 0; p < 3; ++p) {
                const int w = p ? (left->width + 1) / 2 : left->width;
                const int h = p ? (left->height + 1) / 2 : left->height;
                for (int y = 0; y < h; ++y) {
                    auto* row = out->data[p] + y * out->linesize[p];
                    std::memcpy(row, left->data[p] + y * left->linesize[p], w);
                    std::memcpy(row + w, right->data[p] + y * right->linesize[p], w);
                }
            }
            lastPoc = base->first;
            baseViews.erase(base); dependentViews.erase(dependent);
            while (!dependentViews.empty() && dependentViews.begin()->first <= lastPoc)
                dependentViews.erase(dependentViews.begin());
            frames.push_back(out.release()); withoutStereo = 0;
        }
        return true;
    }
    void releasePictures() {
        for (auto* token : borrowedFrames) edge264_return_frame(decoder, token);
        borrowedFrames.clear();
    }
    bool collect(std::string& error) {
        Edge264Frame decoded{};
        while (edge264_get_frame(decoder, &decoded, 1) == 0) {
            if (decoded.bit_depth_Y != 8 || decoded.bit_depth_C != 8 ||
                decoded.width_Y <= 0 || decoded.height_Y <= 0 ||
                decoded.width_C != (decoded.width_Y + 1) / 2 ||
                decoded.height_C != (decoded.height_Y + 1) / 2) {
                edge264_return_frame(decoder, decoded.return_arg);
                error = "Unsupported MVC frame layout."; return false;
            }
            bool valid = true;
            for (int view = 0; view < 2; ++view) {
                auto& pool = view ? dependentViews : baseViews;
                const int poc = view ? decoded.PictureOrderCnt_mvc : decoded.PictureOrderCnt;
                const auto* samples = view ? decoded.samples_mvc : decoded.samples;
                if (!samples[0] || !samples[1] || !samples[2] || poc <= lastPoc || pool.contains(poc)) continue;
                Frame copy(av_frame_alloc());
                if (!copy) { valid = false; break; }
                copy->format = AV_PIX_FMT_YUV420P;
                copy->width = decoded.width_Y; copy->height = decoded.height_Y;
                if (av_frame_get_buffer(copy.get(), 32) < 0) { valid = false; break; }
                for (int p = 0; p < 3; ++p) {
                    const int w = p ? decoded.width_C : decoded.width_Y;
                    const int h = p ? decoded.height_C : decoded.height_Y;
                    const int stride = p ? decoded.stride_C : decoded.stride_Y;
                    for (int y = 0; y < h; ++y)
                        std::memcpy(copy->data[p] + y * copy->linesize[p], samples[p] + y * stride, w);
                }
                pool.emplace(poc, std::move(copy));
            }
            // A non-reference base picture can still be the inter-view
            // reference for this access unit. Keep its decoder slot borrowed
            // until all dependent slices have been submitted, including when
            // backpressure forces us to collect partway through the unit.
            borrowedFrames.push_back(decoded.return_arg);
            if (!valid) { error = "Could not allocate MVC view planes."; return false; }
            if (dependentViews.size() > 64) {
                error = "MVC dependent pictures are not matching the base view."; return false;
            }
            if (!pairViews(error)) return false;
        }
        return true;
    }
    bool nal(std::span<const uint8_t> data, std::string& error) {
        if (data.empty()) return true;
        // End markers belong to each elementary stream. Draining on the base
        // marker would wait for dependent pictures we have not submitted yet.
        // Drain both views together at clip boundaries and end of input.
        if ((data[0] & 31) == 10 || (data[0] & 31) == 11) return true;
        // edge264's bitreader requires two guard bytes before and 64 after.
        std::vector<uint8_t> padded(data.size() + 66, 0);
        padded[0] = padded[1] = 0xff;
        std::memcpy(padded.data() + 2, data.data(), data.size());
        int result = 0;
        for (int retry = 0; retry < 5; ++retry) {
            result = edge264_decode_NAL(decoder, padded.data() + 2,
                padded.data() + 2 + data.size(), nullptr, nullptr);
            if (result != ENOBUFS) break;
            edge264_bump_frames(decoder);
            if (!collect(error)) return false;
        }
        if (result != 0 && result != ENOTSUP && result != EBADMSG) {
            error = std::format("MVC decoding failed ({}) NAL {} clip {} base pool {} dep pool {} PTS {}.", result, data[0] & 31, currentClip, baseViews.size(), dependentViews.size(), timestamps.empty() ? 0 : *timestamps.begin()); return false;
        }
        // Collect after BOTH views, so a displayed non-reference base
        // picture cannot be recycled before its dependent view uses it.
        return true;
    }
    static std::vector<std::span<const uint8_t>> nals(std::span<const uint8_t> data) {
        std::vector<std::span<const uint8_t>> result;
        const auto* bytes = data.data();
        const size_t size = data.size();
        size_t start = 0;
        for (size_t i = 0; i + 3 <= size; ++i) {
            if (bytes[i] || bytes[i + 1] || bytes[i + 2] != 1) continue;
            size_t end = i;
            while (end > start && bytes[end - 1] == 0) --end;
            if (end > start) result.push_back(data.subspan(start, end - start));
            start = i + 3; i += 2;
        }
        if (start < data.size()) result.push_back(data.subspan(start));
        return result;
    }
};
#else
struct MvcDecoder::Impl {};
#endif

MvcDecoder::MvcDecoder() : impl(std::make_unique<Impl>()) {}
MvcDecoder::~MvcDecoder() = default;
bool MvcDecoder::open(const std::filesystem::path& path, int titleIndex,
    std::function<bool()> interrupted, std::string& error,
    std::shared_ptr<std::mutex> discReadMutex) {
#ifdef RENDEPTH_ENABLE_MVC
    impl = std::make_unique<Impl>();
    impl->interrupted = std::move(interrupted);
    impl->discReadMutex = std::move(discReadMutex);
    impl->disc = bd_open(BlurayReader::resolveDiscRoot(path).string().c_str(), nullptr);
    if (!impl->disc) { error = "Could not open MVC disc source."; return false; }
    if (!bd_get_titles(impl->disc, TITLES_RELEVANT, 0)) bd_get_titles(impl->disc, TITLES_ALL, 0);
    auto* title = bd_get_title_info(impl->disc, titleIndex, 0);
    if (!title) { error = "Could not inspect MVC playlist."; return false; }
    std::unique_ptr<BLURAY_TITLE_INFO, decltype(&bd_free_title_info)> info(title, bd_free_title_info);
    void* data = nullptr; int64_t size = 0;
    const auto playlist = std::format("BDMV/PLAYLIST/{:05}.mpls", title->playlist);
    const bool read = bd_read_file(impl->disc, playlist.c_str(), &data, &size) > 0;
    std::vector<BlurayPlaylist::MvcClip> dependent;
    const bool valid = read && data && size > 0 && BlurayPlaylist::hasMvc(
        {static_cast<const uint8_t*>(data), static_cast<size_t>(size)}, &dependent);
    std::free(data);
    if (!valid) { error = "Unsupported MVC playlist structure."; return false; }
    int64_t offset = 0;
    for (unsigned i = 0; i < title->clip_count; ++i) {
        const auto found = std::find_if(dependent.begin(), dependent.end(), [&](const auto& clip) { return clip.playItem == i; });
        if (found == dependent.end()) { error = "MVC playlist has an unmatched base-view clip."; return false; }
        const auto& base = title->clips[i];
        const int64_t end = offset + int64_t(base.pkt_count) * 192;
        impl->clips.push_back({*found, offset, end, int64_t(base.start_time) - int64_t(base.in_time) + int64_t(title->clips[0].in_time)});
        offset = end;
    }
    impl->baseRight = title->mvc_base_view_r_flag != 0;
    // The synchronous decoder keeps compressed buffers owned by this call and
    // avoids cross-thread frame-return races in the upstream C ABI.
    impl->decoder = edge264_alloc(0, nullptr, nullptr, 0, nullptr, nullptr, nullptr);
    if (!impl->decoder) { error = "Could not allocate MVC decoder."; return false; }
    return true;
#else
    error = "Blu-ray 3D (MVC) playback is not supported by this build.";
    return false;
#endif
}

int64_t MvcDecoder::timestampOffset(int64_t bytePosition) const {
#ifdef RENDEPTH_ENABLE_MVC
    for (const auto& clip : impl->clips)
        if (bytePosition >= clip.byteStart && bytePosition < clip.byteEnd) return clip.timestampOffset;
#endif
    return 0;
}

bool MvcDecoder::send(const AVPacket& base, std::string& error) {
#ifdef RENDEPTH_ENABLE_MVC
    if (base.pts == AV_NOPTS_VALUE || base.pos < 0) { error = "MVC base view is missing timing or clip position."; return false; }
    int index = -1;
    for (size_t i = 0; i < impl->clips.size(); ++i)
        if (base.pos >= impl->clips[i].byteStart && base.pos < impl->clips[i].byteEnd) { index = static_cast<int>(i); break; }
    if (index < 0) { error = "MVC base-view packet is outside its playlist."; return false; }
    const auto& clip = impl->clips[index].dependent;
    const int64_t pts = base.pts - impl->clips[index].timestampOffset + (int64_t(clip.inTime) - clip.syncTime) * 2;
    if (impl->currentClip != index || impl->needSeek) {
        if (impl->currentClip >= 0 && impl->currentClip != index) {
            edge264_bump_frames(impl->decoder);
            if (!impl->collect(error) || !impl->pairViews(error, true)) return false;
            impl->releasePictures();
            edge264_flush(impl->decoder);
            impl->timestamps.clear(); impl->baseViews.clear(); impl->dependentViews.clear(); impl->lastPoc = -1;
        }
        if (!impl->openClip(index, pts, error)) return false;
        impl->needSeek = false;
    }
    std::vector<uint8_t> dependent;
    if (!impl->takePacket(pts, dependent, error)) return false;
    const auto nals = Impl::nals(dependent);
    // Announce the second view before decoding the first picture, preventing
    // the decoder from treating an initial base frame as a complete 2D frame.
    for (auto nal : nals) if ((nal[0] & 31) == 15 && !impl->nal(nal, error)) return false;
    impl->timestamps.insert(base.pts);
    for (auto nal : Impl::nals({base.data, static_cast<size_t>(base.size)})) if (!impl->nal(nal, error)) return false;
    for (auto nal : nals) if ((nal[0] & 31) != 15 && !impl->nal(nal, error)) return false;
    if (!impl->collect(error)) return false;
    impl->releasePictures();
    if (++impl->withoutStereo > 250) { error = "MVC stream did not produce stereo frames."; return false; }
    return true;
#else
    return false;
#endif
}
AVFrame* MvcDecoder::receive() {
#ifdef RENDEPTH_ENABLE_MVC
    if (impl->frames.empty()) return nullptr;
    auto* result = impl->frames.front(); impl->frames.pop_front(); return result;
#else
    return nullptr;
#endif
}
void MvcDecoder::flush() {
#ifdef RENDEPTH_ENABLE_MVC
    impl->stopReader();
    impl->releasePictures();
    if (impl->decoder) edge264_flush(impl->decoder);
    impl->timestamps.clear(); impl->dependentPackets.clear(); impl->needSeek = true; impl->withoutStereo = 0;
    impl->dependentPicture.reset();
    for (auto* frame : impl->frames) av_frame_free(&frame);
    impl->frames.clear();
    impl->baseViews.clear(); impl->dependentViews.clear(); impl->lastPoc = -1;
#endif
}
bool MvcDecoder::finish(std::string& error) {
#ifdef RENDEPTH_ENABLE_MVC
    edge264_bump_frames(impl->decoder);
    const bool result = impl->collect(error) && impl->pairViews(error, true);
    impl->releasePictures();
    return result;
#else
    return false;
#endif
}
