#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vx::mcp {

class ToolProfile {
public:
    static ToolProfile Load(const std::filesystem::path& file,
                            const std::string& profile_name);
    static ToolProfile AllowAll();

    [[nodiscard]] bool Allows(const std::string& tool_name) const;
    [[nodiscard]] const std::string& Name() const noexcept { return name_; }

private:
    ToolProfile(std::string name, std::vector<std::string> include,
                std::vector<std::string> exclude, bool allow_all);
    static bool Matches(const std::string& pattern, const std::string& value);

    std::string name_;
    std::vector<std::string> include_;
    std::vector<std::string> exclude_;
    bool allow_all_{false};
};

}  // namespace vx::mcp
