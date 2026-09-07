#include "media.h"
#include "order.h"
#include "input.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

class MemoryReader : public ct::Reader {
public:
    explicit MemoryReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    size_t read(void* data, size_t count) override {
        ++reads;
        count = std::min(count, bytes_.size() - pos_);
        if (count) memcpy(data, bytes_.data() + pos_, count);
        pos_ += count; return count;
    }
    bool seek(uint32_t pos) override { ++seeks; if (pos > bytes_.size()) return false; pos_ = pos; return true; }
    uint32_t size() const override { return bytes_.size(); }
    size_t reads = 0, seeks = 0;
private:
    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
};
std::vector<uint8_t> metadataChain(unsigned version, unsigned frames, unsigned titleFrame = UINT32_MAX) {
    unsigned frameHeader = version == 2 ? 6 : 10;
    uint32_t length = frames * (frameHeader + 2), footer = version == 4 ? 10 : 0;
    std::vector<uint8_t> bytes(10 + length + footer + 128);
    memcpy(bytes.data(), "ID3", 3); bytes[3] = version; bytes[5] = footer ? 0x10 : 0;
    for (unsigned i = 0; i < 4; ++i) bytes[6 + i] = (length >> (7 * (3 - i))) & 127;
    for (unsigned i = 0; i < frames; ++i) {
        auto* frame = bytes.data() + 10 + i * (frameHeader + 2);
        const char* id = i == titleFrame ? (version == 2 ? "TT2" : "TIT2") : (version == 2 ? "PRV" : "PRIV");
        memcpy(frame, id, version == 2 ? 3 : 4);
        frame[version == 2 ? 5 : 7] = 2;
        frame[frameHeader + 1] = 'x';
    }
    if (footer) { memcpy(bytes.data() + 10 + length, bytes.data(), 10); memcpy(bytes.data() + 10 + length, "3DI", 3); }
    return bytes;
}
void testMetadataBudget() {
    for (unsigned version : {2u, 3u, 4u}) {
        for (unsigned frames : {65u, 100000u}) {
            MemoryReader reader(metadataChain(version, frames));
            auto tags = ct::readTags(reader);
            assert(tags.audioStart == reader.size() - 128);
            assert(tags.title.empty() && tags.artist.empty() && tags.album.empty());
            assert(reader.reads == 66 && reader.seeks == 66);
        }
        for (unsigned titleFrame : {63u, 64u}) {
            MemoryReader reader(metadataChain(version, 65, titleFrame));
            auto tags = ct::readTags(reader);
            assert(tags.audioStart == reader.size() - 128);
            assert(tags.title == (titleFrame == 63 ? "x" : ""));
            assert(reader.reads == (titleFrame == 63 ? 67u : 66u) && reader.seeks == 66);
        }
        auto bytes = metadataChain(version, 100000, 64);
        auto* fallback = bytes.data() + bytes.size() - 128;
        memcpy(fallback, "TAG", 3); memcpy(fallback + 3, "Fallback title", 14);
        MemoryReader reader(std::move(bytes));
        auto tags = ct::readTags(reader);
        assert(tags.title == "Fallback title" && tags.audioStart == reader.size() - 128);
        assert(reader.reads == 66 && reader.seeks == 66);
    }
    std::cout << "ID3 metadata budget: 64-frame boundary, 100000-frame chains, audio offsets, and ID3v1 fallback passed\n";
}
std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void testFile(const char* path) {
    MemoryReader reader(readFile(path));
    auto tags = ct::readTags(reader);
    assert(tags.title == "Cardtunes test");
    assert(tags.artist == "Troy Anderson");
    auto decoder = std::make_unique<ct::Mp3Stream>();
    assert(decoder->open(reader));
    assert(decoder->durationMs() >= 12000 && decoder->durationMs() < 12200);
    std::vector<int16_t> reference;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int count;
    while ((count = decoder->decode(pcm)) > 0) reference.insert(reference.end(), pcm, pcm + count);
    assert(reference.size() > 12000);
    long long energy = 0;
    for (int16_t sample : reference) energy += int(sample) * int(sample);
    assert(energy > 1000000);
    for (uint32_t ms : {0u, 250u, 3500u, 7821u, 11000u}) {
        assert(decoder->seekMs(ms));
        assert(decoder->positionMs() <= ms && decoder->positionMs() + 1 >= ms);
        size_t offset = uint64_t(ms) * decoder->rate() / 1000 * decoder->channels();
        size_t checked = 0;
        while (checked < 4096 && (count = decoder->decode(pcm)) > 0) {
            for (int i = 0; i < count; ++i) {
                assert(offset + checked + i < reference.size());
                if (std::abs(int(pcm[i]) - int(reference[offset + checked + i])) > 2) {
                    std::cerr << "Seek PCM mismatch: " << path << " at " << ms << "ms, sample " << checked + i << "\n";
                    std::abort();
                }
            }
            checked += count;
        }
        assert(checked >= 4096);
    }
    assert(decoder->seekMs(UINT32_MAX));
    assert(decoder->decode(pcm) == 0);
    std::cout << "Decoded and sample-checked seeks: " << path << "\n";
}
int main(int argc, char** argv) {
    testMetadataBudget();
    ct::KeyEdges keys;
    assert(keys.press(0) == 0);
    assert(keys.press(1) == 1);
    assert(keys.press(1) == 0);
    assert(keys.press(3) == 2);
    assert(keys.press(1) == 0); // Releasing another key must not replay a held key.
    assert(keys.press(2) == 2); // Same number of held keys, different physical key.
    assert(keys.press(0) == 0);
    assert(keys.press(uint64_t(1) << 55) == uint64_t(1) << 55);
    assert(keys.press(0) == 0);
    assert(keys.press(2) == 2);
    assert(ct::isMp3("/Music/Test.MP3"));
    assert(!ct::isMp3("/Music/._song.mp3"));
    assert(ct::uploadPath("Song 1.mp3") == "/Music/Song 1.mp3");
    for (const char* name : {"../x.mp3", "a/b.mp3", "a\\b.mp3", ".x.mp3", "bad\n.mp3", "x.wav"}) assert(ct::uploadPath(name).empty());
    assert(!ct::validMusicPath("/Music/../x.mp3"));
    assert(!ct::validMusicPath("/Music//x.mp3"));
    assert(ct::contains("The Artist", "ARTIST"));
    ct::Order order;
    order.reset(20); order.seed(123); order.select(7); order.shuffle(true);
    std::set<int> seen{7};
    for (int i = 0; i < 19; ++i) assert(seen.insert(order.next()).second);
    assert(seen.size() == 20);
    order.repeat(ct::Repeat::Off); assert(order.next(true) == -1);
    order.select(3); order.repeat(ct::Repeat::One); assert(order.next(true) == 3);
    assert(order.enqueue(8)); assert(order.next() == 8); assert(order.previous() == 3);
    assert(!order.enqueue(20));
    order.reset(0); assert(order.next() == -1); assert(order.previous() == -1);
    std::mt19937 rng(42);
    for (unsigned length = 0; length < 4096; length += 7) {
        std::vector<uint8_t> junk(length);
        for (auto& value : junk) value = rng();
        if (length > 10) { memcpy(junk.data(), "ID3", 3); junk[3] = 2 + length % 3; }
        MemoryReader reader(junk);
        ct::readTags(reader);
        auto decoder = std::make_unique<ct::Mp3Stream>();
        decoder->open(reader);
    }
    for (int i = 1; i < argc; ++i) testFile(argv[i]);
    std::cout << "Media, malformed metadata, paths, and playback-order tests passed\n";
}
