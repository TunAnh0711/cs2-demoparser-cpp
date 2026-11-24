// snappy.h - Snappy "raw" block decompressor.
// C++20, Windows 11, MSVC.
//
// Valve compresses demo frames flagged with DEM_IsCompressed and string-table
// payloads flagged with data_compressed using Snappy's raw block format.
#pragma once

#include <cstdint>
#include <vector>

namespace s2 {

// Returns false on malformed input.
bool snappyUncompress(const uint8_t* src, size_t srcSize, std::vector<uint8_t>& out);

}  // namespace s2
