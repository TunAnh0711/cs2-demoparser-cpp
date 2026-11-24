// esp.h - CS2 demo ESP: player-state extraction and SVG radar rendering.
// C++20, Windows 11, MSVC.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "demo_parser.h"

namespace s2 {

struct EspPlayer {
    uint32_t tick = 0;
    int entityIndex = -1;
    std::string name;
    uint64_t steamId = 0;
    int team = 0;
    int health = 0;
    bool alive = false;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;

    // Loadout and match state.
    std::string weapon;      // display name, e.g. "AK-47"
    int clipAmmo = 0;        // rounds left in the magazine
    int reserveAmmo = 0;     // rounds in reserve (primary ammo pool)
    int reserveAlt = 0;      // secondary pool (grenades in the launcher)
    int armor = 0;
    bool helmet = false;
    int money = 0;
    int ping = 0;
    int kills = 0;
    int deaths = 0;
    int assists = 0;
    bool scoped = false;
    bool defusing = false;
    bool ducked = false;
};

struct EspFrame {
    uint32_t tick = 0;
    std::vector<EspPlayer> players;
};

// Samples controller/pawn pairs from the parsed demo at a fixed tick stride.
class EspRecorder {
public:
    explicit EspRecorder(uint32_t tickStride = 8) : stride_(tickStride ? tickStride : 1) {}

    void onTick(uint32_t tick, const std::vector<Entity>& entities);
    const std::vector<EspFrame>& frames() const { return frames_; }
    uint32_t stride() const { return stride_; }

private:
    uint32_t stride_ = 8;
    uint32_t lastTick_ = 0xFFFFFFFFu;
    std::vector<EspFrame> frames_;
    // Last observed state per controller entity index.  A dead player has no
    // live pawn, but the controller entity survives; keeping the last known
    // state lets the overlay still show them (greyed out) instead of dropping
    // them from the roster entirely.
    std::unordered_map<int, EspPlayer> lastKnown_;
};

// Writes one CSV row per player per sampled tick.  The rows carry the full
// player state so the output can be diffed against another demo parser.
bool writeStatsCsv(const std::string& path, const std::vector<EspFrame>& frames);

struct RadarBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

RadarBounds radarBounds(const std::vector<EspFrame>& frames);

// Renders a single tick as a self-contained SVG radar.
std::string renderRadarSvg(const EspFrame& frame, const RadarBounds& bounds, int width = 900,
                           int height = 900);

// Writes a self-contained HTML player with a tick scrubber and all frames.
bool writeEspHtml(const std::string& path, const std::vector<EspFrame>& frames,
                  const std::string& mapName);

}  // namespace s2
