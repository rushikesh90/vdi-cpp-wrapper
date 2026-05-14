#include "buffer_pool.h"

BufferPool::BufferPool(
    size_t buffer_size,
    size_t count) {

    for (size_t i = 0; i < count; ++i) {

        buffers_.emplace_back(buffer_size);

        free_buffers_.push(
            buffers_.back().data());
    }
}

uint8_t* BufferPool::acquire() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (free_buffers_.empty()) {
        return nullptr;
    }

    auto* ptr = free_buffers_.front();
    free_buffers_.pop();

    return ptr;
}

void BufferPool::release(
    uint8_t* buffer) {

    std::lock_guard<std::mutex> lock(mutex_);

    free_buffers_.push(buffer);
}