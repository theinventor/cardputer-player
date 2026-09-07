#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "third_party/minimp3.h"

namespace ct {
class Reader {
public:
    virtual ~Reader() = default;
    virtual size_t read(void* data, size_t bytes) = 0;
    virtual bool seek(uint32_t offset) = 0;
    virtual uint32_t size() const = 0;
};

struct Tags {
    std::string title, artist, album;
    uint32_t audioStart = 0;
    uint32_t artworkOffset = 0, artworkBytes = 0;
};
Tags readTags(Reader& file);
std::string basename(const std::string& path);
bool isMp3(const std::string& path);
bool validMusicPath(const std::string& path);
std::string uploadPath(const std::string& name);
bool contains(const std::string& text, const std::string& query);

// A bounded sparse index gives sample-based seeking without retaining one
// record per MP3 frame. Older anchors are thinned for very long recordings.
class Mp3Stream {
public:
    bool open(Reader& file);
    int decode(int16_t* pcm);
    bool seekMs(uint32_t milliseconds);
    uint32_t positionMs() const;
    uint32_t durationMs() const;
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    const std::string& error() const { return error_; }
    float indexingProgress() const;
    bool (*keepGoing)(void*) = nullptr;
    void* context = nullptr;
private:
    struct Anchor { uint64_t sample; uint32_t offset; };
    Reader* file_ = nullptr;
    mp3dec_t decoder_{};
    std::array<uint8_t, 16384> input_{};
    std::array<Anchor, 512> anchors_{};
    size_t anchorCount_ = 0;
    uint64_t stride_ = 0, nextAnchor_ = 0;
    uint64_t total_ = 0, current_ = 0, discardUntil_ = 0;
    uint32_t offset_ = 0, readOffset_ = 0;
    size_t filled_ = 0, consumed_ = 0;
    int rate_ = 0, channels_ = 0;
    std::string error_;
    bool reset(uint32_t offset);
    bool fill();
    int frame(int16_t* pcm, mp3dec_frame_info_t& info, uint32_t& offset);
    void anchor(uint64_t sample, uint32_t offset);
};
}
