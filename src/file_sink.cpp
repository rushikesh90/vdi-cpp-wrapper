#include "file_sink.h"

FileSink::FileSink(const std::string& path)
    : out_(path, std::ios::binary) {}

bool FileSink::write(
    const uint8_t* data,
    size_t size) {

    out_.write(
        reinterpret_cast<const char*>(data),
        size);

    return out_.good();
}

void FileSink::flush() {
    out_.flush();
}