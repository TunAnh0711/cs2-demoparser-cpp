// pbreader.h - minimal protobuf wire-format reader.
// C++20, Windows 11, MSVC. Used to decode Valve's demo/net messages without
// requiring protoc or a generated runtime.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s2 {
namespace pb {

enum WireType : uint32_t {
    kVarint = 0,
    kFixed64 = 1,
    kLengthDelimited = 2,
    kStartGroup = 3,
    kEndGroup = 4,
    kFixed32 = 5,
};

// Sequential reader over one protobuf message body.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
    explicit Reader(const std::vector<uint8_t>& v) : Reader(v.data(), v.size()) {}

    bool done() const { return p_ >= end_; }

    // Reads the next tag.  Returns false at end of buffer or on a malformed tag.
    bool nextTag(uint32_t& fieldNumber, uint32_t& wireType) {
        if (p_ >= end_) return false;
        const uint64_t tag = readVarint();
        fieldNumber = static_cast<uint32_t>(tag >> 3);
        wireType = static_cast<uint32_t>(tag & 7);
        if (fieldNumber == 0) return false;
        return true;
    }

    uint64_t readVarint() {
        uint64_t result = 0;
        uint32_t shift = 0;
        while (p_ < end_) {
            const uint8_t b = *p_++;
            result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return result;
            shift += 7;
            if (shift >= 64) break;
        }
        return result;
    }

    int64_t readZigZag() {
        const uint64_t ux = readVarint();
        return static_cast<int64_t>(ux >> 1) ^ -static_cast<int64_t>(ux & 1);
    }

    uint32_t readFixed32() {
        uint32_t v = 0;
        if (end_ - p_ >= 4) {
            std::memcpy(&v, p_, 4);
            p_ += 4;
        } else {
            p_ = end_;
        }
        return v;
    }

    uint64_t readFixed64() {
        uint64_t v = 0;
        if (end_ - p_ >= 8) {
            std::memcpy(&v, p_, 8);
            p_ += 8;
        } else {
            p_ = end_;
        }
        return v;
    }

    // Reads a length-delimited chunk.  Returns an empty view on truncation.
    bool readBytes(const uint8_t*& out, size_t& outSize) {
        const uint64_t len = readVarint();
        if (static_cast<uint64_t>(end_ - p_) < len) {
            out = p_;
            outSize = static_cast<size_t>(end_ - p_);
            p_ = end_;
            return false;
        }
        out = p_;
        outSize = static_cast<size_t>(len);
        p_ += len;
        return true;
    }

    std::string readString() {
        const uint8_t* data = nullptr;
        size_t size = 0;
        readBytes(data, size);
        return std::string(reinterpret_cast<const char*>(data), size);
    }

    std::vector<uint8_t> readByteVector() {
        const uint8_t* data = nullptr;
        size_t size = 0;
        readBytes(data, size);
        return std::vector<uint8_t>(data, data + size);
    }

    // Skips a field whose contents we do not model.
    void skipField(uint32_t wireType) {
        switch (wireType) {
            case kVarint: readVarint(); break;
            case kFixed64: readFixed64(); break;
            case kLengthDelimited: {
                const uint8_t* d = nullptr;
                size_t s = 0;
                readBytes(d, s);
                break;
            }
            case kFixed32: readFixed32(); break;
            default: p_ = end_; break;
        }
    }

private:
    const uint8_t* p_;
    const uint8_t* end_;
};

// Calls fn(fieldNumber, wireType, reader) for every field in the message.
// The reader is positioned at the field's value; the callback must consume it
// (or call skipField) so the outer loop can continue.
template <typename Fn>
void forEachField(const uint8_t* data, size_t size, Fn&& fn) {
    Reader r(data, size);
    uint32_t field = 0;
    uint32_t wire = 0;
    while (r.nextTag(field, wire)) {
        fn(field, wire, r);
    }
}

}  // namespace pb
}  // namespace s2
