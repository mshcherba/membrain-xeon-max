#ifndef MEMBRAIN_GUIDANCE_PARSER_H
#define MEMBRAIN_GUIDANCE_PARSER_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace membrain::guidance {

// Parses site_tier_guidance.json into site_id -> tier map using nlohmann::json.
// Returns true if file was opened and parsed successfully.
bool parseFile(const std::string& filepath, std::unordered_map<uint32_t, int>& siteTierMap);

} // namespace membrain::guidance

#endif // MEMBRAIN_GUIDANCE_PARSER_H
