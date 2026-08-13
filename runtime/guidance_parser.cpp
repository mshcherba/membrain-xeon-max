#include "guidance_parser.h"
#include "third_party/nlohmann/json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace membrain::guidance {

bool parseFile(const std::string& filepath, std::unordered_map<uint32_t, int>& siteTierMap) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    siteTierMap.clear();

    try {
        json j;
        file >> j;

        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.contains("site_id") && item.contains("tier")) {
                    uint32_t siteId = item["site_id"].get<uint32_t>();
                    int tierNode = item["tier"].get<int>();
                    siteTierMap[siteId] = tierNode;
                }
            }
        } else if (j.is_object()) {
            if (j.contains("site_id") && j.contains("tier")) {
                uint32_t siteId = j["site_id"].get<uint32_t>();
                int tierNode = j["tier"].get<int>();
                siteTierMap[siteId] = tierNode;
            }
        }
    } catch (const json::exception&) {
        file.clear();
        file.seekg(0);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            try {
                json jLine = json::parse(line);
                if (jLine.contains("site_id") && jLine.contains("tier")) {
                    uint32_t siteId = jLine["site_id"].get<uint32_t>();
                    int tierNode = jLine["tier"].get<int>();
                    siteTierMap[siteId] = tierNode;
                }
            } catch (...) {}
        }
    }

    return !siteTierMap.empty();
}

} // namespace membrain::guidance
