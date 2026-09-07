#include "media.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3.h"

namespace ct {
namespace {
uint32_t be32(const uint8_t* b) {
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
}
uint32_t synchsafe(const uint8_t* b) {
    if ((b[0] | b[1] | b[2] | b[3]) & 0x80) return UINT32_MAX;
    return (uint32_t(b[0]) << 21) | (uint32_t(b[1]) << 14) | (uint32_t(b[2]) << 7) | b[3];
}
void utf8(std::string& out, uint32_t c) {
    if (c < 32 || c == 127) { if (c) out += ' '; return; }
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
    else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
    } else {
        out += char(0xf0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63));
    }
}
std::string textTag(const uint8_t* b, size_t n) {
    std::string out;
    if (!n) return out;
    int encoding = *b++; --n;
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 0; i < n && b[i]; ++i) {
            if (encoding == 0) utf8(out, b[i]);
            else out += b[i] < 32 ? ' ' : char(b[i]);
        }
    } else if (encoding == 1 || encoding == 2) {
        bool little = false;
        if (n >= 2 && b[0] == 0xff && b[1] == 0xfe) { little = true; b += 2; n -= 2; }
        else if (n >= 2 && b[0] == 0xfe && b[1] == 0xff) { b += 2; n -= 2; }
        auto unit = [little](const uint8_t* p) -> uint16_t {
            return little ? uint16_t(p[0] | (p[1] << 8)) : uint16_t((p[0] << 8) | p[1]);
        };
        for (size_t i = 0; i + 1 < n; i += 2) {
            uint32_t c = unit(b + i);
            if (!c) break;
            if (c >= 0xd800 && c <= 0xdbff && i + 3 < n) {
                uint32_t low = unit(b + i + 2);
                if (low >= 0xdc00 && low <= 0xdfff) {
                    c = 0x10000 + ((c - 0xd800) << 10) + low - 0xdc00; i += 2;
                } else c = 0xfffd;
            } else if (c >= 0xd800 && c <= 0xdfff) c = 0xfffd;
            utf8(out, c);
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
}

std::string basename(const std::string& path) {
    auto slash = path.find_last_of('/');
    return path.substr(slash == std::string::npos ? 0 : slash + 1);
}
bool isMp3(const std::string& path) {
    auto name = basename(path);
    if (name.size() < 5 || name[0] == '.') return false;
    auto ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), lower);
    return ext == ".mp3";
}
bool validMusicPath(const std::string& path) {
    if (path.empty() || path[0] != '/' || path.size() >= 192 || !isMp3(path)) return false;
    size_t start = 1;
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && (uint8_t(path[i]) < 32 || path[i] == '\\')) return false;
        if (i == path.size() || path[i] == '/') {
            auto part = path.substr(start, i - start);
            if (part.empty() || part[0] == '.') return false;
            start = i + 1;
        }
    }
    return true;
}
std::string uploadPath(const std::string& name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return {};
    auto path = "/Music/" + name;
    return validMusicPath(path) ? path : "";
}
bool contains(const std::string& text, const std::string& query) {
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [](char a, char b) { return lower(a) == lower(b); }) != text.end();
}

