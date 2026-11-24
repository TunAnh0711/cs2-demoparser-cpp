// serializer.h - Source 2 flattened-serializer model: field types, value
// decoders, field-path naming and the entity value store.
// C++20, Windows 11, MSVC.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "bitreader.h"
#include "valve_messages.h"

namespace s2 {

// --------------------------------------------------------------- field paths

// A field path is a list of indices into the (possibly nested) serializer
// tree.  Seven levels is the maximum Valve encodes.
struct FieldPath {
    std::array<int32_t, 7> path{-1, 0, 0, 0, 0, 0, 0};
    int32_t last = 0;

    void push(int32_t v) {
        ++last;
        if (last >= 0 && last < static_cast<int32_t>(path.size())) path[last] = v;
    }
    void pop(int32_t n) {
        last -= n;
        if (last < 0) last = 0;
        if (last >= static_cast<int32_t>(path.size())) last = static_cast<int32_t>(path.size()) - 1;
    }
    void incCurr(int32_t v) {
        if (last >= 0 && last < static_cast<int32_t>(path.size())) path[last] += v;
    }
    void inc(int32_t n, int32_t v) {
        if (n >= 0 && n < static_cast<int32_t>(path.size())) path[n] += v;
    }
    void sub(int32_t n, int32_t v) {
        if (n >= 0 && n < static_cast<int32_t>(path.size())) path[n] -= v;
    }
    bool operator==(const FieldPath& o) const { return path == o.path && last == o.last; }
};

// --------------------------------------------------------------- field types

enum class Encoder {
    None,
    Coord,
    SimTime,
    RuneTime,
    Normal,
    Fixed8,
    Fixed64,
    QAnglePitchYaw,
    QAnglePrecise,
};

Encoder encoderFromString(const std::string& s);

struct FieldType {
    std::string base;
    std::shared_ptr<FieldType> generic;
    bool pointer = false;
    int32_t count = 0;
};

std::shared_ptr<FieldType> parseFieldType(const std::string& name);

// -------------------------------------------------------------------- values

struct Value {
    enum class Kind { None, Bool, Int, Float, String, Vector };

    Kind kind = Kind::None;
    bool b = false;
    int64_t i = 0;
    float f = 0.0f;
    std::array<float, 4> v{0.0f, 0.0f, 0.0f, 0.0f};
    std::string s;

    bool isNone() const { return kind == Kind::None; }
    float asFloat() const {
        switch (kind) {
            case Kind::Float: return f;
            case Kind::Int: return static_cast<float>(i);
            case Kind::Bool: return b ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }
    int64_t asInt() const {
        switch (kind) {
            case Kind::Int: return i;
            case Kind::Float: return static_cast<int64_t>(f);
            case Kind::Bool: return b ? 1 : 0;
            default: return 0;
        }
    }
    bool asBool() const {
        switch (kind) {
            case Kind::Bool: return b;
            case Kind::Int: return i != 0;
            case Kind::Float: return f != 0.0f;
            default: return false;
        }
    }
};

// ------------------------------------------------------------------ decoders

enum class ValueKind {
    Boolean,
    String,
    BinaryBlock,
    Signed8,
    Signed16,
    Signed32,
    Unsigned8,
    Unsigned16,
    Unsigned32,
    Unsigned64,
    Float32,
    QuantizedFloat,
    Vector,
    QAngle,
    CCSGameModeRules,
    Ammo,
};

// Quantized float decoder state, mirroring Valve's CNetworkedQuantizedFloat.
struct QuantFloat {
    uint32_t bitCount = 0;
    float low = 0.0f;
    float high = 0.0f;
    float highLowMul = 0.0f;
    float decMul = 0.0f;
    float offset = 0.0f;
    uint32_t flags = 0;

    static constexpr uint32_t kRoundDown = 1 << 0;
    static constexpr uint32_t kRoundUp = 1 << 1;
    static constexpr uint32_t kEncodeZero = 1 << 2;
    static constexpr uint32_t kEncodeInteger = 1 << 3;

    static QuantFloat create(int32_t bitCount, float low, float high, int32_t encoderFlags);
    float quantize(float v) const;
    float decode(BitReader& r) const;
};

struct Decoder {
    ValueKind kind = ValueKind::Unsigned32;
    Encoder encoder = Encoder::None;
    int32_t bitCount = 0;
    int32_t dims = 0;  // for ValueKind::Vector
    QuantFloat quant;
};

Value decodeValue(const Decoder& d, BitReader& r);

// ---------------------------------------------------------------- serializers

enum class FieldModelKind { Value, Array, ValueVector, Vector, Pointer };

struct Serializer;

struct Field {
    std::string varName;
    std::string sendNode;
    std::shared_ptr<FieldType> type;
    FieldModelKind model = FieldModelKind::Value;
    std::shared_ptr<Serializer> child;  // Vector / Pointer models
    Decoder decoder;
    Decoder elementDecoder;  // element decoder for ValueVector (CUtlVector) models
};

struct Serializer {
    std::string name;
    std::vector<std::shared_ptr<Field>> fields;
};

// Builds the serializer tree from a decoded CSVCMsg_FlattenedSerializer.
class SerializerRegistry {
public:
    void build(const FlattenedSerializerMsg& msg);

    const Serializer* find(const std::string& name) const;
    Serializer* findMutable(const std::string& name);
    std::shared_ptr<Serializer> get(const std::string& name) const;
    const std::unordered_map<std::string, std::shared_ptr<Serializer>>& serializers() const {
        return serializers_;
    }

    // Resolves a dotted field name (e.g. "m_pGameSceneNode.m_vecAbsOrigin")
    // into a field path inside `ser`.
    bool findPath(const Serializer& ser, const std::string& name, FieldPath& out) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Serializer>> serializers_;
    std::vector<std::shared_ptr<Field>> fields_;
};

// Builds the dotted name of a field path, e.g.
// "m_pGameSceneNode.m_vecAbsOrigin" or "m_hMyWeapons.0003".
std::string fieldPathName(const Serializer& ser, const FieldPath& fp);

// Returns the decoder used for a field path (values of the path itself).
const Decoder* fieldPathDecoder(const Serializer& ser, const FieldPath& fp);

}  // namespace s2
