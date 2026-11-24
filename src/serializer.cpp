// serializer.cpp - Source 2 flattened-serializer model implementation.
// C++20, Windows 11, MSVC.
#include "serializer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace s2 {

// --------------------------------------------------------------- encoders

Encoder encoderFromString(const std::string& s) {
    if (s == "coord") return Encoder::Coord;
    if (s == "simtime") return Encoder::SimTime;
    if (s == "runetime") return Encoder::RuneTime;
    if (s == "normal") return Encoder::Normal;
    if (s == "fixed8") return Encoder::Fixed8;
    if (s == "fixed64") return Encoder::Fixed64;
    if (s == "qangle_pitch_yaw") return Encoder::QAnglePitchYaw;
    if (s == "qangle_precise") return Encoder::QAnglePrecise;
    return Encoder::None;
}

// ------------------------------------------------------------- field types

std::shared_ptr<FieldType> parseFieldType(const std::string& name) {
    auto t = std::make_shared<FieldType>();
    size_t baseEnd = name.size();
    bool pointer = false;

    if (!name.empty() && name.back() == '*') {
        pointer = true;
        --baseEnd;
    } else if (name == "CBodyComponent" || name == "CLightComponent" ||
               name == "CPhysicsComponent" || name == "CRenderComponent" ||
               name == "CPlayerLocalData") {
        pointer = true;
    }

    const size_t openBracket = name.find('[');
    if (openBracket != std::string::npos) {
        const size_t closeBracket = name.find(']', openBracket);
        if (closeBracket != std::string::npos) {
            const std::string inner = name.substr(openBracket + 1, closeBracket - openBracket - 1);
            if (inner == "MAX_ITEM_STOCKS") {
                t->count = 8;
            } else if (inner == "MAX_ABILITY_DRAFT_ABILITIES") {
                t->count = 48;
            } else if (inner == "DOTA_ABILITY_DRAFT_HEROES_PER_GAME") {
                t->count = 10;
            } else {
                t->count = 0;
                for (char c : inner) {
                    if (c < '0' || c > '9') {
                        t->count = 0;
                        break;
                    }
                    t->count = t->count * 10 + (c - '0');
                }
            }
            baseEnd = openBracket;
        }
    }

    const size_t openAngle = name.find('<');
    if (openAngle != std::string::npos) {
        const size_t closeAngle = name.rfind('>');
        if (closeAngle != std::string::npos && closeAngle > openAngle) {
            t->generic = parseFieldType(name.substr(openAngle + 1, closeAngle - openAngle - 1));
            baseEnd = openAngle;
        }
    }

    t->base = name.substr(0, baseEnd);
    // trim
    while (!t->base.empty() && std::isspace(static_cast<unsigned char>(t->base.front())))
        t->base.erase(t->base.begin());
    while (!t->base.empty() && std::isspace(static_cast<unsigned char>(t->base.back())))
        t->base.pop_back();

    t->pointer = pointer;
    if (t->base == "char" && t->count == 0) t->base = "uint8";
    return t;
}

// ---------------------------------------------------------- quantized float