Tags readTags(Reader& file) {
    Tags result;
    uint8_t header[10];
    if (!file.seek(0) || file.read(header, 10) != 10) return result;
    if (!memcmp(header, "ID3", 3)) {
        uint32_t length = synchsafe(header + 6);
        uint32_t footer = header[3] == 4 && (header[5] & 0x10) ? 10 : 0;
        if (length <= file.size() - 10 && footer <= file.size() - 10 - length) {
            result.audioStart = 10 + length + footer;
            int version = header[3];
            // Unsupported unsynchronisation/compression is skipped as a whole;
            // never interpret its transformed byte offsets as normal frames.
            if ((version == 2 || version == 3 || version == 4) && !(header[5] & 0xc0)) {
                uint32_t pos = 10, end = 10 + length;
                uint32_t headerSize = version == 2 ? 6 : 10;
                // Keep one scan step bounded even when a tag has many tiny frames.
                constexpr unsigned MaxMetadataFrames = 64;
                for (unsigned frames = 0; frames < MaxMetadataFrames && end - pos >= headerSize; ++frames) {
                    uint8_t frameHeader[10]{};
                    if (!file.seek(pos) || file.read(frameHeader, headerSize) != headerSize || !frameHeader[0]) break;
                    uint32_t bytes = version == 2 ? (uint32_t(frameHeader[3]) << 16) | (uint32_t(frameHeader[4]) << 8) | frameHeader[5]
                        : version == 3 ? be32(frameHeader + 4) : synchsafe(frameHeader + 4);
                    pos += headerSize;
                    if (!bytes || bytes > end - pos) break;
                    std::string id(reinterpret_cast<char*>(frameHeader), version == 2 ? 3 : 4);
                    bool plain = version == 2 || frameHeader[9] == 0;
                    if (plain && (id == "TIT2" || id == "TT2" || id == "TPE1" || id == "TP1" || id == "TALB" || id == "TAL")) {
                        uint8_t data[512];
                        size_t n = file.read(data, std::min(bytes, uint32_t(sizeof(data))));
                        auto value = textTag(data, n);
                        if (id == "TIT2" || id == "TT2") result.title = value;
                        else if (id == "TPE1" || id == "TP1") result.artist = value;
                        else result.album = value;
                    } else if (plain && id == "APIC" && !result.artworkBytes) {
                        uint8_t data[512];
                        size_t n = file.read(data, std::min(bytes, uint32_t(sizeof(data))));
                        if (n > 12 && (data[0] == 0 || data[0] == 3)) {
                            size_t p = 1;
                            while (p < n && data[p]) ++p;
                            std::string mime(reinterpret_cast<char*>(data + 1), p - 1);
                            p += 2; // MIME terminator and picture type
                            while (p < n && data[p]) ++p;
                            ++p;
                            if (p < n && (mime == "image/jpeg" || mime == "image/jpg") && bytes - p <= 256 * 1024) {
                                result.artworkOffset = pos + p; result.artworkBytes = bytes - p;
                            }
                        }
                    }
                    pos += bytes;
                }
            }
        }
    }
    if (file.size() >= 128 && (result.title.empty() || result.artist.empty() || result.album.empty())) {
        uint8_t tag[128];
        if (file.seek(file.size() - 128) && file.read(tag, 128) == 128 && !memcmp(tag, "TAG", 3)) {
            auto field = [&](int pos) { uint8_t data[31]{}; memcpy(data + 1, tag + pos, 30); return textTag(data, 31); };
            if (result.title.empty()) result.title = field(3);
            if (result.artist.empty()) result.artist = field(33);
            if (result.album.empty()) result.album = field(63);
        }
    }
    return result;
}

