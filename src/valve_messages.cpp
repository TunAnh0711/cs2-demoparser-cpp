// valve_messages.cpp - decoders for the Valve protobuf messages used by the
// CS2 demo pipeline.
// C++20, Windows 11, MSVC.
#include "valve_messages.h"

#include "pbreader.h"

namespace s2 {
namespace {

// Proto2 'optional' scalar helpers: the value is only present when the tag was
// seen on the wire, so we track presence explicitly where it matters.
int32_t i32Field(pb::Reader& r) { return static_cast<int32_t>(r.readVarint()); }
uint32_t u32Field(pb::Reader& r) { return static_cast<uint32_t>(r.readVarint()); }
bool boolField(pb::Reader& r) { return r.readVarint() != 0; }
float floatField(pb::Reader& r) {
    const uint32_t bits = r.readFixed32();
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
int32_t s32Field(pb::Reader& r) { return static_cast<int32_t>(r.readZigZag()); }

}  // namespace

void decodeDemoFileHeader(const uint8_t* data, size_t size, DemoFileHeader& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.demoFileStamp = r.readString(); break;
            case 2: out.patchVersion = i32Field(r); break;
            case 3: out.serverName = r.readString(); break;
            case 4: out.clientName = r.readString(); break;
            case 5: out.mapName = r.readString(); break;
            case 6: out.gameDirectory = r.readString(); break;
            case 11: out.demoVersionName = r.readString(); break;
            case 12: out.demoVersionGuid = r.readString(); break;
            case 13: out.buildNum = i32Field(r); break;
            case 14: out.game = r.readString(); break;
            case 15: out.serverStartTick = i32Field(r); break;
            default: r.skipField(wire); break;
        }
    });
}

void decodeDemoFileInfo(const uint8_t* data, size_t size, DemoFileInfo& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.playbackTime = floatField(r); break;
            case 2: out.playbackTicks = i32Field(r); break;
            case 3: out.playbackFrames = i32Field(r); break;
            default: r.skipField(wire); break;
        }
    });
}

void decodeDemoPacket(const uint8_t* data, size_t size, DemoPacket& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field == 3) {
            out.data = r.readByteVector();
        } else {
            r.skipField(wire);
        }
    });
}

void decodeDemoSendTables(const uint8_t* data, size_t size, DemoSendTables& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field == 1) {
            out.data = r.readByteVector();
        } else {
            r.skipField(wire);
        }
    });
}

void decodeDemoClassInfo(const uint8_t* data, size_t size, DemoClassInfo& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field != 1 || wire != pb::kLengthDelimited) {
            r.skipField(wire);
            return;
        }
        const uint8_t* sub = nullptr;
        size_t subSize = 0;
        r.readBytes(sub, subSize);
        DemoClassEntry entry;
        pb::forEachField(sub, subSize, [&](uint32_t f, uint32_t w, pb::Reader& sr) {
            switch (f) {
                case 1: entry.classId = i32Field(sr); break;
                case 2: entry.networkName = sr.readString(); break;
                case 3: entry.tableName = sr.readString(); break;
                default: sr.skipField(w); break;
            }
        });
        out.classes.push_back(std::move(entry));
    });
}

void decodeSvcClassInfo(const uint8_t* data, size_t size, SvcClassInfoMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field != 2 || wire != pb::kLengthDelimited) {
            r.skipField(wire);
            return;
        }
        const uint8_t* sub = nullptr;
        size_t subSize = 0;
        r.readBytes(sub, subSize);
        DemoClassEntry entry;
        pb::forEachField(sub, subSize, [&](uint32_t f, uint32_t w, pb::Reader& sr) {
            switch (f) {
                case 1: entry.classId = i32Field(sr); break;
                case 3: entry.networkName = sr.readString(); break;
                default: sr.skipField(w); break;
            }
        });
        out.classes.push_back(std::move(entry));
    });
}

void decodeDemoStringTables(const uint8_t* data, size_t size, DemoStringTables& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field != 1 || wire != pb::kLengthDelimited) {
            r.skipField(wire);
            return;
        }
        const uint8_t* sub = nullptr;
        size_t subSize = 0;
        r.readBytes(sub, subSize);

        DemoStringTable table;
        pb::forEachField(sub, subSize, [&](uint32_t f, uint32_t w, pb::Reader& tr) {
            if (f == 1) {
                table.tableName = tr.readString();
            } else if (f == 2 && w == pb::kLengthDelimited) {
                const uint8_t* item = nullptr;
                size_t itemSize = 0;
                tr.readBytes(item, itemSize);
                DemoStringTableItem entry;
                pb::forEachField(item, itemSize, [&](uint32_t if_, uint32_t iw, pb::Reader& ir) {
                    if (if_ == 1) {
                        entry.str = ir.readString();
                    } else if (if_ == 2) {
                        entry.data = ir.readByteVector();
                    } else {
                        ir.skipField(iw);
                    }
                });
                table.items.push_back(std::move(entry));
            } else if (f == 4) {
                table.tableFlags = i32Field(tr);
            } else {
                tr.skipField(w);
            }
        });
        out.tables.push_back(std::move(table));
    });
}

void decodeDemoFullPacket(const uint8_t* data, size_t size, DemoFullPacket& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field == 1 && wire == pb::kLengthDelimited) {
            const uint8_t* sub = nullptr;
            size_t subSize = 0;
            r.readBytes(sub, subSize);
            out.hasStringTable = true;
            decodeDemoStringTables(sub, subSize, out.stringTable);
        } else if (field == 2 && wire == pb::kLengthDelimited) {
            const uint8_t* sub = nullptr;
            size_t subSize = 0;
            r.readBytes(sub, subSize);
            out.hasPacket = true;
            decodeDemoPacket(sub, subSize, out.packet);
        } else {
            r.skipField(wire);
        }
    });
}

