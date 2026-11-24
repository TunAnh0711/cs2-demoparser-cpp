// demo_parser.cpp - Source 2 (CS2) demo container and entity-state parser.
// C++20, Windows 11, MSVC.
#include "demo_parser.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "pbreader.h"
#include "snappy.h"

namespace s2 {
namespace {

// demo.proto :: EDemoCommands
constexpr uint32_t kDemStop = 0;
constexpr uint32_t kDemFileHeader = 1;
constexpr uint32_t kDemFileInfo = 2;
constexpr uint32_t kDemSendTables = 4;
constexpr uint32_t kDemClassInfo = 5;
constexpr uint32_t kDemStringTables = 6;
constexpr uint32_t kDemPacket = 7;
constexpr uint32_t kDemSignonPacket = 8;
constexpr uint32_t kDemFullPacket = 13;
constexpr uint32_t kDemIsCompressed = 64;

// netmessages.proto :: SVC_Messages
constexpr uint32_t kSvcServerInfo = 40;
constexpr uint32_t kSvcFlattenedSerializer = 41;
constexpr uint32_t kSvcClassInfo = 42;
constexpr uint32_t kSvcCreateStringTable = 44;
constexpr uint32_t kSvcUpdateStringTable = 45;
constexpr uint32_t kSvcPacketEntities = 55;

uint32_t readVarint32(const uint8_t*& p, const uint8_t* end) {
    uint32_t result = 0;
    uint32_t shift = 0;
    while (p < end) {
        const uint8_t b = *p++;
        result |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 35) break;
    }
    return result;
}

}  // namespace

bool DemoParser::load(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open demo file: " + path;
        return false;
    }
    fileData_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (fileData_.size() < 16) {
        error = "file too small to be a CS2 demo";
        return false;
    }
    static const char kStamp[8] = {'P', 'B', 'D', 'E', 'M', 'S', '2', '\0'};
    if (std::memcmp(fileData_.data(), kStamp, 8) != 0) {
        error = "not a Source 2 demo (expected PBDEMS2 filestamp)";
        return false;
    }
    filePos_ = 16;
    // Source 2 uses a 14-bit entity slot index.  Keep a full slot table so an
    // update for a slot we have not seen created stays addressable.
    entities_.resize(8192);
    return true;
}

const StringTable* DemoParser::findStringTable(const std::string& name) const {
    auto it = stringTableIndex_.find(name);
    if (it == stringTableIndex_.end()) return nullptr;
    return &stringTables_[static_cast<size_t>(it->second)];
}

const Entity* DemoParser::entityByHandle(uint32_t handle) const {
    const uint32_t index = handle & 0x3FFFu;
    const uint32_t serial = handle >> 14;
    if (index >= entities_.size()) return nullptr;
    const Entity& e = entities_[index];
    if (!e.alive) return nullptr;
    if (serial != 0 && e.serial != serial) return nullptr;
    return &e;
}

const std::string& DemoParser::className(int32_t classId) const {
    static const std::string kUnknown = "<unknown>";
    auto it = classes_.find(classId);
    return it == classes_.end() ? kUnknown : it->second.networkName;
}

bool DemoParser::readContainerFrame(uint32_t& cmd, uint32_t& tick, uint32_t& size) {
    if (filePos_ >= fileData_.size()) return false;
    const uint8_t* p = fileData_.data() + filePos_;
    const uint8_t* end = fileData_.data() + fileData_.size();
    cmd = readVarint32(p, end);
    tick = readVarint32(p, end);
    filePos_ = static_cast<size_t>(p - fileData_.data());
    size = readVarint32(p, end);
    filePos_ = static_cast<size_t>(p - fileData_.data());
    return true;
}