bool Mp3Stream::reset(uint32_t offset) {
    mp3dec_init(&decoder_);
    filled_ = consumed_ = 0;
    offset_ = readOffset_ = offset;
    return file_ && file_->seek(offset);
}
bool Mp3Stream::fill() {
    if (consumed_) {
        std::memmove(input_.data(), input_.data() + consumed_, filled_ - consumed_);
        offset_ += consumed_;
        filled_ -= consumed_; consumed_ = 0;
    }
    size_t count = file_->read(input_.data() + filled_, input_.size() - filled_);
    filled_ += count; readOffset_ += count;
    return filled_ > 0;
}
int Mp3Stream::frame(int16_t* pcm, mp3dec_frame_info_t& info, uint32_t& offset) {
    while (true) {
        if (filled_ - consumed_ < 8192 && !fill()) return 0;
        offset = offset_ + consumed_;
        int samples = mp3dec_decode_frame(&decoder_, input_.data() + consumed_, filled_ - consumed_, pcm, &info);
        if (info.frame_bytes <= 0) return 0;
        if (!samples && pcm && info.hz && info.channels && info.frame_offset + 4 <= info.frame_bytes) {
            // A fresh decoder may lack the first frame's bit reservoir. Keep
            // its time in the sample clock; seek warm-up discards this silence.
            samples = hdr_frame_samples(input_.data() + consumed_ + info.frame_offset);
            std::fill(pcm, pcm + samples * info.channels, int16_t(0));
        }
        consumed_ += info.frame_bytes;
        offset += info.frame_offset;
        if (samples) return samples;
        if (keepGoing && !keepGoing(context)) { error_ = "Cancelled"; return 0; }
    }
}
void Mp3Stream::anchor(uint64_t sample, uint32_t offset) {
    if (anchorCount_ && sample < nextAnchor_) return;
    if (anchorCount_ == anchors_.size()) {
        for (size_t i = 0; i < anchorCount_ / 2; ++i) anchors_[i] = anchors_[i * 2];
        anchorCount_ /= 2; stride_ *= 2;
    }
    anchors_[anchorCount_++] = {sample, offset};
    nextAnchor_ = sample + stride_;
}
bool Mp3Stream::open(Reader& file) {
    file_ = &file; error_.clear();
    total_ = current_ = discardUntil_ = 0;
    rate_ = channels_ = 0; anchorCount_ = 0; nextAnchor_ = 0;
    auto tags = readTags(file);
    if (!reset(tags.audioStart)) { error_ = "Cannot read SD file"; return false; }
    mp3dec_frame_info_t info{};
    uint32_t offset = 0;
    int samples;
    while ((samples = frame(nullptr, info, offset)) > 0) {
        if (!rate_) { rate_ = info.hz; channels_ = info.channels; stride_ = uint64_t(rate_) * 5; }
        if (info.hz != rate_ || info.channels != channels_ || info.layer != 3) {
            error_ = "Unsupported MP3 format change"; return false;
        }
        anchor(total_, offset); total_ += samples;
        if (keepGoing && !keepGoing(context)) { error_ = "Cancelled"; return false; }
    }
    if (!error_.empty()) return false;
    if (!total_ || !anchorCount_) { error_ = "No MP3 audio frames"; return false; }
    return reset(anchors_[0].offset);
}
int Mp3Stream::decode(int16_t* pcm) {
    mp3dec_frame_info_t info{};
    uint32_t offset;
    while (true) {
        int count = frame(pcm, info, offset);
        if (!count) return 0;
        if (info.hz != rate_ || info.channels != channels_) { error_ = "MP3 format changed"; return 0; }
        uint64_t before = current_;
        current_ += count;
        if (current_ <= discardUntil_) continue;
        size_t skip = before < discardUntil_ ? discardUntil_ - before : 0;
        if (skip) std::memmove(pcm, pcm + skip * channels_, (count - skip) * channels_ * sizeof(int16_t));
        return (count - skip) * channels_;
    }
}
bool Mp3Stream::seekMs(uint32_t ms) {
    if (!anchorCount_) return false;
    uint64_t target = std::min(total_, uint64_t(ms) * rate_ / 1000);
    if (target == total_) { current_ = total_; discardUntil_ = total_; return reset(file_->size()); }
    uint64_t warmup = target > uint64_t(rate_) ? target - rate_ : 0;
    size_t chosen = 0;
    for (size_t i = 1; i < anchorCount_ && anchors_[i].sample <= warmup; ++i) chosen = i;
    current_ = anchors_[chosen].sample;
    discardUntil_ = target;
    return reset(anchors_[chosen].offset);
}
uint32_t Mp3Stream::positionMs() const { return rate_ ? std::max(current_, discardUntil_) * 1000 / rate_ : 0; }
uint32_t Mp3Stream::durationMs() const { return rate_ ? total_ * 1000 / rate_ : 0; }
float Mp3Stream::indexingProgress() const { return file_ && file_->size() ? float(offset_ + consumed_) / file_->size() : 0; }
}
