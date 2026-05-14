#pragma once

#include "sink.h"
#include <fstream>
#include <string>

class FileSink : public Sink {
public:
    explicit FileSink(const std::string& path);

    bool write(
        const uint8_t* data,
        size_t size) override;

    void flush() override;

private:
    std::ofstream out_;
};