bool DemoParser::parse(std::string& error) {
    if (fileData_.empty()) {
        error = "no demo loaded";
        return false;
    }

    std::vector<uint8_t> scratch;
    while (true) {
        uint32_t cmd = 0;
        uint32_t tick = 0;
        uint32_t size = 0;
        if (!readContainerFrame(cmd, tick, size)) break;

        const uint32_t msgType = cmd & ~kDemIsCompressed;
        const bool compressed = (cmd & kDemIsCompressed) != 0;
        if (msgType == kDemStop) break;

        tick_ = tick;
        ++stats_.frames;

        if (filePos_ + size > fileData_.size()) {
            error = "truncated demo frame";
            return false;
        }
        const uint8_t* payload = fileData_.data() + filePos_;
        filePos_ += size;

        if (compressed) {
            if (!snappyUncompress(payload, size, scratch)) {
                ++stats_.decompressFailures;
                error = "failed to decompress demo frame";
                return false;
            }
            dispatchCommand(msgType, scratch.data(), scratch.size());
        } else {
            dispatchCommand(msgType, payload, size);
        }
    }

    collectIdentities();
    return true;
}

void DemoParser::dispatchCommand(uint32_t cmd, const uint8_t* data, size_t size) {
    switch (cmd) {
        case kDemFileHeader:
            decodeDemoFileHeader(data, size, header_);
            break;
        case kDemFileInfo:
            decodeDemoFileInfo(data, size, fileInfo_);
            break;
        case kDemSendTables: {
            DemoSendTables st;
            decodeDemoSendTables(data, size, st);
            applySendTables(st.data);
            break;
        }
        case kDemClassInfo: {
            DemoClassInfo ci;
            decodeDemoClassInfo(data, size, ci);
            applyClassInfo(ci.classes);
            break;
        }
        case kDemStringTables: {
            DemoStringTables st;
            decodeDemoStringTables(data, size, st);
            applyStringTables(st);
            break;
        }
        case kDemPacket:
        case kDemSignonPacket: {
            DemoPacket pkt;
            decodeDemoPacket(data, size, pkt);
            handlePacket(pkt.data);
            break;
        }
        case kDemFullPacket: {
            DemoFullPacket fp;
            decodeDemoFullPacket(data, size, fp);
            ++stats_.fullPackets;
            if (fp.hasStringTable) applyStringTables(fp.stringTable);
            if (fp.hasPacket) handlePacket(fp.packet.data);
            break;
        }
        default:
            break;
    }
}

void DemoParser::applySendTables(const std::vector<uint8_t>& data) {
    // DEM_SendTables carries a length-prefixed CSVCMsg_FlattenedSerializer.
    if (data.empty()) return;
    const uint8_t* p = data.data();
    const uint8_t* end = data.data() + data.size();
    const uint32_t len = readVarint32(p, end);
    if (static_cast<size_t>(end - p) < len) return;

    FlattenedSerializerMsg msg;
    decodeFlattenedSerializer(p, len, msg);
    registry_.build(msg);
    for (auto& kv : classes_) {
        kv.second.serializer = registry_.get(kv.second.networkName);
    }
}

void DemoParser::applyClassInfo(const std::vector<DemoClassEntry>& list) {
    for (const DemoClassEntry& c : list) {
        ClassEntry entry;
        entry.classId = c.classId;
        entry.networkName = c.networkName;
        entry.serializer = registry_.get(c.networkName);
        classes_[c.classId] = entry;
    }
}

void DemoParser::applyStringTables(const DemoStringTables& tables) {
    for (const DemoStringTable& t : tables.tables) {
        auto it = stringTableIndex_.find(t.tableName);
        int32_t index;
        if (it == stringTableIndex_.end()) {
            index = static_cast<int32_t>(stringTables_.size());
            StringTable st;
            st.name = t.tableName;
            st.id = index;
            stringTables_.push_back(std::move(st));
            stringTableIndex_[t.tableName] = index;
        } else {
            index = it->second;
        }
        StringTable& st = stringTables_[static_cast<size_t>(index)];
        st.rows.clear();
        st.rows.reserve(t.items.size());
        for (const DemoStringTableItem& item : t.items) {
            StringTableRow row;
            row.key = item.str;
            row.value = item.data;
            st.rows.push_back(std::move(row));
        }
        if (st.name == "instancebaseline") {
            baselineBlobs_.clear();
            baselineStates_.clear();
            for (const StringTableRow& row : st.rows) {
                const int32_t classId = std::atoi(row.key.c_str());
                if (classId >= 0) baselineBlobs_[classId] = row.value;
            }
        }
    }
}

