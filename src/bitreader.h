// bitreader.h - LSB-first bitstream reader for Source 2 / CS2 demo data.
// C++20, Windows 11, MSVC. Freestanding: no external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace s2 {

// Valve's Source 2 bitstream packs bits from the low bit of each byte upwards
// and assembles multi-bit reads with the first-consumed bit in the LSB of the
// result.  Both the Go (gobitread) and Rust (bitter) reference readers agree
// on this layout, so it is what this class reproduces.
//
//   bit index:  0 1 2 3 4 5 6 7   8 9 ...
//   byte:      [b0 b1 b2 ... b7][b0 b1 ...]
//
// A 32-bit read of 0x12345678 at an aligned offset yields 0x78563412.
class BitReader {
public:
    BitReader() = default;
    BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    void reset(const uint8_t* data, size_t size) {
        data_ = data;
        size_ = size;
        bitPos_ = 0;
    }

    size_t bitPosition() const { return bitPos_; }
    size_t bytePosition() const { return (bitPos_ + 7) / 8; }
    size_t size() const { return size_; }
    bool exhausted() const { return bitPos_ >= size_ * 8; }
    bool byteAligned() const { return (bitPos_ & 7) == 0; }
    const uint8_t* data() const { return data_; }

    // Reads up to 32 bits.  Bits past the end of the buffer read as zero.
    uint32_t readBits(uint32_t n) {
        if (n == 0) return 0;
        if (n <= 32 && byteAligned() && (n & 7) == 0) {
            const size_t byteIdx = bitPos_ >> 3;
            uint32_t v = 0;
            for (uint32_t i = 0; i < n / 8; ++i) {
                const size_t idx = byteIdx + i;
                if (idx < size_) v |= static_cast<uint32_t>(data_[idx]) << (8 * i);
            }
            bitPos_ += n;
            return v;
        }

        uint32_t result = 0;
        uint32_t got = 0;
        while (got < n) {
            const size_t byteIdx = bitPos_ >> 3;
            if (byteIdx >= size_) break;
            const uint32_t bitOff = static_cast<uint32_t>(bitPos_ & 7);
            const uint32_t avail = 8u - bitOff;
            const uint32_t take = std::min(avail, n - got);

            uint64_t window = 0;
            const size_t availBytes = std::min<size_t>(8, size_ - byteIdx);
            std::memcpy(&window, data_ + byteIdx, availBytes);

            const uint32_t chunk = static_cast<uint32_t>((window >> bitOff) & ((1ull << take) - 1ull));
            result |= chunk << got;
            got += take;
            bitPos_ += take;
        }
        return result;
    }

    bool readBool() { return readBits(1) != 0; }

    // Reads n bits without advancing the cursor.
    uint32_t peekBits(uint32_t n) const {
        const size_t saved = bitPos_;
        BitReader copy = *this;
        const uint32_t v = copy.readBits(n);
        bitPos_ = saved;
        return v;
    }