QuantFloat QuantFloat::create(int32_t bitCountIn, float lowIn, float highIn, int32_t encoderFlags) {
    QuantFloat d;
    d.bitCount = static_cast<uint32_t>(bitCountIn);
    d.low = lowIn;
    d.high = highIn;
    d.flags = static_cast<uint32_t>(encoderFlags);

    // validate_flags()
    if (d.flags != 0) {
        if ((d.low == 0.0f && (d.flags & kRoundDown)) ||
            (d.high == 0.0f && (d.flags & kRoundUp))) {
            d.flags &= ~kEncodeZero;
        }
        if (d.low == 0.0f && (d.flags & kEncodeZero)) {
            d.flags |= kRoundUp;
            d.flags &= ~kEncodeZero;
        }
        if (d.high == 0.0f && (d.flags & kEncodeZero)) {
            d.flags |= kRoundDown;
            d.flags &= ~kEncodeZero;
        }
        if (d.low > 0.0f || d.high < 0.0f) d.flags &= ~kEncodeZero;
        if (d.flags & kEncodeInteger) {
            d.flags &= ~(kRoundUp | kRoundDown | kEncodeZero);
        }
    }

    uint32_t steps = d.bitCount >= 32 ? 1u : (1u << d.bitCount);

    if (d.flags & kRoundDown) {
        d.offset = (d.high - d.low) / static_cast<float>(steps);
        d.high -= d.offset;
    } else if (d.flags & kRoundUp) {
        d.offset = (d.high - d.low) / static_cast<float>(steps);
        d.low += d.offset;
    }

    if (d.flags & kEncodeInteger) {
        const float sum = d.high + d.low;
        float lg = std::log2(sum);
        if (std::isnan(lg)) lg = 1.0f;
        if (lg < 1.0f) lg = 1.0f;
        const uint32_t delta = static_cast<uint32_t>(std::ceil(lg));
        const uint32_t range2 = 1u << delta;
        uint32_t bc = d.bitCount;
        while ((1ull << bc) <= static_cast<uint64_t>(range2)) ++bc;
        if (bc > d.bitCount) {
            d.bitCount = bc;
            steps = 1u << d.bitCount;
        }
        d.offset = static_cast<float>(range2) / static_cast<float>(steps);
        d.high = d.low + static_cast<float>(range2) - d.offset;
    }

    // assign_multipliers(steps)
    {
        const float range = d.high - d.low;
        const uint32_t high = d.bitCount == 32 ? 0xFFFFFFFEu : ((1u << d.bitCount) - 1u);
        float highMul;
        if (std::fabs(range) <= 0.0f) {
            highMul = static_cast<float>(high);
        } else {
            highMul = static_cast<float>(high) / range;
        }
        const double prod = static_cast<double>(highMul) * static_cast<double>(range);
        if (highMul * range > static_cast<float>(high) || prod > static_cast<double>(high)) {
            for (float mult : {0.9999f, 0.99f, 0.9f, 0.8f, 0.7f}) {
                highMul = static_cast<float>(high) / range * mult;
                const double p = static_cast<double>(highMul) * static_cast<double>(range);
                if (highMul * range <= static_cast<float>(high) && p <= static_cast<double>(high)) break;
            }
        }
        d.highLowMul = highMul;
        d.decMul = 1.0f / static_cast<float>(steps - 1);
    }

    if ((d.flags & kRoundDown) && d.quantize(d.low) == d.low) d.flags &= ~kRoundDown;
    if ((d.flags & kRoundUp) && d.quantize(d.high) == d.high) d.flags &= ~kRoundUp;
    if ((d.flags & kEncodeZero) && d.quantize(0.0f) == 0.0f) d.flags &= ~kEncodeZero;

    return d;
}

float QuantFloat::quantize(float v) const {
    if (v < low) return low;
    if (v > high) return high;
    const auto i = static_cast<uint32_t>(std::nearbyint((v - low) * highLowMul));
    return low + (high - low) * static_cast<float>(i) * decMul;
}

float QuantFloat::decode(BitReader& r) const {
    if (bitCount == 32 || bitCount == 0) return r.readFloat32();
    if ((flags & kRoundDown) && r.readBool()) return low;
    if ((flags & kRoundUp) && r.readBool()) return high;
    if ((flags & kEncodeZero) && r.readBool()) return 0.0f;
    return low + (high - low) * static_cast<float>(r.readBits(bitCount)) * decMul;
}

// ------------------------------------------------------------------ decoders

