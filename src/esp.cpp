// esp.cpp - CS2 demo ESP: player-state extraction and SVG radar rendering.
// C++20, Windows 11, MSVC.
#include "esp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "weapon_indices.h"

namespace s2 {

Entity* entityByHandleGlobal(const std::vector<Entity>& entities, uint32_t handle);

namespace {

constexpr float kCellBits = 512.0f;   // 1 << 9
constexpr float kMaxCoord = 16384.0f;

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Exact match first; a unique dotted-suffix match is accepted as a fallback.
// Ambiguous suffixes return nullptr so that unrelated fields (for example the
// many "m_iKills" entries under m_pActionTrackingServices.m_perRoundStats)
// can never be picked up by accident.
const Value* findField(const Entity& e, const std::string& name) {
    auto exact = e.fields.find(name);
    if (exact != e.fields.end()) return &exact->second;
    const Value* match = nullptr;
    int matches = 0;
    for (const auto& kv : e.fields) {
        if (!endsWith(kv.first, name)) continue;
        if (kv.first.size() == name.size()) return &kv.second;
        ++matches;
        if (matches == 1) match = &kv.second;
    }
    return matches == 1 ? match : nullptr;
}

float number(const Entity& e, const std::string& name) {
    const Value* v = findField(e, name);
    return v ? v->asFloat() : 0.0f;
}

int integer(const Entity& e, const std::string& name) {
    const Value* v = findField(e, name);
    return v ? static_cast<int>(v->asInt()) : 0;
}

bool flag(const Entity& e, const std::string& name) {
    const Value* v = findField(e, name);
    return v != nullptr && v->asBool();
}

// Field names carry a send-node prefix ("m_pInGameMoneyServices.m_iAccount").
// The suffix used here is the trailing component only, so the lookup stays
// unambiguous as long as the component name is unique within the entity.
float axisCoordinate(const Entity& pawn, const char* cell, const char* offset) {
    return number(pawn, cell) * kCellBits - kMaxCoord + number(pawn, offset);
}

// Maps the econ item definition index carried by the weapon entity to the
// weapon's display name.
std::string weaponName(const Entity& weapon) {
    const Value* def = findField(weapon, "m_AttributeManager.m_Item.m_iItemDefinitionIndex");
    if (def == nullptr) return std::string();
    const auto& names = weaponIndexNames();
    const auto it = names.find(static_cast<uint32_t>(def->asInt()));
    if (it != names.end()) return it->second;
    return std::string();
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string csvEscape(const std::string& s) {
    bool needsQuotes = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return s;
    std::string out;
    out.reserve(s.size() + 8);
    out += '"';
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

struct TeamStyle {
    const char* fill;
    const char* stroke;
    const char* label;
};

TeamStyle styleFor(int team) {
    if (team == 3) return {"#3d82d8", "#9dc4ff", "CT"};
    if (team == 2) return {"#dba32b", "#ffe19a", "T"};
    return {"#7c8798", "#c2cbd6", "?"};
}

}  // namespace

void EspRecorder::onTick(uint32_t tick, const std::vector<Entity>& entities) {
    if (tick - lastTick_ < stride_ && lastTick_ != 0xFFFFFFFFu) return;
    lastTick_ = tick;

    EspFrame frame;
    frame.tick = tick;

    for (const Entity& controller : entities) {
        if (!controller.alive || controller.className != "CCSPlayerController") continue;
        const Value* pawnHandle = findField(controller, "m_hPawn");
        if (pawnHandle == nullptr) continue;
        const Entity* pawn = entityByHandleGlobal(entities, static_cast<uint32_t>(pawnHandle->asInt()));
        if (pawn != nullptr && !pawn->alive) pawn = nullptr;
        if (pawn != nullptr && pawn->className.find("Pawn") == std::string::npos) pawn = nullptr;

        // Liveness: the pawn's life state is authoritative (LIFE_ALIVE == 0).
        // The controller's m_bPawnIsAlive is only a fallback for demos whose
        // pawn does not carry a life state.
        const Value* aliveVal = findField(controller, "m_bPawnIsAlive");
        const Value* lifeVal = pawn ? findField(*pawn, "m_lifeState") : nullptr;
        const bool pawnAlive = pawn != nullptr &&
                               (lifeVal != nullptr ? lifeVal->asInt() == 0
                                                   : (aliveVal == nullptr || aliveVal->asBool()));

        if (pawn == nullptr) {
            // No pawn entity at all (the player died and the pawn was
            // destroyed).  Keep the player on the roster with their last known
            // position and loadout so the overlay does not lose track of them.
            auto it = lastKnown_.find(controller.index);
            if (it == lastKnown_.end()) continue;
            EspPlayer p = it->second;
            p.tick = tick;
            p.alive = false;
            p.health = 0;
            p.scoped = false;
            p.defusing = false;
            p.ducked = false;
            // Controller-side counters keep ticking while the player is dead.
            p.team = static_cast<int>(number(controller, "m_iTeamNum"));
            p.money = integer(controller, "m_pInGameMoneyServices.m_iAccount");
            p.ping = integer(controller, "m_iPing");
            p.kills = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iKills");
            p.deaths = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iDeaths");
            p.assists = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iAssists");
            frame.players.push_back(std::move(p));
            continue;
        }

        EspPlayer p;
        p.tick = tick;
        p.entityIndex = controller.index;
        const Value* nameVal = findField(controller, "m_iszPlayerName");
        p.name = nameVal && nameVal->kind == Value::Kind::String ? nameVal->s : std::string("player");
        const Value* steamVal = findField(controller, "m_steamID");
        p.steamId = steamVal ? static_cast<uint64_t>(steamVal->asInt()) : 0;
        p.team = static_cast<int>(number(controller, "m_iTeamNum"));
        p.health = std::max(0, integer(*pawn, "m_iHealth"));
        p.alive = pawnAlive;
        p.x = axisCoordinate(*pawn, "m_vecOrigin.m_cellX", "m_vecOrigin.m_vecX");
        p.y = axisCoordinate(*pawn, "m_vecOrigin.m_cellY", "m_vecOrigin.m_vecY");
        p.z = axisCoordinate(*pawn, "m_vecOrigin.m_cellZ", "m_vecOrigin.m_vecZ");
        const Value* angle = findField(*pawn, "m_angEyeAngles");
        p.yaw = angle && angle->kind == Value::Kind::Vector ? angle->v[1] : 0.0f;

        // Equipment: the pawn points at the weapon entity it is holding.
        const Value* weaponHandle = findField(*pawn, "m_pWeaponServices.m_hActiveWeapon");
        if (weaponHandle != nullptr) {
            const Entity* weapon =
                entityByHandleGlobal(entities, static_cast<uint32_t>(weaponHandle->asInt()));
            if (weapon != nullptr) {
                p.weapon = weaponName(*weapon);
                p.clipAmmo = integer(*weapon, "m_iClip1");
                // m_pReserveAmmo is a two-slot array: slot 0 holds the weapon's
                // primary ammo pool (rifle/pistol rounds), slot 1 the secondary
                // pool (how many grenades a launcher holds).  Guns only fill
                // slot 0, grenade launchers only slot 1 - report whichever
                // slot actually carries ammo.
                // (The reference parser's "total_ammo_left" alias resolves to
                // whichever element was written last, so it reports 0 for
                // every gun; its own docs describe the intent as [0].)
                const int primary = integer(*weapon, "m_pReserveAmmo.0000");
                const int secondary = integer(*weapon, "m_pReserveAmmo.0001");
                p.reserveAmmo = primary > 0 ? primary : secondary;
                p.reserveAlt = secondary;
                // A knife carries a filler value in the ammo array; showing it
                // as "0/1" would look like a round in the magazine.
                if (weapon->className.find("Knife") != std::string::npos) {
                    p.reserveAmmo = 0;
                    p.reserveAlt = 0;
                }
                if (p.weapon.empty()) p.weapon = weapon->className;
            }
        }

        // Armour and helmet live on the pawn; the controller mirrors them.
        p.armor = integer(*pawn, "m_ArmorValue");
        if (p.armor == 0 && p.alive) p.armor = integer(controller, "m_iPawnArmor");
        p.helmet = (flag(*pawn, "m_pItemServices.m_bHasHelmet") ||
                    flag(controller, "m_bPawnHasHelmet")) &&
                   p.alive;

        // Economy and performance counters live on the controller.
        p.money = integer(controller, "m_pInGameMoneyServices.m_iAccount");
        p.ping = integer(controller, "m_iPing");
        p.kills = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iKills");
        p.deaths = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iDeaths");
        p.assists = integer(controller, "m_pActionTrackingServices.m_matchStats.m_iAssists");

        p.scoped = flag(*pawn, "m_bIsScoped") && p.alive;
        p.defusing = flag(*pawn, "m_bIsDefusing") && p.alive;
        p.ducked = flag(*pawn, "m_pMovementServices.m_bDucked") && p.alive;

        // Only a live state is worth remembering as the fallback.
        if (p.alive) lastKnown_[controller.index] = p;
        frame.players.push_back(std::move(p));
    }

    frames_.push_back(std::move(frame));
}

RadarBounds radarBounds(const std::vector<EspFrame>& frames) {
    RadarBounds b;
    bool first = true;
    for (const EspFrame& f : frames) {
        for (const EspPlayer& p : f.players) {
            if (first) {
                b.minX = b.maxX = p.x;
                b.minY = b.maxY = p.y;
                first = false;
            } else {
                b.minX = std::min(b.minX, p.x);
                b.maxX = std::max(b.maxX, p.x);
                b.minY = std::min(b.minY, p.y);
                b.maxY = std::max(b.maxY, p.y);
            }
        }
    }
    if (first || b.maxX - b.minX < 256.0f || b.maxY - b.minY < 256.0f) {
        b.minX -= 512.0f;
        b.maxX += 512.0f;
        b.minY -= 512.0f;
        b.maxY += 512.0f;
    }
    const float padX = (b.maxX - b.minX) * 0.08f;
    const float padY = (b.maxY - b.minY) * 0.08f;
    b.minX -= padX;
    b.maxX += padX;
    b.minY -= padY;
    b.maxY += padY;
    return b;
}

std::string renderRadarSvg(const EspFrame& frame, const RadarBounds& bounds, int width,
                           int height) {
    std::ostringstream s;
    const float spanX = std::max(1.0f, bounds.maxX - bounds.minX);
    const float spanY = std::max(1.0f, bounds.maxY - bounds.minY);
    const float scale = std::min((width - 80.0f) / spanX, (height - 80.0f) / spanY);
    const float usedW = spanX * scale;
    const float usedH = spanY * scale;
    const float offX = (width - usedW) * 0.5f;
    const float offY = (height - usedH) * 0.5f;
    auto px = [&](float x) { return offX + (x - bounds.minX) * scale; };
    auto py = [&](float y) { return height - (offY + (y - bounds.minY) * scale); };

    s << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << width << ' ' << height
      << "\" width=\"100%\" height=\"100%\">\n";
    s << "<rect width=\"100%\" height=\"100%\" fill=\"#0d1117\"/>\n";
    s << "<g stroke=\"#1d2938\" stroke-width=\"1\">\n";
    for (int i = 0; i <= 10; ++i) {
        const float gx = offX + usedW * i / 10.0f;
        const float gy = offY + usedH * i / 10.0f;
        s << "<line x1=\"" << gx << "\" y1=\"" << offY << "\" x2=\"" << gx << "\" y2=\""
          << offY + usedH << "\"/>\n";
        s << "<line x1=\"" << offX << "\" y1=\"" << gy << "\" x2=\"" << offX + usedW
          << "\" y2=\"" << gy << "\"/>\n";
    }
    s << "</g>\n";
    s << "<rect x=\"" << offX << "\" y=\"" << offY << "\" width=\"" << usedW << "\" height=\""
      << usedH << "\" fill=\"none\" stroke=\"#2d3b4e\" stroke-width=\"2\"/>\n";
    s << "<text x=\"24\" y=\"34\" fill=\"#8ea2ba\" font-family=\"Consolas,monospace\" "
         "font-size=\"18\">CS2 DEMO ESP / tick "
      << frame.tick << " / " << frame.players.size() << " players</text>\n";

    // Label text is built first so that its real extent can be measured: the
    // labels are then placed so that two-line blocks never overlap, which
    // happens whenever several players stand at the same spawn point.
    struct Marker {
        float cx = 0.0f;
        float cy = 0.0f;
        float rad = 0.0f;
        float labelDy = 0.0f;   // vertical nudge applied to the text block
        std::string nameLine;
        std::string detailLine;
        float width = 0.0f;     // widest of the two lines, in pixels
    };
    std::vector<Marker> markers;
    markers.reserve(frame.players.size());
    for (const EspPlayer& p : frame.players) {
        const TeamStyle st = styleFor(p.team);
        Marker m;
        m.cx = px(p.x);
        m.cy = py(p.y);
        m.rad = std::min(16.0f, std::max(9.0f, scale * 1.4f));
        m.nameLine = p.name + " [" + st.label + "] " + std::to_string(p.health);
        if (!p.alive) m.nameLine += " DEAD";
        if (!p.weapon.empty()) {
            m.detailLine = p.weapon + ' ' + std::to_string(p.clipAmmo);
            if (p.reserveAmmo > 0) {
                m.detailLine += '/';
                m.detailLine += std::to_string(p.reserveAmmo);
            }
        }
        if (p.armor > 0) {
            if (!m.detailLine.empty()) m.detailLine += "  ";
            m.detailLine += std::to_string(p.armor);
            m.detailLine += p.helmet ? "A+H" : "A";
        }
        if (p.money > 0) {
            if (!m.detailLine.empty()) m.detailLine += "  ";
            m.detailLine += '$';
            m.detailLine += std::to_string(p.money);
        }
        // Consolas advance width is 0.6 em.
        m.width = std::max(m.nameLine.size() * 12.0f * 0.6f,
                           m.detailLine.size() * 10.0f * 0.6f);
        markers.push_back(std::move(m));
    }

    // Greedy placement: a label is pushed down by one line until its text
    // block no longer overlaps any block already placed.
    const float kLineH = 13.0f;
    const float kAscent = 11.0f;   // height of the upper text line
    const float kDescent = 4.0f;   // depth of the lower text line
    for (size_t i = 0; i < markers.size(); ++i) {
        bool moved = true;
        while (moved) {
            moved = false;
            const float ax = markers[i].cx + markers[i].rad + 5.0f;
            const float iTop = markers[i].cy + 4.0f + markers[i].labelDy - kAscent;
            const float iBot = markers[i].cy + 17.0f + markers[i].labelDy + kDescent;
            for (size_t j = 0; j < i; ++j) {
                const float bx = markers[j].cx + markers[j].rad + 5.0f;
                const float jTop = markers[j].cy + 4.0f + markers[j].labelDy - kAscent;
                const float jBot = markers[j].cy + 17.0f + markers[j].labelDy + kDescent;
                const bool hOverlap = ax < bx + markers[j].width && bx < ax + markers[i].width;
                const bool vOverlap = iBot >= jTop && jBot >= iTop;
                if (hOverlap && vOverlap) {
                    markers[i].labelDy += kLineH;
                    moved = true;
                    break;
                }
            }
        }
    }

    for (size_t index = 0; index < frame.players.size(); ++index) {
        const EspPlayer& p = frame.players[index];
        const Marker& m = markers[index];
        const TeamStyle st = styleFor(p.team);
        const float cx = m.cx;
        const float cy = m.cy;
        const float rad = m.rad;
        const float ang = p.yaw * 3.14159265f / 180.0f;
        const float cone = 52.0f;
        const float coneLen = rad * 3.4f;
        const float a1 = ang - cone * 3.14159265f / 360.0f;
        const float a2 = ang + cone * 3.14159265f / 360.0f;
        if (p.alive) {
            s << "<path d=\"M " << cx << ' ' << cy << " L " << cx + std::sin(a1) * coneLen << ' '
              << cy - std::cos(a1) * coneLen << " A " << coneLen << ' ' << coneLen << " 0 0 1 "
              << cx + std::sin(a2) * coneLen << ' ' << cy - std::cos(a2) * coneLen
              << " Z\" fill=\"" << st.fill << "\" fill-opacity=\"0.16\"/>\n";
        }
        const char* fill = p.alive ? st.fill : "#414a57";
        const char* stroke = p.alive ? st.stroke : "#6b7482";
        s << "<circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << rad << "\" fill=\""
          << fill << "\" stroke=\"" << stroke << "\" stroke-width=\"2\" opacity=\""
          << (p.alive ? "1" : "0.55") << "\"/>\n";
        const float barW = rad * 2.6f;
        const float hp = std::max(0.0f, std::min(1.0f, p.health / 100.0f));
        s << "<rect x=\"" << cx - barW / 2.0f << "\" y=\"" << cy - rad - 12.0f << "\" width=\""
          << barW << "\" height=\"4\" fill=\"#202b39\"/>\n";
        if (p.alive) {
            s << "<rect x=\"" << cx - barW / 2.0f << "\" y=\"" << cy - rad - 12.0f << "\" width=\""
              << barW * hp << "\" height=\"4\" fill=\""
              << (hp > 0.5f ? "#5ad18b" : hp > 0.25f ? "#dba32b" : "#d75b5b") << "\"/>\n";
        }
    }

    // Text is emitted after every marker so that a label is never painted over
    // by another player's circle.
    for (size_t index = 0; index < frame.players.size(); ++index) {
        const EspPlayer& p = frame.players[index];
        const Marker& m = markers[index];
        const float cx = m.cx;
        const float cy = m.cy;
        const float rad = m.rad;

        // A nudged label gets a thin leader line back to its marker.
        if (m.labelDy != 0.0f) {
            s << "<path d=\"M " << cx + rad * 0.7f << ' ' << cy << " L " << cx + rad + 3.0f
              << ' ' << cy + 4.0f + m.labelDy << "\" fill=\"none\" stroke=\"#3a4759\" "
                 "stroke-width=\"1\"/>\n";
        }

        // Name line: identity, team and health.
        s << "<text x=\"" << cx + rad + 5.0f << "\" y=\"" << cy + 4.0f + m.labelDy
          << "\" fill=\"" << (p.alive ? "#dbe6f3" : "#8b95a4")
          << "\" font-family=\"Consolas,monospace\" font-size=\"12\">"
          << xmlEscape(m.nameLine) << "</text>\n";

        // Detail line: weapon, ammunition, armour and money.
        if (!m.detailLine.empty()) {
            s << "<text x=\"" << cx + rad + 5.0f << "\" y=\"" << cy + 17.0f + m.labelDy
              << "\" fill=\"#7f93ad\" font-family=\"Consolas,monospace\" font-size=\"10\">"
              << xmlEscape(m.detailLine) << "</text>\n";
        }
    }
    s << "</svg>\n";
    return s.str();
}

bool writeEspHtml(const std::string& path, const std::vector<EspFrame>& frames,
                  const std::string& mapName) {
    if (frames.empty()) return false;
    const RadarBounds bounds = radarBounds(frames);

    std::ostringstream html;
    html << "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
         << "<title>CS2 Demo ESP - " << xmlEscape(mapName) << "</title>\n"
         << "<style>\n"
         << "body{margin:0;background:#0d1117;color:#dbe6f3;font:14px Consolas,monospace}\n"
         << "header{padding:16px 24px;border-bottom:1px solid #263347;display:flex;gap:20px;"
         << "align-items:center}\n"
         << "h1{font-size:18px;margin:0;letter-spacing:.08em;color:#9dc4ff}\n"
         << "#wrap{display:flex;height:calc(100vh - 58px)}\n"
         << "#stage{flex:1;min-width:0;height:100%}\n"
         << "input[type=range]{flex:1;accent-color:#3d82d8}\n"
         << "button{background:#182233;color:#dbe6f3;border:1px solid #35465e;padding:6px 14px;"
         << "cursor:pointer;font:inherit}\n"
         << "button:hover{background:#223149}\n"
         << "#tick{min-width:96px;color:#dba32b}\n"
         << "#side{width:470px;border-left:1px solid #263347;overflow:auto;padding:0 0 24px}\n"
         << "#side h2{font-size:13px;letter-spacing:.08em;color:#9dc4ff;margin:18px 16px 8px}\n"
         << "table{width:100%;border-collapse:collapse;font-size:11.5px}\n"
         << "th,td{padding:4px 5px;text-align:left;border-bottom:1px solid #1b2534;white-space:nowrap}\n"
         << "th{color:#7f93ad;font-weight:normal;text-align:left}\n"
         << "td.num{text-align:right}\n"
         << ".ct{color:#9dc4ff}.t{color:#dba32b}\n"
         << ".dead{opacity:.42}\n"
         << ".dead td{text-decoration:line-through}\n"
         << "</style></head><body>\n"
         << "<header><h1>CS2 DEMO ESP / " << xmlEscape(mapName) << "</h1>"
         << "<button id=\"prev\">&#9664;</button><button id=\"play\">PLAY</button>"
         << "<button id=\"next\">&#9654;</button>"
         << "<input id=\"scrub\" type=\"range\" min=\"0\" max=\"" << frames.size() - 1
         << "\" value=\"0\"><span id=\"tick\"></span></header>\n"
         << "<div id=\"wrap\"><div id=\"stage\"></div><div id=\"side\">"
         << "<h2>SCOREBOARD</h2><table id=\"board\"></table></div></div>\n<script>\n"
         << "const frames=[\n";

    for (size_t i = 0; i < frames.size(); ++i) {
        html << '"' << jsonEscape(renderRadarSvg(frames[i], bounds)) << '"';
        html << (i + 1 < frames.size() ? ",\n" : "\n");
    }
    html << "];\n";

    // Per-frame roster rows: [name, team, health, weapon, clip, reserve, armor,
    // helmet, money, ping, kills, deaths, assists, alive].
    html << "const rosters=[\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        html << '[';
        const std::vector<EspPlayer>& players = frames[i].players;
        for (size_t j = 0; j < players.size(); ++j) {
            const EspPlayer& p = players[j];
            html << '[' << '"' << jsonEscape(p.name) << '"' << ',' << p.team << ',' << p.health
                 << ',' << '"' << jsonEscape(p.weapon) << '"' << ',' << p.clipAmmo << ','
                 << p.reserveAmmo << ',' << p.armor << ',' << (p.helmet ? 1 : 0) << ',' << p.money
                 << ',' << p.ping << ',' << p.kills << ',' << p.deaths << ',' << p.assists << ','
                 << (p.alive ? 1 : 0) << ']';
            html << (j + 1 < players.size() ? "," : "");
        }
        html << ']' << (i + 1 < frames.size() ? ",\n" : "\n");
    }
    html << "];\n"
         << "const ticks=[";
    for (size_t i = 0; i < frames.size(); ++i) {
        html << (i ? "," : "") << frames[i].tick;
    }
    html << "];\n"
         << "const stage=document.getElementById('scrub');\n"
         << "const label=document.getElementById('tick');\n"
         << "const view=document.getElementById('stage');\n"
         << "const board=document.getElementById('board');\n"
         << "const esc=s=>String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;');\n"
         << "function table(rows){\n"
         << "  let h='<tr><th>PLAYER</th><th>HP</th><th>WEAPON</th><th>AMMO</th>"
         << "<th>ARM</th><th>\\$</th><th>MS</th><th>K/D/A</th></tr>';\n"
         << "  for(const r of rows){\n"
         << "    const cls=r[1]===3?'ct':(r[1]===2?'t':'');\n"
         << "    const ammo=r[4]+(r[5]>0?'/'+r[5]:'');\n"
         << "    const arm=r[6]>0?(r[7]?'A+H':'A'):'';\n"
         << "    const dead=r[13]?1:0;\n"
         << "    h+='<tr class=\"'+(dead?'dead':'')+'\"><td class=\"'+cls+'\">'+esc(r[0])+(dead?' &#9760;':'')+\n"
         << "       '</td><td class=\"num\">'+r[2] +\n"
         << "       '</td><td>'+esc(r[3])+'</td><td class=\"num\">'+ammo+'</td><td class=\"num\">'+\n"
         << "       arm+'</td><td class=\"num\">'+r[8]+'</td><td class=\"num\">'+r[9] +\n"
         << "       '</td><td class=\"num\">'+r[10]+'/'+r[11]+'/'+r[12]+'</td></tr>';\n"
         << "  }\n"
         << "  return h;\n"
         << "}\n"
         << "let idx=0,timer=null;\n"
         << "function draw(){view.innerHTML=frames[idx];label.textContent='tick '+ticks[idx];"
         << "stage.value=idx;board.innerHTML=table(rosters[idx]);}\n"
         << "stage.oninput=()=>{idx=+stage.value;draw();};\n"
         << "document.getElementById('prev').onclick=()=>{idx=(idx+frames.length-1)%frames.length;draw();};\n"
         << "document.getElementById('next').onclick=()=>{idx=(idx+1)%frames.length;draw();};\n"
         << "document.getElementById('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;"
         << "document.getElementById('play').textContent='PLAY';}else{timer=setInterval(()=>{"
         << "idx=(idx+1)%frames.length;draw();},120);document.getElementById('play').textContent='PAUSE';}};\n"
         << "draw();\n</script></body></html>\n";

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << html.str();
    return out.good();
}

bool writeStatsCsv(const std::string& path, const std::vector<EspFrame>& frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "tick,steamid,name,team,health,alive,x,y,z,yaw,weapon,clip,reserve,reserveAlt,armor,"
           "helmet,money,ping,kills,deaths,assists,scoped,defusing,ducked\n";
    for (const EspFrame& frame : frames) {
        for (const EspPlayer& p : frame.players) {
            out << frame.tick << ',' << p.steamId << ',' << csvEscape(p.name) << ',' << p.team << ','
                << p.health << ',' << (p.alive ? 1 : 0) << ',' << p.x << ',' << p.y << ',' << p.z
                << ',' << p.yaw << ',' << csvEscape(p.weapon) << ',' << p.clipAmmo << ','
                << p.reserveAmmo << ',' << p.reserveAlt << ',' << p.armor << ','
                << (p.helmet ? 1 : 0) << ',' << p.money << ',' << p.ping << ',' << p.kills << ','
                << p.deaths << ',' << p.assists << ',' << (p.scoped ? 1 : 0) << ','
                << (p.defusing ? 1 : 0) << ',' << (p.ducked ? 1 : 0) << '\n';
        }
    }
    return out.good();
}

Entity* entityByHandleGlobal(const std::vector<Entity>& entities, uint32_t handle) {
    const uint32_t index = handle & 0x3FFFu;
    const uint32_t serial = handle >> 14;
    if (index >= entities.size()) return nullptr;
    Entity* e = const_cast<Entity*>(&entities[index]);
    if (!e->alive || e->serial != serial) return nullptr;
    return e;
}

}  // namespace s2
