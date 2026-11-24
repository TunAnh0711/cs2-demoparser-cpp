// valve_messages.h - decoded representations of the Valve protobuf messages the
// CS2 demo pipeline needs.
// C++20, Windows 11, MSVC.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace s2 {

// ---------------------------------------------------------------- demo.proto

struct DemoFileHeader {
    std::string demoFileStamp;
    int32_t patchVersion = 0;
    std::string serverName;
    std::string clientName;
    std::string mapName;
    std::string gameDirectory;
    std::string demoVersionName;
    std::string demoVersionGuid;
    int32_t buildNum = 0;
    std::string game;
    int32_t serverStartTick = 0;
};

struct DemoFileInfo {
    float playbackTime = 0.0f;
    int32_t playbackTicks = 0;
    int32_t playbackFrames = 0;
};

struct DemoPacket {
    std::vector<uint8_t> data;
};

struct DemoSendTables {
    std::vector<uint8_t> data;
};

struct DemoClassEntry {
    int32_t classId = 0;
    std::string networkName;
    std::string tableName;
};

struct DemoClassInfo {
    std::vector<DemoClassEntry> classes;
};

struct DemoStringTableItem {
    std::string str;
    std::vector<uint8_t> data;
};

struct DemoStringTable {
    std::string tableName;
    std::vector<DemoStringTableItem> items;
    int32_t tableFlags = 0;
};

struct DemoStringTables {
    std::vector<DemoStringTable> tables;
};

struct DemoFullPacket {
    bool hasStringTable = false;
    DemoStringTables stringTable;
    bool hasPacket = false;
    DemoPacket packet;
};

// ------------------------------------------------------------ netmessages.proto

struct FlattenedSerializerField {
    int32_t varTypeSym = 0;
    int32_t varNameSym = 0;
    int32_t bitCount = 0;
    float lowValue = 0.0f;
    float highValue = 0.0f;
    int32_t encodeFlags = 0;
    int32_t fieldSerializerNameSym = 0;
    int32_t fieldSerializerVersion = 0;
    int32_t sendNodeSym = 0;
    int32_t varEncoderSym = 0;
    int32_t varSerializerSym = 0;
    bool hasSendNode = false;
    bool hasVarEncoder = false;
    bool hasFieldSerializerName = false;
};

struct FlattenedSerializer {
    int32_t serializerNameSym = 0;
    int32_t serializerVersion = 0;
    std::vector<int32_t> fieldsIndex;
};

struct FlattenedSerializerMsg {
    std::vector<FlattenedSerializer> serializers;
    std::vector<std::string> symbols;
    std::vector<FlattenedSerializerField> fields;
};

struct PacketEntitiesMsg {
    int32_t maxEntries = 0;
    int32_t updatedEntries = 0;
    bool legacyIsDelta = false;
    bool updateBaseline = false;
    int32_t baseline = 0;
    int32_t deltaFrom = 0;
    std::vector<uint8_t> entityData;
    std::vector<uint8_t> serializedEntities;  // per-entity field-update bit lengths
    uint32_t serverTick = 0;
    uint32_t hasPvsVisBitsDeprecated = 0;
};

struct ServerInfoMsg {
    int32_t protocol = 0;
    int32_t maxClients = 0;
    int32_t maxClasses = 0;
    int32_t playerSlot = -1;
    float tickInterval = 0.0f;
    std::string gameDir;
    std::string mapName;
    std::string hostName;
};

struct CreateStringTableMsg {
    std::string name;
    int32_t numEntries = 0;
    bool userDataFixedSize = false;
    int32_t userDataSize = 0;
    int32_t flags = 0;
    std::vector<uint8_t> stringData;
    bool dataCompressed = false;
    bool usingVarintBitcounts = false;
};

struct UpdateStringTableMsg {
    int32_t tableId = 0;
    int32_t numChangedEntries = 0;
    std::vector<uint8_t> stringData;
};

struct SvcClassInfoMsg {
    std::vector<DemoClassEntry> classes;  // className carries the network name
};

// ------------------------------------------------------------------ decoders

void decodeDemoFileHeader(const uint8_t* data, size_t size, DemoFileHeader& out);
void decodeDemoFileInfo(const uint8_t* data, size_t size, DemoFileInfo& out);
void decodeDemoPacket(const uint8_t* data, size_t size, DemoPacket& out);
void decodeDemoSendTables(const uint8_t* data, size_t size, DemoSendTables& out);
void decodeDemoClassInfo(const uint8_t* data, size_t size, DemoClassInfo& out);
void decodeDemoStringTables(const uint8_t* data, size_t size, DemoStringTables& out);
void decodeDemoFullPacket(const uint8_t* data, size_t size, DemoFullPacket& out);

void decodeFlattenedSerializer(const uint8_t* data, size_t size, FlattenedSerializerMsg& out);
void decodePacketEntities(const uint8_t* data, size_t size, PacketEntitiesMsg& out);
void decodeServerInfo(const uint8_t* data, size_t size, ServerInfoMsg& out);
void decodeCreateStringTable(const uint8_t* data, size_t size, CreateStringTableMsg& out);
void decodeUpdateStringTable(const uint8_t* data, size_t size, UpdateStringTableMsg& out);
void decodeSvcClassInfo(const uint8_t* data, size_t size, SvcClassInfoMsg& out);

}  // namespace s2
