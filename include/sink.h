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

    // Returns true if the sink is open and ready to accept writes.
    // Used by VdiClient to check sink health before writing.
    virtual bool is_open() const { return true; }
};

class NullSink : public Sink {
public:
    bool write(const uint8_t*, size_t) override {
        return true;
    }
    bool is_open() const override { return true; }
};