    float readFloat32() {
        const uint32_t bits = readBits(32);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    uint64_t readU64LE() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(readBits(8)) << (8 * i);
        }
        return v;
    }

    // Valve's variable-length unsigned 32-bit integer (LEB128, max 5 bytes).
    uint32_t readVarU32() {
        uint32_t x = 0;
        uint32_t shift = 0;
        uint8_t byte;
        do {
            byte = static_cast<uint8_t>(readBits(8));
            x |= static_cast<uint32_t>(byte & 0x7F) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0 && shift != 35);
        return x;
    }

    uint64_t readVarU64() {
        uint64_t x = 0;
        uint32_t shift = 0;
        uint8_t byte;
        do {
            byte = static_cast<uint8_t>(readBits(8));
            x |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0 && shift < 64);
        return x;
    }

    // Zigzag-decoded signed 32-bit variable integer.
    int32_t readVarI32() {
        const uint32_t ux = readVarU32();
        if (ux & 1u) return ~static_cast<int32_t>(ux >> 1);
        return static_cast<int32_t>(ux >> 1);
    }

    // Valve's "UBitVar": 6-bit header followed by 0/4/8/28 extra bits.
    uint32_t readUBitVar() {
        static const uint32_t kCounts[4] = {0, 4, 8, 28};
        const uint32_t a = readBits(6);
        const uint32_t b = a >> 4;
        if (a == 0) return b;
        return (a & 15u) | (readBits(kCounts[b]) << 4);
    }

    // Valve's field-path UBitVar: up to 4 flag bits, then 2/4/10/17/31 bits.
    uint32_t readUBitVarFp() {
        static const uint32_t kCounts[5] = {2, 4, 10, 17, 31};
        uint32_t i = 0;
        while (i < 4 && !readBool()) ++i;
        return readBits(kCounts[i]);
    }

    // Quantized coordinate: 1-bit int flag, 1-bit fract flag, optional sign,
    // 14-bit integer part and 5-bit fractional part.
    float readCoordinate() {
        uint32_t intVal = readBits(1);
        uint32_t fractVal = readBits(1);
        if (intVal == 0 && fractVal == 0) return 0.0f;

        const uint32_t sign = readBits(1);
        if (intVal != 0) intVal = readBits(14) + 1;
        if (fractVal != 0) fractVal = readBits(5);

        float value = static_cast<float>(intVal) + static_cast<float>(fractVal) * (1.0f / 32.0f);
        return sign ? -value : value;
    }

    // Normalized float in [-1, 1] using 11 fractional bits plus a sign bit.
    float readNormal() {
        const bool isNeg = readBool();
        const float len = static_cast<float>(readBits(11)) / 2047.0f;
        return isNeg ? -len : len;
    }

    std::array<float, 3> readNormalVec3() {
        std::array<float, 3> v{0.0f, 0.0f, 0.0f};
        const bool x = readBool();
        const bool y = readBool();
        if (x) v[0] = readNormal();
        if (y) v[1] = readNormal();
        const float xy = v[0] * v[0] + v[1] * v[1];
        v[2] = (xy < 1.0f) ? std::sqrt(1.0f - xy) : 0.0f;
        if (readBool()) v[2] = -v[2];
        return v;
    }

    float readAngle(uint32_t n) {
        if (n == 0) return 0.0f;
        if (n > 31) n = 31;
        return static_cast<float>(readBits(n)) * 360.0f / static_cast<float>(1u << n);
    }

    // Zero-terminated string.
    std::string readCString(size_t maxLen = 1024) {
        std::string out;
        out.reserve(64);
        for (size_t i = 0; i < maxLen; ++i) {
            const char c = static_cast<char>(readBits(8));
            if (c == 0) break;
            out.push_back(c);
        }
        return out;
    }

    // Reads n bytes.  When the reader is byte aligned this is a straight copy;
    // otherwise every byte is assembled from the bit stream, matching Valve's
    // behaviour for net messages that start on an arbitrary bit boundary.
    void readBytes(uint8_t* dst, size_t n) {
        if (byteAligned()) {
            const size_t byteIdx = bitPos_ >> 3;
            const size_t avail = size_ > byteIdx ? size_ - byteIdx : 0;
            const size_t copy = std::min(avail, n);
            if (copy) std::memcpy(dst, data_ + byteIdx, copy);
            if (copy < n) std::memset(dst + copy, 0, n - copy);
            bitPos_ += n * 8;
            return;
        }
        for (size_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>(readBits(8));
    }

    std::vector<uint8_t> readBytes(size_t n) {
        std::vector<uint8_t> out(n);
        if (n) readBytes(out.data(), n);
        return out;
    }

    void seekBits(size_t pos) { bitPos_ = pos; }

    void skipBits(uint32_t n) { bitPos_ += n; }

    void skipBytes(uint32_t n) { skipBits(n * 8); }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    mutable size_t bitPos_ = 0;
};

}  // namespace s2