void decodeFlattenedSerializer(const uint8_t* data, size_t size, FlattenedSerializerMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        if (field == 1 && wire == pb::kLengthDelimited) {
            const uint8_t* sub = nullptr;
            size_t subSize = 0;
            r.readBytes(sub, subSize);
            FlattenedSerializer ser;
            pb::forEachField(sub, subSize, [&](uint32_t f, uint32_t w, pb::Reader& sr) {
                switch (f) {
                    case 1: ser.serializerNameSym = i32Field(sr); break;
                    case 2: ser.serializerVersion = i32Field(sr); break;
                    case 3: {
                        // `repeated int32 fields_index` may be packed or not.
                        if (w == pb::kLengthDelimited) {
                            const uint8_t* p = nullptr;
                            size_t n = 0;
                            sr.readBytes(p, n);
                            pb::Reader pr(p, n);
                            while (!pr.done()) {
                                const uint64_t v = pr.readVarint();
                                ser.fieldsIndex.push_back(static_cast<int32_t>(v));
                            }
                        } else {
                            ser.fieldsIndex.push_back(i32Field(sr));
                        }
                        break;
                    }
                    default: sr.skipField(w); break;
                }
            });
            out.serializers.push_back(std::move(ser));
        } else if (field == 2 && wire == pb::kLengthDelimited) {
            out.symbols.push_back(r.readString());
        } else if (field == 3 && wire == pb::kLengthDelimited) {
            const uint8_t* sub = nullptr;
            size_t subSize = 0;
            r.readBytes(sub, subSize);
            FlattenedSerializerField fld;
            pb::forEachField(sub, subSize, [&](uint32_t f, uint32_t w, pb::Reader& fr) {
                switch (f) {
                    case 1: fld.varTypeSym = i32Field(fr); break;
                    case 2: fld.varNameSym = i32Field(fr); break;
                    case 3: fld.bitCount = i32Field(fr); break;
                    case 4: fld.lowValue = floatField(fr); break;
                    case 5: fld.highValue = floatField(fr); break;
                    case 6: fld.encodeFlags = i32Field(fr); break;
                    case 7:
                        fld.fieldSerializerNameSym = i32Field(fr);
                        fld.hasFieldSerializerName = true;
                        break;
                    case 8: fld.fieldSerializerVersion = i32Field(fr); break;
                    case 9:
                        fld.sendNodeSym = i32Field(fr);
                        fld.hasSendNode = true;
                        break;
                    case 10:
                        fld.varEncoderSym = i32Field(fr);
                        fld.hasVarEncoder = true;
                        break;
                    case 12: fld.varSerializerSym = i32Field(fr); break;
                    default: fr.skipField(w); break;
                }
            });
            out.fields.push_back(std::move(fld));
        } else {
            r.skipField(wire);
        }
    });
}

void decodePacketEntities(const uint8_t* data, size_t size, PacketEntitiesMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.maxEntries = i32Field(r); break;
            case 2: out.updatedEntries = i32Field(r); break;
            case 3: out.legacyIsDelta = boolField(r); break;
            case 4: out.updateBaseline = boolField(r); break;
            case 5: out.baseline = i32Field(r); break;
            case 6: out.deltaFrom = i32Field(r); break;
            case 7: out.entityData = r.readByteVector(); break;
            case 13: out.serializedEntities = r.readByteVector(); break;
            case 12: out.serverTick = u32Field(r); break;
            case 16: out.hasPvsVisBitsDeprecated = u32Field(r); break;
            default: r.skipField(wire); break;
        }
    });
}

void decodeServerInfo(const uint8_t* data, size_t size, ServerInfoMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.protocol = i32Field(r); break;
            case 10: out.maxClients = i32Field(r); break;
            case 11: out.maxClasses = i32Field(r); break;
            case 12: out.playerSlot = i32Field(r); break;
            case 13: out.tickInterval = floatField(r); break;
            case 14: out.gameDir = r.readString(); break;
            case 15: out.mapName = r.readString(); break;
            case 17: out.hostName = r.readString(); break;
            default: r.skipField(wire); break;
        }
    });
}

void decodeCreateStringTable(const uint8_t* data, size_t size, CreateStringTableMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.name = r.readString(); break;
            case 2: out.numEntries = i32Field(r); break;
            case 3: out.userDataFixedSize = boolField(r); break;
            case 4: out.userDataSize = i32Field(r); break;
            case 6: out.flags = i32Field(r); break;
            case 7: out.stringData = r.readByteVector(); break;
            case 9: out.dataCompressed = boolField(r); break;
            case 10: out.usingVarintBitcounts = boolField(r); break;
            default: r.skipField(wire); break;
        }
    });
}

void decodeUpdateStringTable(const uint8_t* data, size_t size, UpdateStringTableMsg& out) {
    pb::forEachField(data, size, [&](uint32_t field, uint32_t wire, pb::Reader& r) {
        switch (field) {
            case 1: out.tableId = i32Field(r); break;
            case 2: out.numChangedEntries = i32Field(r); break;
            case 3: out.stringData = r.readByteVector(); break;
            default: r.skipField(wire); break;
        }
    });
}

}  // namespace s2
