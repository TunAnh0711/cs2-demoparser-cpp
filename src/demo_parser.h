// demo_parser.h - Source 2 (CS2) demo container and entity-state parser.
// C++20, Windows 11, MSVC.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "fieldpath.h"
#include "serializer.h"
#include "valve_messages.h"

namespace s2 {

// A networked entity with its decoded field values, keyed by dotted field name.
struct Entity {
    int32_t index = -1;
    uint32_t serial = 0;
    int32_t classId = -1;
    std::string className;
    bool alive = false;
    std::unordered_map<std::string, Value> fields;

    const Value* find(const std::string& name) const {
        auto it = fields.find(name);
        return it == fields.end() ? nullptr : &it->second;
    }
};

struct StringTableRow {
    std::string key;
    std::vector<uint8_t> value;
};

struct StringTable {
    std::string name;
    int32_t id = 0;
    bool userDataFixedSize = false;
    int32_t userDataSize = 0;
    int32_t flags = 0;
    bool usingVarintBitcounts = false;
    std::vector<StringTableRow> rows;
    std::array<std::string, 32> keyHistory{};
};

struct DemoStats {
    uint32_t frames = 0;
    uint32_t packets = 0;
    uint32_t entityUpdates = 0;
    uint32_t entitiesCreated = 0;
    uint32_t entitiesUpdated = 0;
    uint32_t entitiesDeleted = 0;
    uint32_t stringTableUpdates = 0;
    uint32_t fullPackets = 0;
    uint32_t decompressFailures = 0;
};

// Player identity recovered from the `userinfo` string table.
struct PlayerIdentity {
    std::string name;
    uint64_t xuid = 0;
    uint64_t steamId = 0;
    int32_t userId = -1;
    bool fake = false;
    bool hltv = false;
};

class DemoParser {
public:
    using TickHandler = std::function<void(uint32_t tick, const std::vector<Entity>& entities)>;

    bool load(const std::string& path, std::string& error);
    void setTickHandler(TickHandler handler) { tickHandler_ = std::move(handler); }
    bool parse(std::string& error);

    const DemoFileHeader& header() const { return header_; }
    const DemoFileInfo& fileInfo() const { return fileInfo_; }
    const DemoStats& stats() const { return stats_; }
    const std::vector<Entity>& entities() const { return entities_; }
    const std::vector<StringTable>& stringTables() const { return stringTables_; }
    const SerializerRegistry& serializers() const { return registry_; }
    uint32_t tick() const { return tick_; }
    int32_t classIdSize() const { return classIdSize_; }
    const std::string& mapName() const { return header_.mapName; }

    const StringTable* findStringTable(const std::string& name) const;

    // Resolves an entity handle: (serial << 14) | index.
    const Entity* entityByHandle(uint32_t handle) const;

    // Decoded CMsgPlayerInfo blobs from the `userinfo` string table.
    const std::vector<PlayerIdentity>& playerIdentities() const { return identities_; }

    // Human readable name for a networked class, e.g. "CCSPlayerPawn".
    const std::string& className(int32_t classId) const;

private:
    struct ClassEntry {
        int32_t classId = -1;
        std::string networkName;
        std::shared_ptr<Serializer> serializer;
    };

    bool readContainerFrame(uint32_t& cmd, uint32_t& tick, uint32_t& size);
    void dispatchCommand(uint32_t cmd, const uint8_t* data, size_t size);
    void handlePacket(const std::vector<uint8_t>& data);
    void handleNetMessage(uint32_t msgType, const uint8_t* data, size_t size);
    void handlePacketEntities(const PacketEntitiesMsg& msg);
    void applySendTables(const std::vector<uint8_t>& data);
    void applyClassInfo(const std::vector<DemoClassEntry>& classes);
    void applyStringTables(const DemoStringTables& tables);
    void createStringTable(const CreateStringTableMsg& msg);
    void updateStringTable(const UpdateStringTableMsg& msg);
    void parseStringTableData(StringTable& table, const uint8_t* data, size_t size, int32_t numUpdates);
    void readEntityFields(BitReader& r, Entity& entity, const Serializer& serializer);
    void collectIdentities();

    std::vector<uint8_t> fileData_;
    size_t filePos_ = 0;

    DemoFileHeader header_;
    DemoFileInfo fileInfo_;
    DemoStats stats_;
    SerializerRegistry registry_;
    FieldPathCodec codec_;

    std::vector<Entity> entities_;
    std::vector<StringTable> stringTables_;
    std::unordered_map<std::string, int32_t> stringTableIndex_;
    std::unordered_map<int32_t, ClassEntry> classes_;
    std::unordered_map<int32_t, std::unordered_map<std::string, Value>> baselineStates_;
    std::unordered_map<int32_t, std::vector<uint8_t>> baselineBlobs_;
    std::vector<PlayerIdentity> identities_;

    uint32_t tick_ = 0;
    int32_t classIdSize_ = 0;
    uint32_t lastFullPacketTick_ = 0xFFFFFFFFu;
    bool desynced_ = false;
    TickHandler tickHandler_;
};

}  // namespace s2
