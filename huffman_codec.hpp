#ifndef HUFFMAN_CODEC_HPP
#define HUFFMAN_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace huffman {

struct CompressionStats {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::size_t original_size = 0;
    std::size_t compressed_size = 0;

    double compression_ratio() const;
    double saving_ratio() const;
};

struct DecompressionStats {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::size_t compressed_size = 0;
    std::size_t restored_size = 0;
};

std::vector<std::uint8_t> compress_bytes(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> decompress_bytes(const std::vector<std::uint8_t>& blob);

CompressionStats compress_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path = std::filesystem::path());

DecompressionStats decompress_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path = std::filesystem::path());

std::string format_size(std::size_t size);

}  // namespace huffman

#endif
