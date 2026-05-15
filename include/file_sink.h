#pragma once

#include "sink.h"
#include <windows.h>
#include <string>

class FileSink : public Sink {
public:
    explicit FileSink(const std::string& path);
    ~FileSink() override;

    bool write(
        const uint8_t* data,
        size_t size) override;

    void flush() override;

private:
    void flush_buffer();

    HANDLE file_handle_;
    uint8_t* write_buffer_;
    static constexpr size_t WRITE_BUFFER_SIZE = 1 * 1024 * 1024; // 1 MB
    size_t buffered_bytes_;
};