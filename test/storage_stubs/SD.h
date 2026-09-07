#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/stat.h>

inline uint32_t millis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
inline void delay(unsigned) {}
inline constexpr const char* FILE_READ = "rb";
inline constexpr const char* FILE_WRITE = "wb";
inline constexpr const char* FILE_APPEND = "ab";
inline int failWriteAfter = -1;
class File {
    struct Handle {
        FILE* file;
        explicit Handle(FILE* f) : file(f) {}
        ~Handle() { if (file) fclose(file); }
    };
    std::shared_ptr<Handle> handle_;
public:
    File() = default;
    explicit File(FILE* f) { if (f) handle_ = std::make_shared<Handle>(f); }
    explicit operator bool() const { return handle_ && handle_->file; }
    void close() { handle_.reset(); }
    void flush() { if (*this) fflush(handle_->file); }
    uint32_t size() const { struct stat s{}; return *this && fstat(fileno(handle_->file), &s) == 0 ? s.st_size : 0; }
    bool isDirectory() const { struct stat s{}; return *this && fstat(fileno(handle_->file), &s) == 0 && S_ISDIR(s.st_mode); }
    bool seek(uint32_t pos) { return *this && fseek(handle_->file, pos, SEEK_SET) == 0; }
    size_t read(uint8_t* data, size_t size) { return *this ? fread(data, 1, size, handle_->file) : 0; }
    size_t write(const uint8_t* data, size_t size) {
        if (failWriteAfter == 0) return 0;
        if (failWriteAfter > 0) --failWriteAfter;
        return *this ? fwrite(data, 1, size, handle_->file) : 0;
    }
};
class FakeFS {
public:
    std::string root, failOpen, failRename;
    bool begin(bool = false) { return !root.empty(); }
    template<class SPI> bool begin(int, SPI&, int, const char* point) { root = point; return std::filesystem::is_directory(root); }
    File open(const char* path, const char* mode) {
        if (path == failOpen) return {};
        return File(fopen((root + path).c_str(), mode));
    }
    bool exists(const char* path) { return std::filesystem::exists(root + path); }
    bool mkdir(const char* path) { return exists(path) || std::filesystem::create_directories(root + path); }
    bool remove(const char* path) { return std::remove((root + path).c_str()) == 0; }
    bool rename(const char* from, const char* to) { return from != failRename && std::rename((root + from).c_str(), (root + to).c_str()) == 0; }
};
inline FakeFS SD;
