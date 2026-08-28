#ifndef HUFFMAN_HPP
#define HUFFMAN_HPP

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// Creates and reads HUF1 archives. Files are treated as raw bytes, so text,
// PDFs, images, videos, presentations, and other binary formats are preserved.
class huffman {
private:
    struct Node {
        std::uint8_t symbol{};
        std::uint64_t frequency{};
        std::uint16_t smallestSymbol{};
        Node* left{nullptr};
        Node* right{nullptr};

        bool isLeaf() const { return left == nullptr && right == nullptr; }
    };

    struct Header {
        std::uint8_t method{};
        std::uint64_t originalSize{};
        std::string originalFilename;
    };

    std::string inFileName;
    std::string outFileName;
    std::string restoredFileName;
    std::array<std::uint64_t, 256> frequencies{};
    std::array<std::string, 256> codes{};
    std::vector<std::unique_ptr<Node>> nodes;
    Node* root{nullptr};

    void collectStatistics();
    void buildTree();
    void createCodes(Node* node, const std::string& code);
    std::uint64_t encodedBitCount() const;
    std::string archiveFilename() const;
    std::size_t usedSymbolCount() const;

    Header readHeader(std::istream& input);
    void writeHeader(std::ostream& output, std::uint8_t method,
                     std::uint64_t originalSize, const std::string& filename) const;
    void writeFrequencyTable(std::ostream& output) const;
    void readFrequencyTable(std::istream& input);

public:
    huffman(const std::string& inputFileName, const std::string& outputFileName = "")
        : inFileName(inputFileName), outFileName(outputFileName) {}

    void compress();
    void decompress();

    // Available after decompression. It is the complete path actually written.
    const std::string& getRestoredFileName() const { return restoredFileName; }
};

#endif
