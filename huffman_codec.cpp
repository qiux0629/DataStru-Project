#include "huffman_codec.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <cctype>

namespace huffman {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'H', 'U', 'F', '1'};

struct Node {
    std::uint64_t freq = 0;
    std::size_t order = 0;
    int symbol = -1;
    Node* left = nullptr;
    Node* right = nullptr;
};

struct NodeLess {
    bool operator()(const Node* a, const Node* b) const {
        if (a->freq != b->freq) {
            return a->freq > b->freq;
        }
        return a->order > b->order;
    }
};

class BitWriter {
public:
    void write(const std::vector<std::uint8_t>& bits) {
        for (std::uint8_t bit : bits) {
            current_ = static_cast<std::uint8_t>((current_ << 1U) | (bit & 1U));
            ++fill_;
            ++total_bits_;
            if (fill_ == 8U) {
                buffer_.push_back(current_);
                current_ = 0;
                fill_ = 0;
            }
        }
    }

    std::vector<std::uint8_t> finish() {
        if (fill_ > 0U) {
            current_ = static_cast<std::uint8_t>(current_ << (8U - fill_));
            buffer_.push_back(current_);
            current_ = 0;
            fill_ = 0;
        }
        return buffer_;
    }

    std::uint64_t total_bits() const {
        return total_bits_;
    }

private:
    std::vector<std::uint8_t> buffer_;
    std::uint8_t current_ = 0;
    std::uint8_t fill_ = 0;
    std::uint64_t total_bits_ = 0;
};

class BitReader {
public:
    BitReader(const std::vector<std::uint8_t>& data, std::uint64_t total_bits)
        : data_(data), total_bits_(total_bits) {}

    int read() {
        if (consumed_ >= total_bits_) {
            return -1;
        }
        const std::uint64_t byte_index = consumed_ / 8U;
        const std::uint8_t bit_offset = static_cast<std::uint8_t>(7U - (consumed_ % 8U));
        ++consumed_;
        return static_cast<int>((data_.at(static_cast<std::size_t>(byte_index)) >> bit_offset) & 1U);
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::uint64_t total_bits_ = 0;
    std::uint64_t consumed_ = 0;
};

void push_u16_be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void push_u64_be(std::vector<std::uint8_t>& out, std::uint64_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 56U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 48U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 40U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 32U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

std::uint16_t read_u16_be(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data.at(offset)) << 8U) |
                                      static_cast<std::uint16_t>(data.at(offset + 1U)));
}

std::uint64_t read_u64_be(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return (static_cast<std::uint64_t>(data.at(offset)) << 56U) |
           (static_cast<std::uint64_t>(data.at(offset + 1U)) << 48U) |
           (static_cast<std::uint64_t>(data.at(offset + 2U)) << 40U) |
           (static_cast<std::uint64_t>(data.at(offset + 3U)) << 32U) |
           (static_cast<std::uint64_t>(data.at(offset + 4U)) << 24U) |
           (static_cast<std::uint64_t>(data.at(offset + 5U)) << 16U) |
           (static_cast<std::uint64_t>(data.at(offset + 6U)) << 8U) |
           static_cast<std::uint64_t>(data.at(offset + 7U));
}

Node* build_tree(const std::array<std::uint64_t, 256>& freq_map, std::vector<Node>& arena) {
    std::priority_queue<Node*, std::vector<Node*>, NodeLess> heap;
    std::size_t order = 0;
    for (std::size_t symbol = 0; symbol < freq_map.size(); ++symbol) {
        if (freq_map[symbol] == 0U) {
            continue;
        }
        arena.push_back(Node{freq_map[symbol], order++, static_cast<int>(symbol), nullptr, nullptr});
        heap.push(&arena.back());
    }

    if (heap.empty()) {
        return nullptr;
    }

    while (heap.size() > 1U) {
        Node* left = heap.top();
        heap.pop();
        Node* right = heap.top();
        heap.pop();
        arena.push_back(Node{left->freq + right->freq, order++, -1, left, right});
        heap.push(&arena.back());
    }

    return heap.top();
}

void collect_code_lengths(const Node* node, std::uint16_t depth, std::array<std::uint8_t, 256>& out) {
    if (node == nullptr) {
        return;
    }
    if (node->symbol >= 0) {
        out[static_cast<std::size_t>(node->symbol)] = static_cast<std::uint8_t>(std::max<std::uint16_t>(1U, depth));
        return;
    }
    collect_code_lengths(node->left, static_cast<std::uint16_t>(depth + 1U), out);
    collect_code_lengths(node->right, static_cast<std::uint16_t>(depth + 1U), out);
}

