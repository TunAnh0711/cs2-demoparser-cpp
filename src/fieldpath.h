// fieldpath.h - Source 2 field-path Huffman codec.
// C++20, Windows 11, MSVC.
//
// Source 2 encodes the path of every networked field as a sequence of
// operations over a small stack.  The operations themselves are Huffman coded
// with the static weight table Valve ships in the game binary.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "bitreader.h"
#include "serializer.h"

namespace s2 {

enum class FieldOp : uint8_t {
    PlusOne = 0,
    PlusTwo,
    PlusThree,
    PlusFour,
    PlusN,
    PushOneLeftDeltaZeroRightZero,
    PushOneLeftDeltaZeroRightNonZero,
    PushOneLeftDeltaOneRightZero,
    PushOneLeftDeltaOneRightNonZero,
    PushOneLeftDeltaNRightZero,
    PushOneLeftDeltaNRightNonZero,
    PushOneLeftDeltaNRightNonZeroPack6Bits,
    PushOneLeftDeltaNRightNonZeroPack8Bits,
    PushTwoLeftDeltaZero,
    PushTwoPack5LeftDeltaZero,
    PushThreeLeftDeltaZero,
    PushThreePack5LeftDeltaZero,
    PushTwoLeftDeltaOne,
    PushTwoPack5LeftDeltaOne,
    PushThreeLeftDeltaOne,
    PushThreePack5LeftDeltaOne,
    PushTwoLeftDeltaN,
    PushTwoPack5LeftDeltaN,
    PushThreeLeftDeltaN,
    PushThreePack5LeftDeltaN,
    PushN,
    PushNAndNonTopological,
    PopOnePlusOne,
    PopOnePlusN,
    PopAllButOnePlusOne,
    PopAllButOnePlusN,
    PopAllButOnePlusNPack3Bits,
    PopAllButOnePlusNPack6Bits,
    PopNPlusOne,
    PopNPlusN,
    PopNAndNonTopographical,
    NonTopoComplex,
    NonTopoPenultimatePlusOne,
    NonTopoComplexPack4Bits,
    FieldPathEncodeFinish,
    Count,
};

class FieldPathCodec {
public:
    FieldPathCodec();

    // Reads one operation.  Returns false when the finish marker was read.
    bool readOp(BitReader& r, FieldOp& out);

    // Reads the whole operation list of a field update, appending the
    // resulting paths to `out`.
    void readPaths(BitReader& r, std::vector<FieldPath>& out);

    static const char* opName(FieldOp op);

private:
    struct Node {
        int32_t left = -1;
        int32_t right = -1;
        int32_t value = -1;
        uint32_t weight = 0;
    };

    struct LutEntry {
        int16_t op = -1;
        uint8_t bitLen = 0;
        bool valid = false;
    };

    static void applyOp(FieldOp op, BitReader& r, FieldPath& fp);
    void fillLut(int32_t nodeIdx, uint32_t code, uint8_t depth);

    std::vector<Node> nodes_;
    std::array<LutEntry, 256> lut_{};
    int32_t root_ = -1;
};

}  // namespace s2