void DemoParser::createStringTable(const CreateStringTableMsg& msg) {
    StringTable st;
    st.name = msg.name;
    st.userDataFixedSize = msg.userDataFixedSize;
    st.userDataSize = msg.userDataSize;
    st.flags = msg.flags;
    st.usingVarintBitcounts = msg.usingVarintBitcounts;
    st.id = static_cast<int32_t>(stringTables_.size());

    std::vector<uint8_t> buffer;
    if (msg.dataCompressed) {
        if (!snappyUncompress(msg.stringData.data(), msg.stringData.size(), buffer)) {
            ++stats_.decompressFailures;
            return;
        }
    } else {
        buffer = msg.stringData;
    }

    const int32_t index = st.id;
    stringTables_.push_back(std::move(st));
    stringTableIndex_[stringTables_[static_cast<size_t>(index)].name] = index;
    parseStringTableData(stringTables_[static_cast<size_t>(index)], buffer.data(), buffer.size(),
                         msg.numEntries);
    ++stats_.stringTableUpdates;
}

void DemoParser::updateStringTable(const UpdateStringTableMsg& msg) {
    if (msg.tableId < 0 || static_cast<size_t>(msg.tableId) >= stringTables_.size()) return;
    StringTable& st = stringTables_[static_cast<size_t>(msg.tableId)];
    parseStringTableData(st, msg.stringData.data(), msg.stringData.size(), msg.numChangedEntries);
    ++stats_.stringTableUpdates;
}

void DemoParser::parseStringTableData(StringTable& table, const uint8_t* data, size_t size,
                                      int32_t numUpdates) {
    if (table.name == "decalprecache") return;

    BitReader r(data, size);
    int64_t index = -1;
    size_t deltaPos = 0;

    for (int32_t i = 0; i < numUpdates; ++i) {
        if (r.exhausted()) break;

        index += 1;
        if (!r.readBool()) index += static_cast<int64_t>(r.readVarU32()) + 1;
        if (index < 0) continue;

        bool hasKey = false;
        std::string key;
        if (r.readBool()) {
            hasKey = true;
            const uint32_t deltaZero = deltaPos > 32 ? static_cast<uint32_t>(deltaPos & 31) : 0u;
            if (r.readBool()) {
                const size_t pos = (deltaZero + r.readBits(5)) & 31;
                const size_t keySize = r.readBits(5);
                if (deltaPos < pos || table.keyHistory[pos].size() < keySize) {
                    key = r.readCString();
                } else {
                    key = table.keyHistory[pos].substr(0, keySize) + r.readCString();
                }
            } else {
                key = r.readCString();
            }
            table.keyHistory[deltaPos & 31] = key;
            deltaPos += 1;
        }

        bool hasValue = false;
        std::vector<uint8_t> value;
        if (r.readBool()) {
            hasValue = true;
            bool isCompressed = false;
            uint32_t bitSize = 0;
            if (table.userDataFixedSize) {
                bitSize = static_cast<uint32_t>(table.userDataSize);
            } else {
                if ((table.flags & 0x1) != 0) isCompressed = r.readBool();
                bitSize = table.usingVarintBitcounts ? r.readUBitVar() * 8 : r.readBits(17) * 8;
            }
            const size_t byteCount = bitSize / 8;
            const uint32_t extraBits = bitSize % 8;
            value = r.readBytes(byteCount);
            if (extraBits > 0) value.push_back(static_cast<uint8_t>(r.readBits(extraBits)));
            if (isCompressed) {
                std::vector<uint8_t> plain;
                if (snappyUncompress(value.data(), value.size(), plain)) value.swap(plain);
            }
        }

        if (static_cast<size_t>(index) >= table.rows.size()) {
            table.rows.resize(static_cast<size_t>(index) + 1);
        }
        StringTableRow& row = table.rows[static_cast<size_t>(index)];
        if (hasKey) row.key = key;
        if (hasValue) row.value = value;

        if (table.name == "instancebaseline") {
            const int32_t classId = std::atoi(row.key.c_str());
            if (classId >= 0) {
                baselineBlobs_[classId] = row.value;
                baselineStates_.erase(classId);
            }
        }
    }
}