class BigUInt {
public:
    BigUInt() : limbs_(1U, 0U) {}

    void left_shift(std::uint16_t bits) {
        if (bits == 0U || is_zero()) {
            return;
        }

        const std::size_t word_shift = bits / 32U;
        const std::uint8_t bit_shift = static_cast<std::uint8_t>(bits % 32U);
        if (word_shift > 0U) {
            limbs_.insert(limbs_.begin(), word_shift, 0U);
        }

        if (bit_shift > 0U) {
            std::uint64_t carry = 0U;
            for (std::size_t i = word_shift; i < limbs_.size(); ++i) {
                const std::uint64_t value =
                    (static_cast<std::uint64_t>(limbs_[i]) << bit_shift) | carry;
                limbs_[i] = static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
                carry = value >> 32U;
            }
            if (carry != 0U) {
                limbs_.push_back(static_cast<std::uint32_t>(carry));
            }
        }
        trim();
    }

    void add_one() {
        std::uint64_t carry = 1U;
        for (std::size_t i = 0; i < limbs_.size() && carry != 0U; ++i) {
            const std::uint64_t sum = static_cast<std::uint64_t>(limbs_[i]) + carry;
            limbs_[i] = static_cast<std::uint32_t>(sum & 0xFFFFFFFFULL);
            carry = sum >> 32U;
        }
        if (carry != 0U) {
            limbs_.push_back(static_cast<std::uint32_t>(carry));
        }
        trim();
    }

    std::size_t bit_length() const {
        for (std::size_t i = limbs_.size(); i > 0; --i) {
            const std::uint32_t value = limbs_[i - 1U];
            if (value == 0U) {
                continue;
            }
            std::size_t bits = 0U;
            std::uint32_t x = value;
            while (x != 0U) {
                ++bits;
                x >>= 1U;
            }
            return (i - 1U) * 32U + bits;
        }
        return 0U;
    }

    std::uint8_t get_bit(std::size_t index) const {
        const std::size_t limb_index = index / 32U;
        if (limb_index >= limbs_.size()) {
            return 0U;
        }
        const std::size_t bit_offset = index % 32U;
        return static_cast<std::uint8_t>((limbs_[limb_index] >> bit_offset) & 1U);
    }

private:
    bool is_zero() const {
        return limbs_.size() == 1U && limbs_[0] == 0U;
    }

    void trim() {
        while (limbs_.size() > 1U && limbs_.back() == 0U) {
            limbs_.pop_back();
        }
    }

    std::vector<std::uint32_t> limbs_;
};

std::unordered_map<int, std::vector<std::uint8_t>> build_canonical_codes(
    const std::array<std::uint8_t, 256>& code_lengths,
    std::size_t& used_symbols,
    std::uint8_t& max_length) {
    std::vector<std::pair<int, std::uint8_t>> sorted;
    sorted.reserve(256);
    max_length = 0U;

    for (int symbol = 0; symbol < 256; ++symbol) {
        const std::uint8_t length = code_lengths[static_cast<std::size_t>(symbol)];
        if (length == 0U) {
            continue;
        }
        if (length > 255U) {
            throw std::runtime_error("Huffman code length too large to serialize.");
        }
        sorted.emplace_back(symbol, length);
        max_length = std::max(max_length, length);
    }

    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second < b.second;
        }
        return a.first < b.first;
    });

    used_symbols = sorted.size();
    std::unordered_map<int, std::vector<std::uint8_t>> codes;
    codes.reserve(sorted.size());

    if (sorted.empty()) {
        return codes;
    }

    BigUInt current_code;
    std::uint8_t previous_length = 0U;

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const int symbol = sorted[i].first;
        const std::uint8_t length = sorted[i].second;
        if (length < previous_length) {
            throw std::runtime_error("Invalid Huffman code length ordering.");
        }
        current_code.left_shift(static_cast<std::uint16_t>(length - previous_length));
        std::vector<std::uint8_t> bits(length, 0U);
        if (current_code.bit_length() > static_cast<std::size_t>(length)) {
            throw std::runtime_error("Invalid Huffman code lengths: code exceeds declared width.");
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(length); ++i) {
            bits[static_cast<std::size_t>(length) - 1U - i] = current_code.get_bit(i);
        }
        codes[symbol] = std::move(bits);
        current_code.add_one();
        previous_length = length;
    }

    return codes;
}