namespace {

Decoder decoderForType(const std::shared_ptr<FieldType>& type, Encoder encoder, int32_t bitCount,
                        float low, float high, int32_t flags) {
    Decoder d;
    d.encoder = encoder;
    d.bitCount = bitCount;
    // Valve's quantized-float parameters are also used by plain float fields
    // whose bit count is smaller than 32, so they are always prepared.
    d.quant = QuantFloat::create(bitCount, low, high, flags);

    const std::string& base = type->base;
    if (base == "bool" || base == "CBodyComponent" || base == "CPhysicsComponent" ||
        base == "CRenderComponent") {
        d.kind = ValueKind::Boolean;
    } else if (base == "char" || base == "CUtlString" || base == "CUtlSymbolLarge" ||
               base == "CGlobalSymbol") {
        d.kind = ValueKind::String;
    } else if (base == "CUtlBinaryBlock") {
        d.kind = ValueKind::BinaryBlock;
    } else if (base == "Vector" || base == "VectorWS") {
        d.kind = ValueKind::Vector;
        d.dims = 3;
    } else if (base == "Vector2D") {
        d.kind = ValueKind::Vector;
        d.dims = 2;
    } else if (base == "Vector4D" || base == "Quaternion") {
        d.kind = ValueKind::Vector;
        d.dims = 4;
    } else if (base == "QAngle") {
        d.kind = ValueKind::QAngle;
    } else if (base == "CNetworkedQuantizedFloat") {
        d.kind = ValueKind::QuantizedFloat;
    } else if (base == "float32" || base == "GameTime_t") {
        d.kind = ValueKind::Float32;
    } else if (base == "int8") {
        d.kind = ValueKind::Signed8;
    } else if (base == "int16") {
        d.kind = ValueKind::Signed16;
    } else if (base == "int32") {
        d.kind = ValueKind::Signed32;
    } else if (base == "uint8" || base == "BloodType") {
        d.kind = ValueKind::Unsigned8;
    } else if (base == "uint16") {
        d.kind = ValueKind::Unsigned16;
    } else if (base == "uint64" || base == "CStrongHandle" || base == "ResourceId_t") {
        d.kind = ValueKind::Unsigned64;
    } else if (encoder == Encoder::Fixed8) {
        d.kind = ValueKind::Unsigned8;
    } else {
        d.kind = ValueKind::Unsigned32;
    }
    return d;
}

float decodeFloatComponent(const Decoder& d, BitReader& r) {
    switch (d.encoder) {
        case Encoder::Coord: return r.readCoordinate();
        case Encoder::SimTime: return static_cast<float>(r.readVarU32()) / 30.0f;
        case Encoder::RuneTime: {
            const uint32_t bits = r.readBits(d.bitCount > 0 ? static_cast<uint32_t>(d.bitCount) : 32);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return f;
        }
        default:
            if (d.bitCount == 32) return r.readFloat32();
            return d.quant.decode(r);
    }
}

}  // namespace

Value decodeValue(const Decoder& d, BitReader& r) {
    Value v;
    switch (d.kind) {
        case ValueKind::Boolean:
            v.kind = Value::Kind::Bool;
            v.b = r.readBool();
            break;
        case ValueKind::String:
            v.kind = Value::Kind::String;
            v.s = r.readCString();
            break;
        case ValueKind::BinaryBlock: {
            const uint32_t n = r.readVarU32();
            const auto bytes = r.readBytes(n);
            v.kind = Value::Kind::String;
            v.s.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            break;
        }
        case ValueKind::Signed8:
            v.kind = Value::Kind::Int;
            v.i = (d.encoder == Encoder::Fixed8)
                      ? static_cast<int8_t>(r.readBits(8))
                      : static_cast<int8_t>(r.readVarI32());
            break;
        case ValueKind::Signed16:
            v.kind = Value::Kind::Int;
            v.i = static_cast<int16_t>(r.readVarI32());
            break;
        case ValueKind::Signed32:
            v.kind = Value::Kind::Int;
            v.i = r.readVarI32();
            break;
        case ValueKind::Unsigned8:
            v.kind = Value::Kind::Int;
            v.i = (d.encoder == Encoder::Fixed8) ? static_cast<uint8_t>(r.readBits(8))
                                                 : static_cast<uint8_t>(r.readVarU32());
            break;
        case ValueKind::Unsigned16:
            v.kind = Value::Kind::Int;
            v.i = static_cast<uint16_t>(r.readVarU32());
            break;
        case ValueKind::Unsigned32:
            v.kind = Value::Kind::Int;
            v.i = r.readVarU32();
            break;
        case ValueKind::Ammo:
            // Valve encodes the magazine count as an unsigned varint biased by
            // one: the wire value is (ammo + 1), so it has to be read as
            // unsigned and decremented.  A zero on the wire means "no ammo
            // left" and must not be decremented any further.
            v.kind = Value::Kind::Int;
            {
                const uint32_t raw = r.readVarU32();
                v.i = raw > 0 ? static_cast<int64_t>(raw - 1) : 0;
            }
            break;
        case ValueKind::Unsigned64:
            v.kind = Value::Kind::Int;
            v.i = (d.encoder == Encoder::Fixed64) ? static_cast<int64_t>(r.readU64LE())
                                                   : static_cast<int64_t>(r.readVarU64());
            break;
        case ValueKind::Float32:
            v.kind = Value::Kind::Float;
            v.f = decodeFloatComponent(d, r);
            break;
        case ValueKind::QuantizedFloat:
            v.kind = Value::Kind::Float;
            v.f = d.quant.decode(r);
            break;
        case ValueKind::Vector: {
            v.kind = Value::Kind::Vector;
            if (d.dims == 3 && d.encoder == Encoder::Normal) {
                const auto n = r.readNormalVec3();
                v.v[0] = n[0];
                v.v[1] = n[1];
                v.v[2] = n[2];
            } else {
                for (int i = 0; i < d.dims && i < 4; ++i) v.v[i] = decodeFloatComponent(d, r);
            }
            break;
        }
        case ValueKind::QAngle: {
            v.kind = Value::Kind::Vector;
            float out[3] = {0.0f, 0.0f, 0.0f};
            if (d.encoder == Encoder::QAnglePitchYaw) {
                const uint32_t n = d.bitCount > 0 ? static_cast<uint32_t>(d.bitCount) : 1;
                out[0] = r.readAngle(n);
                out[1] = r.readAngle(n);
            } else if (d.bitCount == 32) {
                out[0] = r.readFloat32();
                out[1] = r.readFloat32();
                out[2] = r.readFloat32();
            } else if (d.encoder == Encoder::QAnglePrecise) {
                const bool x = r.readBool();
                const bool y = r.readBool();
                const bool z = r.readBool();
                if (x) out[0] = r.readAngle(20);
                if (y) out[1] = r.readAngle(20);
                if (z) out[2] = r.readAngle(20);
            } else if (d.bitCount != 0) {
                const uint32_t n = static_cast<uint32_t>(d.bitCount);
                out[0] = r.readAngle(n);
                out[1] = r.readAngle(n);
                out[2] = r.readAngle(n);
            } else {
                const bool x = r.readBool();
                const bool y = r.readBool();
                const bool z = r.readBool();
                if (x) out[0] = r.readCoordinate();
                if (y) out[1] = r.readCoordinate();
                if (z) out[2] = r.readCoordinate();
            }
            v.v[0] = out[0];
            v.v[1] = out[1];
            v.v[2] = out[2];
            break;
        }
        case ValueKind::CCSGameModeRules:
            v.kind = Value::Kind::Bool;
            v.b = r.readBool();
            r.readUBitVar();
            break;
    }
    return v;
}

