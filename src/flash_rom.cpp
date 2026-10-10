#include "flash_rom.h"
#include "gameboy.h"
#include <Arduino.h>
#include <SHA2Builder.h>
#include <algorithm>
#include <cstring>

namespace ct {
namespace {
constexpr uint32_t Offset = 0x10000, Magic = 0x43544742;
struct Header { uint32_t magic, size; uint8_t digest[32]; };
bool readHash(Reader& file, uint8_t* digest) {
    if (!file.seek(0)) return false;
    SHA256Builder hash; hash.begin(); uint8_t buffer[1024];
    for (uint32_t at = 0; at < file.size();) {
        size_t count = std::min<uint32_t>(sizeof(buffer), file.size() - at);
        if (file.read(buffer, count) != count) return false;
        hash.add(buffer, count); at += count;
        if (!(at % 4096)) delay(1);
    }
    hash.calculate(); hash.getBytes(digest); return true;
}
bool mappedHash(const uint8_t* data, uint32_t size, const uint8_t* expected) {
    SHA256Builder hash; hash.begin();
    for (uint32_t at = 0; at < size; at += 4096) { hash.add(data + at, std::min<uint32_t>(4096, size - at)); delay(1); }
    uint8_t actual[32]; hash.calculate(); hash.getBytes(actual);
    return memcmp(actual, expected, sizeof(actual)) == 0;
}
}
void FlashRom::reset() {
    if (data_) esp_partition_munmap(handle_);
    data_ = nullptr; handle_ = 0;
}
bool FlashRom::prepare(PlaylistFiles& files, const std::string& path) {
    reset(); error_.clear();
    const auto* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "gbcache");
    if (!partition) return fail("Flash ROM cache needs the USB partition update");
    auto file = files.openReader(path);
    std::string error;
    if (!GameBoy::validPath(path) || !file || !GameBoy::validate(*file, error)) return fail("Invalid ROM for flash cache");
    if (partition->size < Offset || file->size() > partition->size - Offset) return fail("ROM too large for flash cache; using SD");
    Header expected{Magic, file->size(), {}};
    if (!readHash(*file, expected.digest)) return fail("Cannot hash ROM from SD");
    auto map = [&] {
        const void* mapped = nullptr;
        if (esp_partition_mmap(partition, Offset, expected.size, ESP_PARTITION_MMAP_DATA, &mapped, &handle_) != ESP_OK) return false;
        data_ = static_cast<const uint8_t*>(mapped); return true;
    };
    Header existing{};
    if (esp_partition_read(partition, 0, &existing, sizeof(existing)) == ESP_OK &&
        !memcmp(&existing, &expected, sizeof(expected)) && map()) {
        if (mappedHash(data_, expected.size, expected.digest)) return true;
        reset();
    }
    // Invalidate metadata first. Power loss leaves only a rebuildable cache,
    // never an apparently valid partial ROM. No music/save partitions are touched.
    if (esp_partition_erase_range(partition, 0, 4096) != ESP_OK) return fail("Cannot erase ROM cache metadata");
    uint32_t eraseSize = (expected.size + 4095) & ~4095u;
    for (uint32_t at = 0; at < eraseSize; at += 0x10000) {
        if (esp_partition_erase_range(partition, Offset + at, std::min<uint32_t>(0x10000, eraseSize - at)) != ESP_OK) return fail("Cannot erase ROM cache");
        delay(1);
    }
    if (!file->seek(0)) return fail("Cannot seek ROM");
    uint8_t buffer[1024];
    for (uint32_t at = 0; at < expected.size;) {
        size_t count = std::min<uint32_t>(sizeof(buffer), expected.size - at);
        if (file->read(buffer, count) != count || esp_partition_write(partition, Offset + at, buffer, count) != ESP_OK) return fail("Cannot populate ROM cache");
        at += count; if (!(at % 4096)) delay(1);
    }
    file.reset();
    if (!map() || !mappedHash(data_, expected.size, expected.digest)) return fail("ROM cache verification failed");
    if (esp_partition_write(partition, 0, &expected, sizeof(expected)) != ESP_OK) return fail("Cannot finalize ROM cache");
    return true;
}
}