std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Input file not found: " + path.string());
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        throw std::runtime_error("Failed to get file size: " + path.string());
    }
    in.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!in) {
            throw std::runtime_error("Failed to read file: " + path.string());
        }
    }
    return data;
}

void write_file_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open output file: " + path.string());
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            throw std::runtime_error("Failed to write output file: " + path.string());
        }
    }
}

std::filesystem::path default_decompress_output(const std::filesystem::path& in_path) {
    const std::string suffix = in_path.has_extension() ? in_path.extension().string() : "";
    std::string suffix_lower;
    suffix_lower.reserve(suffix.size());
    for (char c : suffix) {
        suffix_lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (suffix_lower == ".huf") {
        std::filesystem::path candidate = in_path;
        candidate.replace_extension("");
        if (candidate.has_extension()) {
            return candidate;
        }
        return candidate.parent_path() / (candidate.filename().string() + "_decoded.txt");
    }
    return std::filesystem::path(in_path.string() + ".decoded.txt");
}

}  // namespace

double CompressionStats::compression_ratio() const {
    if (original_size == 0U) {
        return 0.0;
    }
    return static_cast<double>(compressed_size) / static_cast<double>(original_size);
}

double CompressionStats::saving_ratio() const {
    if (original_size == 0U) {
        return 0.0;
    }
    return 1.0 - compression_ratio();
}

std::string format_size(std::size_t size) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (size < 1024U) {
        return std::to_string(size) + " B";
    }
    if (size < 1024U * 1024U) {
        oss << static_cast<double>(size) / 1024.0;
        return oss.str() + " KB";
    }
    oss << static_cast<double>(size) / (1024.0 * 1024.0);
    return oss.str() + " MB";
}

std::vector<std::uint8_t> compress_bytes(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> header(kMagic.begin(), kMagic.end());

    if (data.empty()) {
        push_u64_be(header, 0U);
        push_u16_be(header, 0U);
        push_u64_be(header, 0U);
        return header;
    }

    std::array<std::uint64_t, 256> freq_map{};
    for (std::uint8_t value : data) {
        ++freq_map[value];
    }

    std::vector<Node> arena;
    arena.reserve(512);
    Node* root = build_tree(freq_map, arena);

    std::array<std::uint8_t, 256> code_lengths{};
    collect_code_lengths(root, 0U, code_lengths);

    std::size_t used_symbols = 0;
    std::uint8_t max_length = 0;
    auto codes = build_canonical_codes(code_lengths, used_symbols, max_length);
    (void)max_length;

    if (used_symbols > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Too many symbols in Huffman table.");
    }

    BitWriter writer;
    for (std::uint8_t value : data) {
        const auto it = codes.find(static_cast<int>(value));
        if (it == codes.end()) {
            throw std::runtime_error("Missing code for symbol during compression.");
        }
        writer.write(it->second);
    }

    std::vector<std::uint8_t> payload = writer.finish();

    push_u64_be(header, static_cast<std::uint64_t>(data.size()));
    push_u16_be(header, static_cast<std::uint16_t>(used_symbols));

    for (int symbol = 0; symbol < 256; ++symbol) {
        const std::uint8_t length = code_lengths[static_cast<std::size_t>(symbol)];
        if (length == 0U) {
            continue;
        }
        header.push_back(static_cast<std::uint8_t>(symbol));
        header.push_back(length);
    }

    push_u64_be(header, writer.total_bits());
    header.insert(header.end(), payload.begin(), payload.end());
    return header;
}