// --------------------------------------------------------------- serializers

namespace {

// Matches the beginning of `name` against a field's (sendNode.varName) name
// and returns the number of characters consumed, or 0 on mismatch.
size_t nameMatchLen(const Field& f, const std::string& name, size_t offset) {
    if (!f.sendNode.empty()) {
        size_t len = 0;
        if (name.compare(offset, f.sendNode.size(), f.sendNode) == 0) {
            len += f.sendNode.size();
            if (offset + len < name.size() && name[offset + len] == '.') {
                ++len;
                if (name.compare(offset + len, f.varName.size(), f.varName) == 0) {
                    return len + f.varName.size();
                }
            }
        }
    }
    if (name.compare(offset, f.varName.size(), f.varName) == 0) return f.varName.size();
    return 0;
}

void appendFieldName(const Field& f, std::string& out) {
    if (!f.sendNode.empty()) {
        out += f.sendNode;
        out += '.';
    }
    out += f.varName;
}

}  // namespace

void SerializerRegistry::build(const FlattenedSerializerMsg& msg) {
    serializers_.clear();
    fields_.clear();

    auto resolve = [&msg](int32_t sym) -> std::string {
        if (sym < 0 || static_cast<size_t>(sym) >= msg.symbols.size()) return std::string();
        return msg.symbols[static_cast<size_t>(sym)];
    };

    std::unordered_map<std::string, std::shared_ptr<FieldType>> typeCache;

    for (const FlattenedSerializer& s : msg.serializers) {
        const std::string serName = resolve(s.serializerNameSym);
        auto serializer = std::make_shared<Serializer>();
        serializer->name = serName;

        for (int32_t rawIdx : s.fieldsIndex) {
            if (rawIdx < 0 || static_cast<size_t>(rawIdx) >= msg.fields.size()) continue;
            const size_t i = static_cast<size_t>(rawIdx);

            if (i >= fields_.size()) {
                const FlattenedSerializerField& src = msg.fields[i];

                auto field = std::make_shared<Field>();
                const std::string varTypeStr = resolve(src.varTypeSym);
                const std::string varName = resolve(src.varNameSym);
                field->varName = varName;

                if (src.hasSendNode) {
                    const std::string sn = resolve(src.sendNodeSym);
                    if (!sn.empty() && sn != "(root)") field->sendNode = sn;
                }

                auto it = typeCache.find(varTypeStr);
                if (it == typeCache.end()) {
                    it = typeCache.emplace(varTypeStr, parseFieldType(varTypeStr)).first;
                }
                field->type = it->second;

                Encoder enc = Encoder::None;
                if (varName == "m_flSimulationTime" || varName == "m_flAnimTime") {
                    enc = Encoder::SimTime;
                } else if (varName == "m_flRuneTime") {
                    enc = Encoder::RuneTime;
                } else if (src.hasVarEncoder) {
                    enc = encoderFromString(resolve(src.varEncoderSym));
                }

                const std::string childName =
                    src.hasFieldSerializerName ? resolve(src.fieldSerializerNameSym) : std::string();
                std::shared_ptr<Serializer> childSer;
                auto childIt = serializers_.find(childName);
                if (!childName.empty() && childIt != serializers_.end()) childSer = childIt->second;

                const bool isVectorBase =
                    field->type->base == "CUtlVector" || field->type->base == "CNetworkUtlVectorBase" ||
                    field->type->base == "CUtlVectorEmbeddedNetworkVar";

                if (childSer) {
                    field->model = field->type->pointer ? FieldModelKind::Pointer : FieldModelKind::Vector;
                    field->child = childSer;
                } else if (isVectorBase && field->type->generic) {
                    field->model = FieldModelKind::ValueVector;
                } else if (field->type->count > 0 && field->type->base != "char") {
                    field->model = FieldModelKind::Array;
                } else {
                    field->model = FieldModelKind::Value;
                }

                Decoder dec;
                switch (field->model) {
                    case FieldModelKind::Value:
                    case FieldModelKind::Array:
                        dec = decoderForType(field->type, enc, src.bitCount, src.lowValue,
                                             src.highValue, src.encodeFlags);
                        break;
                    case FieldModelKind::Vector:
                        dec.kind = ValueKind::Unsigned32;
                        break;
                    case FieldModelKind::ValueVector:
                        dec.kind = ValueKind::Unsigned32;
                        field->elementDecoder =
                            decoderForType(field->type->generic, enc, src.bitCount, src.lowValue,
                                           src.highValue, src.encodeFlags);
                        break;
                    case FieldModelKind::Pointer:
                        dec.kind = ValueKind::Boolean;
                        break;
                }

                if (serName == "CCSGameModeRules" || varName == "m_pGameModeRules") {
                    dec.kind = ValueKind::CCSGameModeRules;
                    dec.encoder = Encoder::None;
                }

                // The magazine counter is transmitted with its own encoding
                // (unsigned varint biased by one) regardless of the declared
                // integer type, so it cannot be derived from the field type.
                if (varName == "m_iClip1") {
                    dec.kind = ValueKind::Ammo;
                    dec.encoder = Encoder::None;
                }

                field->decoder = dec;
                fields_.resize(i + 1);
                fields_[i] = field;
            }

            serializer->fields.push_back(fields_[i]);
        }

        serializers_[serName] = serializer;
    }
}

