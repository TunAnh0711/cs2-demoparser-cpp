// main.cpp - cs2demo_esp command line entry point.
// C++20, Windows 11, MSVC.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "demo_parser.h"
#include "esp.h"

namespace {

void printUsage() {
    std::cout << "cs2demo_esp - Counter-Strike 2 demo ESP / entity parser\n"
                 "\n"
                 "usage:\n"
                 "  cs2demo_esp schema <demo.dem>\n"
                 "  cs2demo_esp dump   <demo.dem> [class-filter] [max-entities]\n"
                 "  cs2demo_esp esp    <demo.dem> <out.html> [tick-stride] [tick.svg]\n"
                 "  cs2demo_esp csv    <demo.dem> <out.csv> [tick-stride]\n"
                 "\n"
                 "The esp mode samples controller/pawn pairs, reconstructs world\n"
                 "coordinates, resolves the held weapon and writes a self-contained\n"
                 "animated radar with a live scoreboard.\n"
                 "\n"
                 "The csv mode writes one row per player per sampled tick so the\n"
                 "output can be compared against another demo parser.\n";
}

void printStats(const s2::DemoStats& s) {
    std::printf("\nframes=%u packets=%u entityUpdates=%u created=%u updated=%u deleted=%u\n",
                s.frames, s.packets, s.entityUpdates, s.entitiesCreated, s.entitiesUpdated,
                s.entitiesDeleted);
}

void dumpSchema(s2::DemoParser& parser) {
    std::cout << "== serializers ==\n";
    for (const auto& kv : parser.serializers().serializers()) {
        std::printf("%s (%zu fields)\n", kv.first.c_str(), kv.second->fields.size());
    }
}

void dumpEntities(s2::DemoParser& parser, std::string filter, int maxEntities) {
    int printed = 0;
    // `filter` is captured by value: the tick handler runs long after this
    // function returns, so capturing a reference would dangle.
    parser.setTickHandler([&printed, filter, maxEntities](uint32_t tick,
                                                          const std::vector<s2::Entity>& entities) {
        if (printed >= maxEntities) return;
        for (const s2::Entity& e : entities) {
            if (printed >= maxEntities) return;
            if (!e.alive || e.className.find(filter) == std::string::npos) continue;
            std::printf("tick=%u entity[%d] class=%s serial=%u fields=%zu\n", tick, e.index,
                        e.className.c_str(), e.serial, e.fields.size());
            for (const auto& kv : e.fields) {
                const s2::Value& v = kv.second;
                switch (v.kind) {
                    case s2::Value::Kind::Bool:
                        std::printf("    %s = %s\n", kv.first.c_str(), v.b ? "true" : "false");
                        break;
                    case s2::Value::Kind::Int:
                        std::printf("    %s = %lld\n", kv.first.c_str(),
                                    static_cast<long long>(v.i));
                        break;
                    case s2::Value::Kind::Float:
                        std::printf("    %s = %.4f\n", kv.first.c_str(), v.f);
                        break;
                    case s2::Value::Kind::String:
                        std::printf("    %s = \"%s\"\n", kv.first.c_str(), v.s.c_str());
                        break;
                    case s2::Value::Kind::Vector:
                        std::printf("    %s = (%.3f, %.3f, %.3f)\n", kv.first.c_str(), v.v[0],
                                    v.v[1], v.v[2]);
                        break;
                    default:
                        break;
                }
            }
            ++printed;
        }
    });
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string mode = argv[1];
    const std::string path = argv[2];
    if (mode != "schema" && mode != "dump" && mode != "esp" && mode != "csv") {
        printUsage();
        return 1;
    }

    s2::DemoParser parser;
    std::string error;
    if (!parser.load(path, error)) {
        std::cerr << "error: " << error << "\n";
        return 2;
    }

    if (mode == "esp") {
        if (argc < 4) {
            printUsage();
            return 1;
        }
        const std::string output = argv[3];
        const uint32_t stride = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 8;
        const std::string svgPath = argc > 5 ? argv[5] : std::string();

        s2::EspRecorder recorder(stride);
        parser.setTickHandler([&](uint32_t tick, const std::vector<s2::Entity>& entities) {
            recorder.onTick(tick, entities);
        });
        if (!parser.parse(error)) {
            std::cerr << "error: " << error << "\n";
            return 3;
        }
        if (recorder.frames().empty()) {
            std::cerr << "error: no player entities were found in this demo\n";
            return 4;
        }
        if (!s2::writeEspHtml(output, recorder.frames(), parser.mapName())) {
            std::cerr << "error: cannot write " << output << "\n";
            return 5;
        }
        if (!svgPath.empty()) {
            const s2::RadarBounds bounds = s2::radarBounds(recorder.frames());
            const std::string svg = s2::renderRadarSvg(recorder.frames().front(), bounds);
            std::ofstream svgFile(svgPath, std::ios::binary);
            if (!svgFile) {
                std::cerr << "error: cannot write " << svgPath << "\n";
                return 5;
            }
            svgFile << svg;
        }
        std::printf("map=%s frames=%u players(first)=%zu\n", parser.mapName().c_str(),
                    static_cast<unsigned>(recorder.frames().size()),
                    recorder.frames().front().players.size());
        printStats(parser.stats());
        return 0;
    }

    if (mode == "csv") {
        if (argc < 4) {
            printUsage();
            return 1;
        }
        const std::string output = argv[3];
        const uint32_t stride = argc > 4 ? static_cast<uint32_t>(std::stoul(argv[4])) : 1;

        s2::EspRecorder recorder(stride);
        parser.setTickHandler([&](uint32_t tick, const std::vector<s2::Entity>& entities) {
            recorder.onTick(tick, entities);
        });
        if (!parser.parse(error)) {
            std::cerr << "error: " << error << "\n";
            return 3;
        }
        if (!s2::writeStatsCsv(output, recorder.frames())) {
            std::cerr << "error: cannot write " << output << "\n";
            return 5;
        }
        std::printf("map=%s frames=%u rows=%zu\n", parser.mapName().c_str(),
                    static_cast<unsigned>(recorder.frames().size()),
                    recorder.frames().size() ? recorder.frames().front().players.size() : 0);
        printStats(parser.stats());
        return 0;
    }

    if (mode == "schema") {
        if (!parser.parse(error)) {
            std::cerr << "error: " << error << "\n";
            return 3;
        }
        dumpSchema(parser);
        printStats(parser.stats());
        return 0;
    }

    dumpEntities(parser, argc > 3 ? argv[3] : "CCSPlayer",
                 argc > 4 ? std::atoi(argv[4]) : 3);
    if (!parser.parse(error)) {
        std::cerr << "error: " << error << "\n";
        return 3;
    }
    printStats(parser.stats());
    return 0;
}
