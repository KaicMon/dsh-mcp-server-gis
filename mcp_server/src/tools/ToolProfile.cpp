#include "tools/ToolProfile.h"

#include "json.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>

using json = nlohmann::json;

namespace vx::mcp {

ToolProfile::ToolProfile(std::string name, std::vector<std::string> include,
                         std::vector<std::string> exclude, bool allow_all)
    : name_(std::move(name)), include_(std::move(include)),
      exclude_(std::move(exclude)), allow_all_(allow_all) {}

ToolProfile ToolProfile::AllowAll() {
    return ToolProfile("all", {}, {}, true);
}

ToolProfile ToolProfile::Load(const std::filesystem::path& file,
                              const std::string& profile_name) {
    if (profile_name == "all") return AllowAll();
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Unable to open tool profile file: " + file.string());
    const auto document = json::parse(input);
    const auto& profile = document.at("profiles").at(profile_name);
    return ToolProfile(profile_name,
        profile.value("include", std::vector<std::string>{}),
        profile.value("exclude", std::vector<std::string>{}), false);
}

bool ToolProfile::Matches(const std::string& pattern, const std::string& value) {
    if (pattern == "*") return true;
    if (!pattern.empty() && pattern.back() == '*') {
        return value.starts_with(pattern.substr(0, pattern.size() - 1));
    }
    return pattern == value;
}

bool ToolProfile::Allows(const std::string& tool_name) const {
    for (const auto& pattern : exclude_) {
        if (Matches(pattern, tool_name)) return false;
    }
    if (allow_all_) return true;
    for (const auto& pattern : include_) {
        if (Matches(pattern, tool_name)) return true;
    }
    return false;
}

}  // namespace vx::mcp
