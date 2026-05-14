#pragma once

#include <vector>
#include <queue>
#include <mutex>

class BufferPool {
public:
    BufferPool(
        size_t buffer_size,
        size_t count);

    uint8_t* acquire();
    void release(uint8_t* buffer);

private:
    std::vector<std::vector<uint8_t>> buffers_;
    std::queue<uint8_t*> free_buffers_;

    std::mutex mutex_;
};