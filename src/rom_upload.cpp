#include "rom_upload.h"
#include <algorithm>

namespace ct {
uint32_t RomUpload::hash(uint32_t value, const uint8_t* data, size_t count) {
    while (count--) { value ^= *data++; value *= 16777619u; }
    return value;
}
bool RomUpload::begin(const std::string& path, uint32_t size, uint32_t hash) {
    if (active()) return fail("USB upload already active");
    if (!GameBoy::validPath(path) || size < 32768 || size > GameBoy::MaxRomBytes) return fail("Invalid ROM path or size");
    if (!files_.ready() || files_.exists(path)) return fail("SD unavailable or destination exists");
    path_ = path; size_ = size; hash_ = hash; received_ = 0; error_.clear();
    writer_ = files_.openWriter(Temp);
    return writer_ ? true : fail("Cannot open USB upload temporary file");
}
bool RomUpload::chunk(uint32_t offset, const std::string& hex) {
    if (!active() || offset != received_ || hex.empty() || hex.size() > 1024 || hex.size() % 2 || hex.size() / 2 > size_ - received_)
        return fail("Invalid USB chunk or offset");
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string bytes(hex.size() / 2, '\0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        int hi = digit(hex[2 * i]), lo = digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return fail("Invalid USB hex data");
        bytes[i] = (hi << 4) | lo;
    }
    if (!writer_->write(bytes)) { abort(); return fail("SD write failed"); }
    received_ += bytes.size(); return true;
}
bool RomUpload::finish() {
    if (!active() || received_ != size_) return fail("Incomplete USB upload");
    if (!writer_->finish()) { abort(); return fail("SD flush failed"); }
    writer_.reset();
    auto file = files_.openReader(Temp);
    if (!file || file->size() != size_ || !GameBoy::validate(*file, error_)) {
        file.reset(); abort(); return fail("Invalid uploaded ROM");
    }
    if (!file->seek(0)) { file.reset(); abort(); return fail("SD readback failed"); }
    uint8_t bytes[512]; uint32_t actual = 2166136261u;
    for (uint32_t offset = 0; offset < size_;) {
        size_t count = std::min<uint32_t>(sizeof(bytes), size_ - offset);
        if (file->read(bytes, count) != count) { file.reset(); abort(); return fail("SD readback failed"); }
        actual = hash(actual, bytes, count); offset += count;
    }
    file.reset();
    if (actual != hash_) { abort(); return fail("USB upload checksum mismatch"); }
    if (files_.exists(path_) || !files_.rename(Temp, path_)) { abort(); return fail("Cannot finalize ROM without overwriting"); }
    error_.clear(); return true;
}
void RomUpload::abort() { writer_.reset(); files_.remove(Temp); }
}
