#include "media.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>

class DiskReader : public ct::Reader {
public:
    explicit DiskReader(const std::filesystem::path& path) : file_(fopen(path.c_str(), "rb")), size_(std::filesystem::file_size(path)) { assert(file_); }
    ~DiskReader() override { fclose(file_); }
    size_t read(void* data, size_t count) override { largestRead = std::max(largestRead, count); return fread(data, 1, count, file_); }
    bool seek(uint32_t offset) override { return fseek(file_, offset, SEEK_SET) == 0; }
    uint32_t size() const override { return size_; }
    size_t largestRead = 0;
private:
    FILE* file_;
    uint32_t size_;
};

int main(int argc, char** argv) {
    assert(argc > 1);
    size_t count = 0, largestRead = 0, largestTags = 0;
    for (int i = 1; i < argc; ++i) for (auto& entry : std::filesystem::recursive_directory_iterator(argv[i])) {
        if (!entry.is_regular_file() || !ct::isMp3(entry.path().string())) continue;
        DiskReader reader(entry.path());
        auto tags = ct::readTags(reader);
        assert(reader.largestRead <= 512);
        largestRead = std::max(largestRead, reader.largestRead);
        largestTags = std::max(largestTags, tags.title.size() + tags.artist.size() + tags.album.size());
        ++count;
    }
    std::cout << "Parsed " << count << " MP3s; max read " << largestRead << ", max combined tags " << largestTags << " bytes\n";
}
