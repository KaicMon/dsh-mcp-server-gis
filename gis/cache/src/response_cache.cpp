#include "gis/cache/response_cache.h"

#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace gis::cache {
namespace {

std::string Normalize(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
        [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
        [](unsigned char c) { return std::isspace(c); }).base();
    if (first >= last) return {};
    value = std::string(first, last);
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

std::string BuildCacheKey(
    const std::string& operation,
    const std::vector<std::pair<std::string, std::string>>& parameters) {
    auto sorted = parameters;
    for (auto& [name, value] : sorted) {
        name = Normalize(std::move(name));
        value = Normalize(std::move(value));
    }
    std::sort(sorted.begin(), sorted.end());
    std::string canonical = Normalize(operation);
    for (const auto& [name, value] : sorted) {
        canonical += '\n' + name + '=' + value;
    }
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(canonical.data()),
           canonical.size(), digest);
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest) output << std::setw(2) << unsigned(byte);
    return output.str();
}

}  // namespace gis::cache
