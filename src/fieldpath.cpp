// fieldpath.cpp - Source 2 field-path Huffman codec implementation.
// C++20, Windows 11, MSVC.
#include "fieldpath.h"

#include <queue>
#include <string>
#include <vector>

namespace s2 {
namespace {

// Static Huffman weights Valve uses for the field-path operation alphabet.
// The order must match the FieldOp enum.
constexpr uint32_t kOpWeights[static_cast<size_t>(FieldOp::Count)] = {
    36271,  // PlusOne
    10334,  // PlusTwo
    1375,   // PlusThree
    646,    // PlusFour
    4128,   // PlusN
    35,     // PushOneLeftDeltaZeroRightZero
    3,      // PushOneLeftDeltaZeroRightNonZero
    521,    // PushOneLeftDeltaOneRightZero
    2942,   // PushOneLeftDeltaOneRightNonZero
    560,    // PushOneLeftDeltaNRightZero
    471,    // PushOneLeftDeltaNRightNonZero
    10530,  // PushOneLeftDeltaNRightNonZeroPack6Bits
    251,    // PushOneLeftDeltaNRightNonZeroPack8Bits
    0,      // PushTwoLeftDeltaZero
    0,      // PushTwoPack5LeftDeltaZero
    0,      // PushThreeLeftDeltaZero
    0,      // PushThreePack5LeftDeltaZero
    0,      // PushTwoLeftDeltaOne
    0,      // PushTwoPack5LeftDeltaOne
    0,      // PushThreeLeftDeltaOne
    0,      // PushThreePack5LeftDeltaOne
    0,      // PushTwoLeftDeltaN
    0,      // PushTwoPack5LeftDeltaN
    0,      // PushThreeLeftDeltaN
    0,      // PushThreePack5LeftDeltaN
    0,      // PushN
    310,    // PushNAndNonTopological
    2,      // PopOnePlusOne
    0,      // PopOnePlusN
    1837,   // PopAllButOnePlusOne
    149,    // PopAllButOnePlusN
    300,    // PopAllButOnePlusNPack3Bits
    634,    // PopAllButOnePlusNPack6Bits
    0,      // PopNPlusOne
    0,      // PopNPlusN
    1,      // PopNAndNonTopographical
    76,     // NonTopoComplex
    271,    // NonTopoPenultimatePlusOne
    99,     // NonTopoComplexPack4Bits
    25474,  // FieldPathEncodeFinish
};

const char* const kOpNames[static_cast<size_t>(FieldOp::Count)] = {
    "PlusOne",
    "PlusTwo",
    "PlusThree",
    "PlusFour",
    "PlusN",
    "PushOneLeftDeltaZeroRightZero",
    "PushOneLeftDeltaZeroRightNonZero",
    "PushOneLeftDeltaOneRightZero",
    "PushOneLeftDeltaOneRightNonZero",
    "PushOneLeftDeltaNRightZero",
    "PushOneLeftDeltaNRightNonZero",
    "PushOneLeftDeltaNRightNonZeroPack6Bits",
    "PushOneLeftDeltaNRightNonZeroPack8Bits",
    "PushTwoLeftDeltaZero",
    "PushTwoPack5LeftDeltaZero",
    "PushThreeLeftDeltaZero",
    "PushThreePack5LeftDeltaZero",
    "PushTwoLeftDeltaOne",
    "PushTwoPack5LeftDeltaOne",
    "PushThreeLeftDeltaOne",
    "PushThreePack5LeftDeltaOne",
    "PushTwoLeftDeltaN",
    "PushTwoPack5LeftDeltaN",
    "PushThreeLeftDeltaN",
    "PushThreePack5LeftDeltaN",
    "PushN",
    "PushNAndNonTopological",
    "PopOnePlusOne",
    "PopOnePlusN",
    "PopAllButOnePlusOne",
    "PopAllButOnePlusN",
    "PopAllButOnePlusNPack3Bits",
    "PopAllButOnePlusNPack6Bits",
    "PopNPlusOne",
    "PopNPlusN",
    "PopNAndNonTopographical",
    "NonTopoComplex",
    "NonTopoPenultimatePlusOne",
    "NonTopoComplexPack4Bits",
    "FieldPathEncodeFinish",
};

// Build-node used while assembling the Huffman tree.
struct BuildNode {
    uint32_t weight;
    int32_t value;
    int32_t left;
    int32_t right;
};

// Orders the construction queue so that the tree is built exactly like Valve's
// encoder: the smallest weight is merged first, and among equal weights the
// larger operation index is merged first.  std::priority_queue is a max-heap,
// so the comparison is inverted relative to a min-heap.
struct BuildCmp {
    bool operator()(const BuildNode& a, const BuildNode& b) const {
        if (a.weight != b.weight) return a.weight > b.weight;
        return a.value < b.value;
    }
};

}  // namespace

FieldPathCodec::FieldPathCodec() {
    // Leaf nodes occupy indices 0..39 (one per operation); internal nodes are
    // appended in merge order, so a node's index is also its tie-break value.
    nodes_.resize(static_cast<size_t>(FieldOp::Count));
    for (size_t i = 0; i < static_cast<size_t>(FieldOp::Count); ++i) {
        nodes_[i].left = -1;
        nodes_[i].right = -1;
        nodes_[i].value = static_cast<int32_t>(i);
        nodes_[i].weight = kOpWeights[i] == 0 ? 1u : kOpWeights[i];
    }

    std::priority_queue<BuildNode, std::vector<BuildNode>, BuildCmp> heap;
    for (size_t i = 0; i < static_cast<size_t>(FieldOp::Count); ++i) {
        BuildNode n;
        n.weight = nodes_[i].weight;
        n.value = static_cast<int32_t>(i);
        n.left = -1;
        n.right = -1;
        heap.push(n);
    }

    while (heap.size() > 1) {
        const BuildNode a = heap.top();
        heap.pop();
        const BuildNode b = heap.top();
        heap.pop();

        Node parent;
        parent.weight = a.weight + b.weight;
        parent.value = static_cast<int32_t>(nodes_.size());
        parent.left = a.value;
        parent.right = b.value;
        const int32_t parentIdx = parent.value;
        nodes_.push_back(parent);

        BuildNode n;
        n.weight = parent.weight;
        n.value = parentIdx;
        n.left = -1;
        n.right = -1;
        heap.push(n);
    }

    root_ = heap.top().value;
    heap.pop();

    // 8-bit lookup table: every code of at most 8 bits resolves in one lookup.
    fillLut(root_, 0, 0);
}

void FieldPathCodec::fillLut(int32_t nodeIdx, uint32_t code, uint8_t depth) {
    const Node& n = nodes_[static_cast<size_t>(nodeIdx)];
    if (n.left < 0) {
        // Leaf: replicate the entry across the unused high bits.
        const uint32_t mask = depth == 0 ? 0u : ((1u << depth) - 1u);
        for (uint32_t bits = 0; bits < 256u; ++bits) {
            if ((bits & mask) == code) {
                LutEntry& e = lut_[bits];
                e.op = static_cast<int16_t>(n.value);
                e.bitLen = depth;
                e.valid = true;
            }
        }
        return;
    }
    if (depth >= 8) return;
    fillLut(n.left, code, static_cast<uint8_t>(depth + 1));
    fillLut(n.right, code | (1u << depth), static_cast<uint8_t>(depth + 1));
}

bool FieldPathCodec::readOp(BitReader& r, FieldOp& out) {
    const uint32_t peeked = r.peekBits(8);
    const LutEntry& e = lut_[peeked & 0xFFu];
    if (e.valid) {
        r.skipBits(e.bitLen);
        out = static_cast<FieldOp>(e.op);
        return out != FieldOp::FieldPathEncodeFinish;
    }

    int32_t node = root_;
    for (int guard = 0; guard < 64; ++guard) {
        const bool bit = r.readBool();
        const Node& n = nodes_[static_cast<size_t>(node)];
        node = bit ? n.right : n.left;
        if (node < 0) break;
        const Node& child = nodes_[static_cast<size_t>(node)];
        if (child.left < 0) {
            out = static_cast<FieldOp>(child.value);
            return out != FieldOp::FieldPathEncodeFinish;
        }
    }
    out = FieldOp::FieldPathEncodeFinish;
    return false;
}

void FieldPathCodec::readPaths(BitReader& r, std::vector<FieldPath>& out) {
    FieldPath fp;
    for (int guard = 0; guard < 4096; ++guard) {
        FieldOp op;
        if (!readOp(r, op)) return;
        applyOp(op, r, fp);
        out.push_back(fp);
    }
}

const char* FieldPathCodec::opName(FieldOp op) {
    const size_t i = static_cast<size_t>(op);
    return i < static_cast<size_t>(FieldOp::Count) ? kOpNames[i] : "?";
}

void FieldPathCodec::applyOp(FieldOp op, BitReader& r, FieldPath& fp) {
    switch (op) {
        case FieldOp::PlusOne: fp.incCurr(1); break;
        case FieldOp::PlusTwo: fp.incCurr(2); break;
        case FieldOp::PlusThree: fp.incCurr(3); break;
        case FieldOp::PlusFour: fp.incCurr(4); break;
        case FieldOp::PlusN: fp.incCurr(static_cast<int32_t>(r.readUBitVarFp()) + 5); break;

        case FieldOp::PushOneLeftDeltaZeroRightZero: fp.push(0); break;
        case FieldOp::PushOneLeftDeltaZeroRightNonZero:
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushOneLeftDeltaOneRightZero:
            fp.incCurr(1);
            fp.push(0);
            break;
        case FieldOp::PushOneLeftDeltaOneRightNonZero:
            fp.incCurr(1);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushOneLeftDeltaNRightZero:
            fp.incCurr(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(0);
            break;
        case FieldOp::PushOneLeftDeltaNRightNonZero:
            fp.incCurr(static_cast<int32_t>(r.readUBitVarFp()) + 2);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()) + 1);
            break;
        case FieldOp::PushOneLeftDeltaNRightNonZeroPack6Bits:
            fp.incCurr(static_cast<int32_t>(r.readBits(3)) + 2);
            fp.push(static_cast<int32_t>(r.readBits(3)) + 1);
            break;
        case FieldOp::PushOneLeftDeltaNRightNonZeroPack8Bits:
            fp.incCurr(static_cast<int32_t>(r.readBits(4)) + 2);
            fp.push(static_cast<int32_t>(r.readBits(4)) + 1);
            break;

        case FieldOp::PushTwoLeftDeltaZero:
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushTwoPack5LeftDeltaZero:
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;
        case FieldOp::PushThreeLeftDeltaZero:
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushThreePack5LeftDeltaZero:
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;

        case FieldOp::PushTwoLeftDeltaOne:
            fp.incCurr(1);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushTwoPack5LeftDeltaOne:
            fp.incCurr(1);
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;
        case FieldOp::PushThreeLeftDeltaOne:
            fp.incCurr(1);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushThreePack5LeftDeltaOne:
            fp.incCurr(1);
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;

        case FieldOp::PushTwoLeftDeltaN:
            fp.incCurr(static_cast<int32_t>(r.readUBitVar()) + 2);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushTwoPack5LeftDeltaN:
            fp.incCurr(static_cast<int32_t>(r.readUBitVar()) + 2);
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;
        case FieldOp::PushThreeLeftDeltaN:
            fp.incCurr(static_cast<int32_t>(r.readUBitVar()) + 2);
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        case FieldOp::PushThreePack5LeftDeltaN:
            fp.incCurr(static_cast<int32_t>(r.readUBitVar()) + 2);
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            fp.push(static_cast<int32_t>(r.readBits(5)));
            break;

        case FieldOp::PushN: {
            const int32_t n = static_cast<int32_t>(r.readUBitVar());
            fp.incCurr(static_cast<int32_t>(r.readUBitVar()));
            for (int32_t i = 0; i < n; ++i) fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        }
        case FieldOp::PushNAndNonTopological: {
            for (int32_t i = 0; i <= fp.last; ++i) {
                if (r.readBool()) fp.inc(i, r.readVarI32() + 1);
            }
            const int32_t count = static_cast<int32_t>(r.readUBitVar());
            for (int32_t i = 0; i < count; ++i) fp.push(static_cast<int32_t>(r.readUBitVarFp()));
            break;
        }

        case FieldOp::PopOnePlusOne:
            fp.pop(1);
            fp.incCurr(1);
            break;
        case FieldOp::PopOnePlusN:
            fp.pop(1);
            fp.incCurr(static_cast<int32_t>(r.readUBitVarFp()) + 1);
            break;
        case FieldOp::PopAllButOnePlusOne:
            fp.pop(fp.last);
            fp.inc(0, 1);
            break;
        case FieldOp::PopAllButOnePlusN:
            fp.pop(fp.last);
            fp.inc(0, static_cast<int32_t>(r.readUBitVarFp()) + 1);
            break;
        case FieldOp::PopAllButOnePlusNPack3Bits:
            fp.pop(fp.last);
            fp.inc(0, static_cast<int32_t>(r.readBits(3)) + 1);
            break;
        case FieldOp::PopAllButOnePlusNPack6Bits:
            fp.pop(fp.last);
            fp.inc(0, static_cast<int32_t>(r.readBits(6)) + 1);
            break;
        case FieldOp::PopNPlusOne:
            fp.pop(static_cast<int32_t>(r.readUBitVarFp()));
            fp.incCurr(1);
            break;
        case FieldOp::PopNPlusN:
            fp.pop(static_cast<int32_t>(r.readUBitVarFp()));
            fp.incCurr(r.readVarI32());
            break;
        case FieldOp::PopNAndNonTopographical:
            fp.pop(static_cast<int32_t>(r.readUBitVarFp()));
            for (int32_t i = 0; i <= fp.last; ++i) {
                if (r.readBool()) fp.inc(i, r.readVarI32());
            }
            break;

        case FieldOp::NonTopoComplex:
            for (int32_t i = 0; i <= fp.last; ++i) {
                if (r.readBool()) fp.inc(i, r.readVarI32());
            }
            break;
        case FieldOp::NonTopoPenultimatePlusOne: fp.inc(fp.last - 1, 1); break;
        case FieldOp::NonTopoComplexPack4Bits:
            for (int32_t i = 0; i <= fp.last; ++i) {
                if (r.readBool()) {
                    fp.inc(i, static_cast<int32_t>(r.readBits(4)));
                    fp.sub(i, 7);
                }
            }
            break;
        case FieldOp::FieldPathEncodeFinish: break;
    }
}

}  // namespace s2