const Serializer* SerializerRegistry::find(const std::string& name) const {
    auto it = serializers_.find(name);
    return it == serializers_.end() ? nullptr : it->second.get();
}

Serializer* SerializerRegistry::findMutable(const std::string& name) {
    auto it = serializers_.find(name);
    return it == serializers_.end() ? nullptr : it->second.get();
}

std::shared_ptr<Serializer> SerializerRegistry::get(const std::string& name) const {
    auto it = serializers_.find(name);
    return it == serializers_.end() ? nullptr : it->second;
}

bool SerializerRegistry::findPath(const Serializer& ser, const std::string& name,
                                  FieldPath& out) const {
    const Serializer* currentSerializer = &ser;
    FieldPath fp;
    size_t offset = 0;

    for (;;) {
        bool matched = false;
        for (size_t i = 0; i < currentSerializer->fields.size(); ++i) {
            const Field& f = *currentSerializer->fields[i];
            const size_t fieldNameLen = nameMatchLen(f, name, offset);
            if (fieldNameLen == 0) continue;

            if (name.size() == offset + fieldNameLen) {
                fp.path[fp.last] = static_cast<int32_t>(i);
                out = fp;
                return true;
            }

            if (name[offset + fieldNameLen] == '.') {
                fp.path[fp.last] = static_cast<int32_t>(i);
                ++fp.last;
                offset += fieldNameLen + 1;

                if (f.model == FieldModelKind::Array || f.model == FieldModelKind::ValueVector) {
                    const int32_t idx = static_cast<int32_t>(std::strtol(name.c_str() + offset, nullptr, 10));
                    fp.path[fp.last] = idx;
                    out = fp;
                    return true;
                }
                if (f.model == FieldModelKind::Vector) {
                    const int32_t idx =
                        static_cast<int32_t>(std::strtol(name.c_str() + offset, nullptr, 10));
                    fp.path[fp.last] = idx;
                    ++fp.last;
                    offset += 5;  // "NNNN."
                    currentSerializer = f.child.get();
                    if (!currentSerializer) return false;
                    matched = true;
                    break;
                }
                if (f.model == FieldModelKind::Pointer) {
                    currentSerializer = f.child.get();
                    if (!currentSerializer) return false;
                    matched = true;
                    break;
                }
                return false;
            }
        }
        if (!matched) return false;
    }
}

