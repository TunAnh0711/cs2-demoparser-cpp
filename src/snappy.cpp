// snappy.cpp - Snappy "raw" block decompressor implementation.
// C++20, Windows 11, MSVC.
#include "snappy.h"

#include <cstring>

namespace s2 {
namespace {

bool readVarint32(const uint8_t*& p, const uint8_t* end, uint32_t& out) {
    uint32_t result = 0;
    int shift = 0;
    while (p < end) {
        const uint8_t b = *p++;
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            out = result;
            return true;
        }
        shift += 7;
        if (shift > 28) return false;
    }
    return false;
}

}  // namespace

bool snappyUncompress(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out) {
    out.clear();
    if (src == nullptr || srcSize == 0) return false;

    const uint8_t* p = src;
    const uint8_t* end = src + srcSize;

    uint32_t expected = 0;
    if (!readVarint32(p, end, expected)) return false;
    out.reserve(expected);

    while (p < end) {
        const uint8_t tag = *p++;
        switch (tag & 0x03) {
            case 0: {  // literal
                uint32_t len = tag >> 2;
                if (len < 60) {
                    len += 1;
                } else {
                    const uint32_t extra = len - 59;
                    if (static_cast<size_t>(end - p) < extra) return false;
                    uint32_t v = 0;
                    for (uint32_t i = 0; i < extra; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
                    p += extra;
                    len = v + 1;
                }
                if (static_cast<size_t>(end - p) < len) return false;
                out.insert(out.end(), p, p + len);
                p += len;
                break;
            }
            case 1: {  // copy, 1-byte offset
                if (end - p < 1) return false;
                const uint32_t len = 4 + ((tag >> 2) & 0x07);
                const uint32_t offset = (static_cast<uint32_t>(tag >> 5) << 8) | *p++;
                if (offset == 0 || offset > out.size()) return false;
                size_t srcIdx = out.size() - offset;
                for (uint32_t i = 0; i < len; ++i) out.push_back(out[srcIdx + i]);
                break;
            }
            case 2: {  // copy, 2-byte offset
                if (end - p < 2) return false;
                const uint32_t len = 1 + (tag >> 2);
                const uint32_t offset =
                    static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
                p += 2;
                if (offset == 0 || offset > out.size()) return false;
                size_t srcIdx = out.size() - offset;
                for (uint32_t i = 0; i < len; ++i) out.push_back(out[srcIdx + i]);
                break;
            }
            default: {  // copy, 4-byte offset
                if (end - p < 4) return false;
                const uint32_t len = 1 + (tag >> 2);
                const uint32_t offset = static_cast<uint32_t>(p[0]) |
                                        (static_cast<uint32_t>(p[1]) << 8) |
                                        (static_cast<uint32_t>(p[2]) << 16) |
                                        (static_cast<uint32_t>(p[3]) << 24);
                p += 4;
                if (offset == 0 || offset > out.size()) return false;
                size_t srcIdx = out.size() - offset;
                for (uint32_t i = 0; i < len; ++i) out.push_back(out[srcIdx + i]);
                break;
            }
        }
    }

    return true;
}

}  // namespace s2