std::vector<std::uint8_t> decompress_bytes(const std::vector<std::uint8_t>& blob) {
    if (blob.size() < 4U + 10U + 8U) {
        throw std::runtime_error("Invalid compressed file: too short.");
    }

    std::size_t cursor = 0U;
    if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin())) {
        throw std::runtime_error("Invalid compressed file: bad magic.");
    }
    cursor += 4U;

    const std::uint64_t original_size = read_u64_be(blob, cursor);
    const std::uint16_t symbol_count = read_u16_be(blob, cursor + 8U);
    cursor += 10U;

    std::array<std::uint8_t, 256> code_lengths{};
    for (std::uint16_t i = 0; i < symbol_count; ++i) {
        if (cursor + 2U > blob.size()) {
            throw std::runtime_error("Invalid compressed file: truncated symbol table.");
        }
        const std::uint8_t symbol = blob[cursor];
        const std::uint8_t length = blob[cursor + 1U];
        cursor += 2U;
        if (length == 0U) {
            throw std::runtime_error("Invalid compressed file: zero-length code.");
        }
        code_lengths[symbol] = length;
    }

    if (cursor + 8U > blob.size()) {
        throw std::runtime_error("Invalid compressed file: missing bit-length field.");
    }
    const std::uint64_t payload_bits = read_u64_be(blob, cursor);
    cursor += 8U;

    const std::uint64_t expected_payload_bytes_u64 = (payload_bits + 7U) / 8U;
    if (expected_payload_bytes_u64 > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid compressed file: payload too large.");
    }
    const std::size_t expected_payload_bytes = static_cast<std::size_t>(expected_payload_bytes_u64);

    if (blob.size() < cursor + expected_payload_bytes) {
        throw std::runtime_error("Invalid compressed file: payload truncated.");
    }

    std::vector<std::uint8_t> payload;
    payload.insert(payload.end(), blob.begin() + static_cast<std::ptrdiff_t>(cursor),
                   blob.begin() + static_cast<std::ptrdiff_t>(cursor + expected_payload_bytes));

    if (original_size == 0U) {
        if (symbol_count != 0U || payload_bits != 0U) {
            throw std::runtime_error("Invalid compressed file: inconsistent empty header.");
        }
        return {};
    }

    std::size_t used_symbols = 0;
    std::uint8_t max_length = 0;
    auto codes = build_canonical_codes(code_lengths, used_symbols, max_length);
    if (codes.empty()) {
        throw std::runtime_error("Invalid compressed file: no symbols for non-empty payload.");
    }

    std::unordered_map<std::string, std::uint8_t> decode_map;
    decode_map.reserve(codes.size());
    for (const auto& kv : codes) {
        const int symbol = kv.first;
        const std::vector<std::uint8_t>& bits = kv.second;
        std::string key;
        key.reserve(bits.size());
        for (std::uint8_t bit : bits) {
            key.push_back(bit == 0U ? '0' : '1');
        }
        decode_map[key] = static_cast<std::uint8_t>(symbol);
    }

    BitReader reader(payload, payload_bits);
    std::vector<std::uint8_t> result;
    if (original_size > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Invalid compressed file: original size too large.");
    }
    result.reserve(static_cast<std::size_t>(original_size));

    while (result.size() < static_cast<std::size_t>(original_size)) {
        std::string current_code;
        current_code.reserve(max_length);
        bool matched = false;
        for (std::uint8_t bit_length = 1U; bit_length <= max_length; ++bit_length) {
            const int bit = reader.read();
            if (bit < 0) {
                throw std::runtime_error("Invalid compressed file: unexpected end of bitstream.");
            }
            current_code.push_back(bit == 0 ? '0' : '1');
            const auto it = decode_map.find(current_code);
            if (it != decode_map.end()) {
                result.push_back(it->second);
                matched = true;
                break;
            }
        }
        if (!matched) {
            throw std::runtime_error("Invalid compressed file: undecodable stream.");
        }
    }

    return result;
}

CompressionStats compress_file(const std::filesystem::path& input_path,
                               const std::filesystem::path& output_path) {
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error("Input file not found: " + input_path.string());
    }

    const std::filesystem::path out_path = output_path.empty()
                                               ? std::filesystem::path(input_path.string() + ".huf")
                                               : output_path;

    const auto raw = read_file_bytes(input_path);
    const auto compressed = compress_bytes(raw);
    write_file_bytes(out_path, compressed);

    CompressionStats stats;
    stats.input_path = input_path;
    stats.output_path = out_path;
    stats.original_size = raw.size();
    stats.compressed_size = compressed.size();
    return stats;
}

DecompressionStats decompress_file(const std::filesystem::path& input_path,
                                   const std::filesystem::path& output_path) {
    if (!std::filesystem::exists(input_path)) {
        throw std::runtime_error("Input file not found: " + input_path.string());
    }

    const std::filesystem::path out_path = output_path.empty() ? default_decompress_output(input_path) : output_path;

    const auto compressed = read_file_bytes(input_path);
    const auto restored = decompress_bytes(compressed);
    write_file_bytes(out_path, restored);

    DecompressionStats stats;
    stats.input_path = input_path;
    stats.output_path = out_path;
    stats.compressed_size = compressed.size();
    stats.restored_size = restored.size();
    return stats;
}

}  // namespace huffman
