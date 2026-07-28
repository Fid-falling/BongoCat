#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Asset {
    std::string path;
    std::vector<unsigned char> data;
    size_t unique = 0;
    uint64_t offset = 0;
};

static uint64_t hash_bytes(const std::vector<unsigned char> &data) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : data) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}

static bool read_file(const fs::path &path, std::vector<unsigned char> &data) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return false;
    std::streamoff size = stream.tellg();
    if (size < 0 || static_cast<uint64_t>(size) >
        std::numeric_limits<uint32_t>::max()) return false;
    data.resize(static_cast<size_t>(size));
    stream.seekg(0);
    return data.empty() || stream.read(reinterpret_cast<char *>(data.data()), size).good();
}

static void write16(std::ostream &stream, uint16_t value) {
    const unsigned char bytes[] = {
        static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
    stream.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

static void write32(std::ostream &stream, uint32_t value) {
    unsigned char bytes[4];
    for (int i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (i * 8));
    stream.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

static void write64(std::ostream &stream, uint64_t value) {
    unsigned char bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = static_cast<unsigned char>(value >> (i * 8));
    stream.write(reinterpret_cast<const char *>(bytes), sizeof(bytes));
}

static bool collect(const fs::path &root, std::vector<Asset> &assets) {
    for (const fs::directory_entry &entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        Asset asset;
        asset.path = root.filename().generic_u8string() + "/" +
            fs::relative(entry.path(), root).generic_u8string();
        if (asset.path == "assets/logo-mac.png" ||
            asset.path == "assets/ui-icons.png" ||
            asset.path == "assets/ui-symbols-1x.png" ||
            asset.path == "assets/ui-symbols-4x.png") continue;
        if (asset.path.size() > std::numeric_limits<uint16_t>::max() ||
            !read_file(entry.path(), asset.data)) return false;
        assets.push_back(std::move(asset));
    }
    std::sort(assets.begin(), assets.end(),
        [](const Asset &left, const Asset &right) { return left.path < right.path; });
    return assets.size() <= std::numeric_limits<uint32_t>::max();
}

static std::vector<size_t> deduplicate(std::vector<Asset> &assets) {
    using Key = std::pair<uint64_t, size_t>;
    std::map<Key, std::vector<size_t>> candidates;
    std::vector<size_t> unique;
    for (size_t index = 0; index < assets.size(); ++index) {
        Key key = {hash_bytes(assets[index].data), assets[index].data.size()};
        bool found = false;
        for (size_t prior : candidates[key]) {
            if (assets[prior].data != assets[index].data) continue;
            assets[index].unique = prior; found = true; break;
        }
        if (!found) {
            assets[index].unique = index;
            candidates[key].push_back(index);
            unique.push_back(index);
        }
    }
    return unique;
}

static bool write_pack(const fs::path &output, std::vector<Asset> &assets) {
    std::vector<size_t> unique = deduplicate(assets);
    uint64_t offset = 12;
    for (const Asset &asset : assets) offset += 16 + asset.path.size();
    for (size_t index : unique) {
        assets[index].offset = offset;
        offset += assets[index].data.size();
    }
    for (Asset &asset : assets) asset.offset = assets[asset.unique].offset;
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    const char magic[8] = {'L', '2', 'D', 'P', 'A', 'K', '1', '\0'};
    stream.write(magic, sizeof(magic));
    write32(stream, static_cast<uint32_t>(assets.size()));
    for (const Asset &asset : assets) {
        write16(stream, static_cast<uint16_t>(asset.path.size()));
        write16(stream, 0);
        write32(stream, static_cast<uint32_t>(asset.data.size()));
        write64(stream, asset.offset);
        stream.write(asset.path.data(), static_cast<std::streamsize>(asset.path.size()));
    }
    for (size_t index : unique) stream.write(
        reinterpret_cast<const char *>(assets[index].data.data()),
        static_cast<std::streamsize>(assets[index].data.size()));
    return stream.good();
}

int main(int argc, char **argv) {
    if (argc != 3) { std::cerr << "usage: asset_packer ROOT OUTPUT\n"; return 2; }
    try {
        std::vector<Asset> assets;
        if (!collect(fs::u8path(argv[1]), assets) ||
            !write_pack(fs::u8path(argv[2]), assets)) {
            std::cerr << "cannot create asset pack\n"; return 1;
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
    return 0;
}
