#include "routing/csr_graph.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace routing {
namespace {

constexpr std::array<char, 8> kMagic{'M', 'C', 'P', 'R', 'O', 'U', 'T', 'E'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint64_t kMaxElements = 2'000'000'000ULL;

struct FileHeader {
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t reserved = 0;
    std::uint64_t node_count = 0;
    std::uint64_t edge_count = 0;
    std::uint64_t geometry_count = 0;
    std::uint64_t checksum = 0;
};

static_assert(std::is_trivially_copyable_v<FileHeader>);

class Checksum {
public:
    // FNV-1a is used for corruption detection, not cryptographic authenticity.
    void Add(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index) {
            value_ ^= bytes[index];
            value_ *= 1099511628211ULL;
        }
    }
    [[nodiscard]] std::uint64_t Value() const noexcept { return value_; }

private:
    std::uint64_t value_ = 14695981039346656037ULL;
};

template <typename T>
void AddVector(Checksum& checksum, const std::vector<T>& values) {
    static_assert(std::is_trivially_copyable_v<T>);
    checksum.Add(values.data(), values.size() * sizeof(T));
}

std::uint64_t GraphChecksum(const CsrGraph& graph) {
    // Hash vectors in exactly the same order in which they are serialized.
    Checksum checksum;
    AddVector(checksum, graph.node_osm_ids);
    AddVector(checksum, graph.node_coordinates);
    AddVector(checksum, graph.offsets);
    AddVector(checksum, graph.targets);
    AddVector(checksum, graph.distance_m);
    AddVector(checksum, graph.duration_s);
    AddVector(checksum, graph.edge_osm_way_ids);
    AddVector(checksum, graph.geometry_offsets);
    AddVector(checksum, graph.geometry);
    return checksum.Value();
}

template <typename T>
void WriteVector(std::ofstream& output, const std::vector<T>& values) {
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!output) throw std::runtime_error("Failed to write routing network file");
}

template <typename T>
void ReadVector(std::ifstream& input, std::vector<T>& values, std::uint64_t count) {
    if (count > kMaxElements || count > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
        throw std::runtime_error("Routing network vector size is invalid");
    }
    values.resize(static_cast<std::size_t>(count));
    input.read(reinterpret_cast<char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!input) throw std::runtime_error("Routing network file is truncated");
}

}  // namespace

void NetworkFile::Save(const CsrGraph& graph, const std::filesystem::path& output_path) {
    graph.Validate();
    FileHeader header{kMagic, kFormatVersion, 0, graph.NodeCount(), graph.EdgeCount(),
                      graph.geometry.size(), GraphChecksum(graph)};
    std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to create network file: " + output_path.string());
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    WriteVector(output, graph.node_osm_ids);
    WriteVector(output, graph.node_coordinates);
    WriteVector(output, graph.offsets);
    WriteVector(output, graph.targets);
    WriteVector(output, graph.distance_m);
    WriteVector(output, graph.duration_s);
    WriteVector(output, graph.edge_osm_way_ids);
    WriteVector(output, graph.geometry_offsets);
    WriteVector(output, graph.geometry);
}

CsrGraph NetworkFile::Load(const std::filesystem::path& input_path) {
    // Counts are validated before allocation to prevent a corrupt header from
    // requesting unbounded memory. Structural validation follows deserialization.
    std::ifstream input{input_path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open network file: " + input_path.string());
    FileHeader header;
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input || header.magic != kMagic) throw std::runtime_error("Invalid routing network magic");
    if (header.version != kFormatVersion) throw std::runtime_error("Unsupported network format version");

    CsrGraph graph;
    ReadVector(input, graph.node_osm_ids, header.node_count);
    ReadVector(input, graph.node_coordinates, header.node_count);
    ReadVector(input, graph.offsets, header.node_count + 1);
    ReadVector(input, graph.targets, header.edge_count);
    ReadVector(input, graph.distance_m, header.edge_count);
    ReadVector(input, graph.duration_s, header.edge_count);
    ReadVector(input, graph.edge_osm_way_ids, header.edge_count);
    ReadVector(input, graph.geometry_offsets, header.edge_count + 1);
    ReadVector(input, graph.geometry, header.geometry_count);
    graph.Validate();
    if (GraphChecksum(graph) != header.checksum) {
        throw std::runtime_error("Routing network checksum mismatch");
    }
    return graph;
}

}  // namespace routing
