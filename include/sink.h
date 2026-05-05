#pragma once
#include <cstddef>
#include <cstdint>

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const uint8_t* data, size_t size) = 0;
};

class NullSink : public Sink {
public:
    void write(const uint8_t* data, size_t size) override;
};