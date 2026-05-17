#include "file_sink.h"
#include <cstdlib>

FileSink::FileSink(const std::string& path)
    : file_handle_(INVALID_HANDLE_VALUE),
      write_buffer_(nullptr),
      buffered_bytes_(0) {

    // Convert UTF-8 path to UTF-16 wide string
    int wide_len = MultiByteToWideChar(
        CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wide_len <= 0) {
        return;
    }

    std::wstring wpath(static_cast<size_t>(wide_len), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, path.c_str(), -1, &wpath[0], wide_len);

    // Open file with sequential scan hint for better caching
    file_handle_ = CreateFileW(
        wpath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    // Allocate aligned buffer (Windows file I/O benefits from alignment)
    write_buffer_ = static_cast<uint8_t*>(
        _aligned_malloc(WRITE_BUFFER_SIZE, 4096));
}

FileSink::~FileSink() {
    flush_buffer();

    if (file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_handle_);
    }

    if (write_buffer_) {
        _aligned_free(write_buffer_);
    }
}

bool FileSink::write(
    const uint8_t* data,
    size_t size) {

    if (file_handle_ == INVALID_HANDLE_VALUE || !write_buffer_) {
        return false;
    }

    // If this single chunk is larger than the buffer, flush buffer
    // and write directly to avoid an extra memcpy of a huge chunk
    if (size >= WRITE_BUFFER_SIZE) {
        // Drain any buffered data first
        if (buffered_bytes_ > 0) {
            flush_buffer();
        }
        // Write the large chunk directly
        DWORD bytes_written = 0;
        if (!WriteFile(file_handle_, data,
                       static_cast<DWORD>(size), &bytes_written, nullptr)) {
            return false;
        }
        return bytes_written == size;
    }

    // If this chunk doesn't fit in remaining buffer space, flush first
    if (buffered_bytes_ + size > WRITE_BUFFER_SIZE) {
        flush_buffer();
    }

    // Copy into buffer
    memcpy(write_buffer_ + buffered_bytes_, data, size);
    buffered_bytes_ += size;

    return true;
}

bool FileSink::is_open() const {
    return file_handle_ != INVALID_HANDLE_VALUE;
}

void FileSink::flush() {
    flush_buffer();
    if (file_handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(file_handle_);
    }
}

void FileSink::flush_buffer() {
    if (buffered_bytes_ == 0) {
        return;
    }

    DWORD bytes_written = 0;
    WriteFile(file_handle_, write_buffer_,
              static_cast<DWORD>(buffered_bytes_), &bytes_written, nullptr);

    buffered_bytes_ = 0;
}