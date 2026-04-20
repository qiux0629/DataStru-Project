#include "huffman_codec.hpp"

#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " compress <input_file> [-o <output.huf>]\n"
              << "  " << prog << " decompress <input.huf> [-o <output_file>]\n";
}

std::string percent(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << (value * 100.0) << "%";
    return oss.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }

        const std::string command = argv[1];
        const std::string input = argv[2];
        std::string output;

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
                output = argv[++i];
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }

        if (command == "compress") {
            const auto stats = output.empty() ? huffman::compress_file(input) : huffman::compress_file(input, output);
            std::cout << "Compressed: " << stats.input_path << " -> " << stats.output_path << "\n";
            std::cout << "Original: " << stats.original_size << " bytes\n";
            std::cout << "Compressed: " << stats.compressed_size << " bytes\n";
            std::cout << "Compression ratio: " << percent(stats.compression_ratio()) << "\n";
            std::cout << "Saved ratio: " << percent(stats.saving_ratio()) << "\n";
            return 0;
        }

        if (command == "decompress") {
            const auto stats = output.empty() ? huffman::decompress_file(input) : huffman::decompress_file(input, output);
            std::cout << "Decompressed: " << stats.input_path << " -> " << stats.output_path << "\n";
            std::cout << "Compressed size: " << stats.compressed_size << " bytes\n";
            std::cout << "Restored size: " << stats.restored_size << " bytes\n";
            return 0;
        }

        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