void DemoParser::handlePacket(const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    ++stats_.packets;

    BitReader r(data.data(), data.size());
    std::vector<uint8_t> message;
    while (!r.exhausted()) {
        const uint32_t msgType = r.readUBitVar();
        const uint32_t size = r.readVarU32();
        if (size > r.size()) break;
        message = r.readBytes(size);
        handleNetMessage(msgType, message.data(), message.size());
    }
}

void DemoParser::handleNetMessage(uint32_t msgType, const uint8_t* data, size_t size) {
    switch (msgType) {
        case kSvcServerInfo: {
            ServerInfoMsg info;
            decodeServerInfo(data, size, info);
            if (info.maxClasses > 0) {
                classIdSize_ = static_cast<int32_t>(
                    std::log2(static_cast<double>(info.maxClasses) + 1.0) + 0.5);
            }
            break;
        }
        case kSvcFlattenedSerializer: {
            FlattenedSerializerMsg msg;
            decodeFlattenedSerializer(data, size, msg);
            registry_.build(msg);
            for (auto& kv : classes_) {
                kv.second.serializer = registry_.get(kv.second.networkName);
            }
            break;
        }
        case kSvcClassInfo: {
            SvcClassInfoMsg msg;
            decodeSvcClassInfo(data, size, msg);
            applyClassInfo(msg.classes);
            break;
        }
        case kSvcCreateStringTable: {
            CreateStringTableMsg msg;
            decodeCreateStringTable(data, size, msg);
            createStringTable(msg);
            break;
        }
        case kSvcUpdateStringTable: {
            UpdateStringTableMsg msg;
            decodeUpdateStringTable(data, size, msg);
            updateStringTable(msg);
            break;
        }
        case kSvcPacketEntities: {
            PacketEntitiesMsg msg;
            decodePacketEntities(data, size, msg);
            handlePacketEntities(msg);
            break;
        }
        default:
            break;
    }
}

