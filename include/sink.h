#pragma once

#include <cstdint>
#include <cstddef>

class Sink {
public:
    virtual ~Sink() = default;

    virtual bool write(
        const uint8_t* data,
        size_t size) = 0;

    virtual void flush() {}
};

class NullSink : public Sink {
public:
    bool write(const uint8_t*, size_t) override {
        return true;
    }
};