std::string fieldPathName(const Serializer& ser, const FieldPath& fp) {
    std::string name;
    int32_t i = 0;
    const Serializer* currentSerializer = &ser;
    if (currentSerializer->fields.empty()) return name;
    if (fp.path[0] < 0 || static_cast<size_t>(fp.path[0]) >= currentSerializer->fields.size())
        return name;

    const Field* currentField = currentSerializer->fields[static_cast<size_t>(fp.path[0])].get();

    for (int guard = 0; guard < 16; ++guard) {
        appendFieldName(*currentField, name);
        ++i;

        if (currentField->model == FieldModelKind::Array ||
            currentField->model == FieldModelKind::ValueVector) {
            if (i > fp.last) break;
            char buf[16];
            std::snprintf(buf, sizeof(buf), ".%04d", fp.path[i]);
            name += buf;
            break;
        }
        if (currentField->model == FieldModelKind::Vector) {
            if (i > fp.last) break;
            char buf[16];
            std::snprintf(buf, sizeof(buf), ".%04d.", fp.path[i]);
            name += buf;
            ++i;
            currentSerializer = currentField->child.get();
            if (!currentSerializer) break;
        } else if (currentField->model == FieldModelKind::Pointer) {
            if (i > fp.last) break;
            name += '.';
            currentSerializer = currentField->child.get();
            if (!currentSerializer) break;
        } else {
            break;
        }

        if (i > fp.last) break;
        if (fp.path[i] < 0 || static_cast<size_t>(fp.path[i]) >= currentSerializer->fields.size())
            break;
        currentField = currentSerializer->fields[static_cast<size_t>(fp.path[i])].get();
    }
    return name;
}

const Decoder* fieldPathDecoder(const Serializer& ser, const FieldPath& fp) {
    const Serializer* currentSerializer = &ser;
    if (currentSerializer->fields.empty()) return nullptr;
    if (fp.path[0] < 0 || static_cast<size_t>(fp.path[0]) >= currentSerializer->fields.size())
        return nullptr;
    int32_t i = 0;
    const Field* currentField = currentSerializer->fields[static_cast<size_t>(fp.path[0])].get();

    for (int guard = 0; guard < 16; ++guard) {
        ++i;
        switch (currentField->model) {
            case FieldModelKind::Value:
            case FieldModelKind::Array:
                return &currentField->decoder;
            case FieldModelKind::ValueVector:
                if (i == fp.last) return &currentField->elementDecoder;
                return &currentField->decoder;
            case FieldModelKind::Vector:
                if (i >= fp.last) return &currentField->decoder;
                ++i;
                currentSerializer = currentField->child.get();
                if (!currentSerializer) return nullptr;
                break;
            case FieldModelKind::Pointer:
                if (i > fp.last) return &currentField->decoder;
                currentSerializer = currentField->child.get();
                if (!currentSerializer) return nullptr;
                break;
        }
        if (i > fp.last) return &currentField->decoder;
        if (fp.path[i] < 0 || static_cast<size_t>(fp.path[i]) >= currentSerializer->fields.size())
            return nullptr;
        currentField = currentSerializer->fields[static_cast<size_t>(fp.path[i])].get();
    }
    return nullptr;
}

}  // namespace s2