void DemoParser::handlePacketEntities(const PacketEntitiesMsg& msg) {
    if (msg.entityData.empty()) return;
    ++stats_.entityUpdates;
    desynced_ = false;

    BitReader r(msg.entityData.data(), msg.entityData.size());
    // CS2 sends one varint per entity in `serialized_entities`: the exact bit
    // length of that entity's field update.  It is authoritative and is used
    // to stay aligned even for classes this parser does not model.
    BitReader offsets(msg.serializedEntities.data(), msg.serializedEntities.size());
    int64_t index = -1;

    for (int32_t i = 0; i < msg.updatedEntries && !r.exhausted(); ++i) {
        index += static_cast<int64_t>(r.readUBitVar()) + 1;
        if (index < 0 || static_cast<size_t>(index) >= entities_.size()) {
            desynced_ = true;
            return;
        }
        const uint32_t cmd = r.readBits(2);
        if (cmd == 1) continue;  // LeavePVS: no field data

        uint32_t expectedBits = 0;
        if (cmd == 0 || cmd == 2) expectedBits = offsets.readVarU32();

        if (cmd == 2) {  // Created
            const int32_t classId =
                classIdSize_ > 0
                    ? static_cast<int32_t>(r.readBits(static_cast<uint32_t>(classIdSize_)))
                    : 0;
            const uint32_t serial = r.readBits(17);
            r.readVarU32();  // unused spawn serial

            auto clsIt = classes_.find(classId);
            if (clsIt == classes_.end() || !clsIt->second.serializer) {
                desynced_ = true;
                return;
            }
            Entity& entity = entities_[static_cast<size_t>(index)];
            entity.index = static_cast<int32_t>(index);
            entity.serial = serial;
            entity.classId = classId;
            entity.className = clsIt->second.networkName;
            entity.alive = true;
            entity.fields.clear();

            auto baselineIt = baselineStates_.find(classId);
            if (baselineIt == baselineStates_.end()) {
                std::unordered_map<std::string, Value> baseline;
                auto blobIt = baselineBlobs_.find(classId);
                if (blobIt != baselineBlobs_.end() && !blobIt->second.empty()) {
                    Entity scratch;
                    BitReader br(blobIt->second.data(), blobIt->second.size());
                    readEntityFields(br, scratch, *clsIt->second.serializer);
                    baseline = std::move(scratch.fields);
                }
                baselineIt = baselineStates_.emplace(classId, std::move(baseline)).first;
            }
            entity.fields = baselineIt->second;

            const size_t fieldsStart = r.bitPosition();
            readEntityFields(r, entity, *clsIt->second.serializer);
            if (expectedBits > 0) r.seekBits(fieldsStart + expectedBits);
            ++stats_.entitiesCreated;
        } else if (cmd == 0) {  // Updated
            Entity& entity = entities_[static_cast<size_t>(index)];
            auto clsIt = classes_.find(entity.classId);
            if (clsIt == classes_.end() || !clsIt->second.serializer) {
                // Entity created before this parse started; the length table
                // still lets us consume its update without losing alignment.
                r.skipBits(expectedBits);
                continue;
            }
            const size_t fieldsStart = r.bitPosition();
            readEntityFields(r, entity, *clsIt->second.serializer);
            if (expectedBits > 0) r.seekBits(fieldsStart + expectedBits);
            ++stats_.entitiesUpdated;
        } else {  // Deleted
            entities_[static_cast<size_t>(index)].alive = false;
            ++stats_.entitiesDeleted;
        }
    }

    if (tickHandler_) tickHandler_(tick_, entities_);
}

void DemoParser::readEntityFields(BitReader& r, Entity& entity, const Serializer& serializer) {
    std::vector<FieldPath> paths;
    codec_.readPaths(r, paths);

    for (const FieldPath& fp : paths) {
        const Decoder* dec = fieldPathDecoder(serializer, fp);
        if (dec == nullptr) break;  // caller snaps to the authoritative length
        const Value v = decodeValue(*dec, r);
        const std::string name = fieldPathName(serializer, fp);
        if (!name.empty()) entity.fields[name] = v;
    }
}

void DemoParser::collectIdentities() {
    identities_.clear();
    const StringTable* userinfo = findStringTable("userinfo");
    if (userinfo == nullptr) return;

    for (const StringTableRow& row : userinfo->rows) {
        if (row.value.empty()) continue;
        PlayerIdentity id;
        pb::forEachField(row.value.data(), row.value.size(),
                         [&](uint32_t field, uint32_t wire, pb::Reader& r) {
                             switch (field) {
                                 case 1: id.name = r.readString(); break;
                                 case 2: id.xuid = r.readFixed64(); break;
                                 case 3: id.userId = static_cast<int32_t>(r.readVarint()); break;
                                 case 4: id.steamId = r.readFixed64(); break;
                                 case 5: id.fake = r.readVarint() != 0; break;
                                 case 6: id.hltv = r.readVarint() != 0; break;
                                 default: r.skipField(wire); break;
                             }
                         });
        identities_.push_back(std::move(id));
    }
}

}  // namespace s2
