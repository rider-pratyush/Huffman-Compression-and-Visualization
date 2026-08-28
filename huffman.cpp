#include "huffman.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>

namespace {
constexpr char MAGIC[] = {'H', 'U', 'F', '1'};
constexpr std::uint8_t VERSION = 1;
constexpr std::uint8_t STORED = 0;
constexpr std::uint8_t HUFFMAN = 1;

template <typename T>
void writeLittleEndian(std::ostream& output, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        output.put(static_cast<char>((value >> (i * 8)) & 0xFF));
    }
}

template <typename T>
T readLittleEndian(std::istream& input) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = input.get();
        if (byte == EOF) {
            throw std::runtime_error("Archive is incomplete or corrupted.");
        }
        value |= static_cast<T>(static_cast<std::uint8_t>(byte)) << (i * 8);
    }
    return value;
}

// ---------- BitWriter ---------------------------------------------------------
// Packs individual bits into full 8-bit bytes with proper padding handling.
// An internal 8 KB buffer batches writes so each put() is not a system call.
class BitWriter {
public:
    explicit BitWriter(std::ostream& output) : output_(output) {}

    ~BitWriter() {
        // Safety net: flush anything remaining if the caller forgot finish().
        if (bufferPos_ > 0) {
            flushBuffer();
        }
    }

    // Write a single bit (true = 1, false = 0).
    void writeBit(bool bit) {
        bitAccum_ = static_cast<std::uint8_t>((bitAccum_ << 1) | (bit ? 1 : 0));
        ++bitsInAccum_;
        ++totalBitsWritten_;
        if (bitsInAccum_ == 8) {
            pushByte(bitAccum_);
            bitAccum_ = 0;
            bitsInAccum_ = 0;
        }
    }

    // Write a string of '0'/'1' characters as individual bits.
    void write(const std::string& bits) {
        for (const char c : bits) {
            writeBit(c == '1');
        }
    }

    // Write a raw byte as 8 bits (MSB first).
    void writeByte(std::uint8_t byte) {
        if (bitsInAccum_ == 0) {
            // Fast path: bit-cursor is byte-aligned, push directly.
            pushByte(byte);
            totalBitsWritten_ += 8;
        } else {
            for (int i = 7; i >= 0; --i) {
                writeBit(((byte >> i) & 1) != 0);
            }
        }
    }

    // Flush remaining bits with zero-padding and write the internal buffer.
    // Returns the number of padding bits appended to the final byte (0–7).
    int finish() {
        int padding = 0;
        if (bitsInAccum_ != 0) {
            padding = 8 - bitsInAccum_;
            bitAccum_ = static_cast<std::uint8_t>(bitAccum_ << padding);
            pushByte(bitAccum_);
            bitAccum_ = 0;
            bitsInAccum_ = 0;
        }
        flushBuffer();
        paddingBits_ = padding;
        return padding;
    }

    std::uint64_t totalBitsWritten() const { return totalBitsWritten_; }
    int            paddingBits()      const { return paddingBits_; }

private:
    void pushByte(std::uint8_t byte) {
        ioBuffer_[bufferPos_++] = static_cast<char>(byte);
        if (bufferPos_ == ioBuffer_.size()) {
            flushBuffer();
        }
    }

    void flushBuffer() {
        if (bufferPos_ > 0) {
            output_.write(ioBuffer_.data(), static_cast<std::streamsize>(bufferPos_));
            bufferPos_ = 0;
        }
    }

    std::ostream&                output_;
    std::array<char, 8 * 1024>   ioBuffer_{};    // 8 KB I/O buffer
    std::size_t                  bufferPos_{0};
    std::uint8_t                 bitAccum_{0};    // accumulates bits until a full byte
    int                          bitsInAccum_{0};
    int                          paddingBits_{0};
    std::uint64_t                totalBitsWritten_{0};
};

// ---------- BitReader ---------------------------------------------------------
// Reads individual bits from packed bytes with an internal 8 KB read-ahead buffer.
class BitReader {
public:
    explicit BitReader(std::istream& input) : input_(input) {}

    // Read a single bit.  Returns true for 1, false for 0.
    bool read() {
        if (bitsRemaining_ == 0) {
            if (bufferPos_ >= bufferEnd_) {
                refillBuffer();
            }
            if (bufferPos_ >= bufferEnd_) {
                throw std::runtime_error("Archive ended before all data could be decoded.");
            }
            currentByte_ = static_cast<std::uint8_t>(ioBuffer_[bufferPos_++]);
            bitsRemaining_ = 8;
        }
        --bitsRemaining_;
        ++totalBitsRead_;
        return ((currentByte_ >> bitsRemaining_) & 1U) != 0;
    }

    // Read 8 bits and return them packed as a byte (MSB first).
    std::uint8_t readByte() {
        if (bitsRemaining_ == 0) {
            // Fast path: bit-cursor is byte-aligned, grab a whole byte directly.
            if (bufferPos_ >= bufferEnd_) {
                refillBuffer();
            }
            if (bufferPos_ >= bufferEnd_) {
                throw std::runtime_error("Archive ended before all data could be decoded.");
            }
            totalBitsRead_ += 8;
            return static_cast<std::uint8_t>(ioBuffer_[bufferPos_++]);
        }
        // Slow path: not aligned, read bit-by-bit.
        std::uint8_t byte = 0;
        for (int i = 7; i >= 0; --i) {
            if (read()) {
                byte |= static_cast<std::uint8_t>(1U << i);
            }
        }
        return byte;
    }

    std::uint64_t totalBitsRead() const { return totalBitsRead_; }

private:
    void refillBuffer() {
        input_.read(ioBuffer_.data(), static_cast<std::streamsize>(ioBuffer_.size()));
        bufferEnd_ = static_cast<std::size_t>(input_.gcount());
        bufferPos_ = 0;
    }

    std::istream&                input_;
    std::array<char, 8 * 1024>   ioBuffer_{};    // 8 KB read-ahead buffer
    std::size_t                  bufferPos_{0};
    std::size_t                  bufferEnd_{0};
    std::uint8_t                 currentByte_{0};
    int                          bitsRemaining_{0};
    std::uint64_t                totalBitsRead_{0};
};

void copyExactly(std::istream& input, std::ostream& output, std::uint64_t count) {
    std::array<char, 64 * 1024> buffer{};
    while (count > 0) {
        const std::streamsize requested = static_cast<std::streamsize>(
            std::min<std::uint64_t>(count, buffer.size()));
        input.read(buffer.data(), requested);
        const std::streamsize received = input.gcount();
        if (received != requested) {
            throw std::runtime_error("File ended unexpectedly while reading data.");
        }
        output.write(buffer.data(), received);
        count -= static_cast<std::uint64_t>(received);
    }
}
} // namespace

void huffman::collectStatistics() {
    frequencies.fill(0);
    std::ifstream input(inFileName, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open input file: " + inFileName);
    }

    std::array<char, 64 * 1024> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        for (std::streamsize i = 0; i < input.gcount(); ++i) {
            ++frequencies[static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)])];
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Could not finish reading input file: " + inFileName);
    }
}

void huffman::buildTree() {
    nodes.clear();
    root = nullptr;
    codes.fill("");

    struct Compare {
        bool operator()(const Node* left, const Node* right) const {
            if (left->frequency != right->frequency) {
                return left->frequency > right->frequency;
            }
            return left->smallestSymbol > right->smallestSymbol;
        }
    };
    std::priority_queue<Node*, std::vector<Node*>, Compare> queue;

    for (std::size_t symbol = 0; symbol < frequencies.size(); ++symbol) {
        if (frequencies[symbol] == 0) {
            continue;
        }
        auto leaf = std::make_unique<Node>();
        leaf->symbol = static_cast<std::uint8_t>(symbol);
        leaf->frequency = frequencies[symbol];
        leaf->smallestSymbol = static_cast<std::uint16_t>(symbol);
        queue.push(leaf.get());
        nodes.push_back(std::move(leaf));
    }

    if (queue.empty()) {
        return;
    }
    while (queue.size() > 1) {
        Node* left = queue.top();
        queue.pop();
        Node* right = queue.top();
        queue.pop();
        auto parent = std::make_unique<Node>();
        parent->frequency = left->frequency + right->frequency;
        parent->smallestSymbol = std::min(left->smallestSymbol, right->smallestSymbol);
        parent->left = left;
        parent->right = right;
        queue.push(parent.get());
        nodes.push_back(std::move(parent));
    }
    root = queue.top();
    createCodes(root, "");
}

void huffman::createCodes(Node* node, const std::string& code) {
    if (node->isLeaf()) {
        codes[node->symbol] = code;
        return;
    }
    createCodes(node->left, code + '0');
    createCodes(node->right, code + '1');
}

std::uint64_t huffman::encodedBitCount() const {
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        const auto codeLength = static_cast<std::uint64_t>(codes[i].size());
        if (codeLength != 0 && frequencies[i] > std::numeric_limits<std::uint64_t>::max() / codeLength) {
            throw std::runtime_error("Input file is too large to archive.");
        }
        const std::uint64_t bits = frequencies[i] * codeLength;
        if (result > std::numeric_limits<std::uint64_t>::max() - bits) {
            throw std::runtime_error("Input file is too large to archive.");
        }
        result += bits;
    }
    return result;
}

std::string huffman::archiveFilename() const {
    const std::string name = std::filesystem::path(inFileName).filename().u8string();
    return name.empty() ? "file" : name;
}

std::size_t huffman::usedSymbolCount() const {
    return static_cast<std::size_t>(std::count_if(
        frequencies.begin(), frequencies.end(), [](std::uint64_t value) { return value != 0; }));
}

void huffman::writeHeader(std::ostream& output, std::uint8_t method,
                          std::uint64_t originalSize, const std::string& filename) const {
    if (filename.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Original filename is too long to archive.");
    }
    output.write(MAGIC, sizeof(MAGIC));
    output.put(static_cast<char>(VERSION));
    output.put(static_cast<char>(method));
    writeLittleEndian<std::uint16_t>(output, 0); // reserved for future HUF1 features
    writeLittleEndian<std::uint64_t>(output, originalSize);
    writeLittleEndian<std::uint16_t>(output, static_cast<std::uint16_t>(filename.size()));
    output.write(filename.data(), static_cast<std::streamsize>(filename.size()));
}

huffman::Header huffman::readHeader(std::istream& input) {
    char magic[sizeof(MAGIC)]{};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic)) ||
        !std::equal(std::begin(magic), std::end(magic), std::begin(MAGIC))) {
        throw std::runtime_error("This is not a supported HUF1 archive.");
    }
    const int version = input.get();
    const int method = input.get();
    if (version != VERSION || (method != STORED && method != HUFFMAN)) {
        throw std::runtime_error("This archive uses an unsupported HUF version or method.");
    }
    (void)readLittleEndian<std::uint16_t>(input); // reserved
    Header header;
    header.method = static_cast<std::uint8_t>(method);
    header.originalSize = readLittleEndian<std::uint64_t>(input);
    const auto filenameLength = readLittleEndian<std::uint16_t>(input);
    header.originalFilename.resize(filenameLength);
    input.read(header.originalFilename.data(), filenameLength);
    if (input.gcount() != static_cast<std::streamsize>(filenameLength)) {
        throw std::runtime_error("Archive is incomplete or corrupted.");
    }
    return header;
}

void huffman::writeFrequencyTable(std::ostream& output) const {
    writeLittleEndian<std::uint16_t>(output, static_cast<std::uint16_t>(usedSymbolCount()));
    for (std::size_t symbol = 0; symbol < frequencies.size(); ++symbol) {
        if (frequencies[symbol] != 0) {
            output.put(static_cast<char>(symbol));
            writeLittleEndian<std::uint64_t>(output, frequencies[symbol]);
        }
    }
}

void huffman::readFrequencyTable(std::istream& input) {
    frequencies.fill(0);
    const auto count = readLittleEndian<std::uint16_t>(input);
    if (count == 0 || count > 256) {
        throw std::runtime_error("Archive contains an invalid Huffman table.");
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const int symbol = input.get();
        if (symbol == EOF || frequencies[static_cast<std::uint8_t>(symbol)] != 0) {
            throw std::runtime_error("Archive contains an invalid Huffman table.");
        }
        const auto frequency = readLittleEndian<std::uint64_t>(input);
        if (frequency == 0) {
            throw std::runtime_error("Archive contains an invalid Huffman table.");
        }
        frequencies[static_cast<std::uint8_t>(symbol)] = frequency;
    }
    buildTree();
}

void huffman::compress() {
    collectStatistics();
    buildTree();

    std::uint64_t originalSize = 0;
    for (const auto frequency : frequencies) {
        if (originalSize > std::numeric_limits<std::uint64_t>::max() - frequency) {
            throw std::runtime_error("Input file is too large to archive.");
        }
        originalSize += frequency;
    }
    const std::string filename = archiveFilename();
    const std::uint64_t encodedBits = encodedBitCount();
    const std::uint64_t encodedBytes = encodedBits / 8 + (encodedBits % 8 != 0 ? 1 : 0);
    const std::uint64_t huffmanOverhead = 2 + static_cast<std::uint64_t>(usedSymbolCount()) * 9;
    const bool useHuffman = root != nullptr && encodedBytes + huffmanOverhead < originalSize;

    std::ofstream output(outFileName, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create archive: " + outFileName);
    }
    writeHeader(output, useHuffman ? HUFFMAN : STORED, originalSize, filename);

    std::ifstream input(inFileName, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not reopen input file: " + inFileName);
    }
    if (!useHuffman) {
        copyExactly(input, output, originalSize);
    } else {
        writeFrequencyTable(output);
        BitWriter writer(output);
        std::array<char, 64 * 1024> buffer{};
        while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
            for (std::streamsize i = 0; i < input.gcount(); ++i) {
                writer.write(codes[static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)])]);
            }
        }
        if (!input.eof()) {
            throw std::runtime_error("Could not finish reading input file: " + inFileName);
        }
        writer.finish();
    }
    if (!output) {
        throw std::runtime_error("Could not finish writing archive: " + outFileName);
    }
}

void huffman::decompress() {
    std::ifstream input(inFileName, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open archive: " + inFileName);
    }
    const Header header = readHeader(input);

    std::filesystem::path target = outFileName.empty()
        ? std::filesystem::path(inFileName).parent_path() /
              std::filesystem::path(header.originalFilename).filename()
        : std::filesystem::path(outFileName);
    if (target.filename().empty()) {
        throw std::runtime_error("Archive does not contain a usable output filename.");
    }
    if (outFileName.empty() && std::filesystem::exists(target)) {
        throw std::runtime_error("Refusing to overwrite existing file: " + target.string() +
                                 ". Choose an output filename instead.");
    }
    if (header.method == HUFFMAN) {
        readFrequencyTable(input);
        std::uint64_t frequencyTotal = 0;
        for (const auto frequency : frequencies) {
            if (frequencyTotal > std::numeric_limits<std::uint64_t>::max() - frequency) {
                throw std::runtime_error("Archive contains an invalid Huffman table.");
            }
            frequencyTotal += frequency;
        }
        if (frequencyTotal != header.originalSize || root == nullptr) {
            throw std::runtime_error("Archive size and Huffman table do not match.");
        }
    }
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create output file: " + target.string());
    }

    if (header.method == STORED) {
        copyExactly(input, output, header.originalSize);
    } else {
        if (root->isLeaf()) {
            for (std::uint64_t i = 0; i < header.originalSize; ++i) {
                output.put(static_cast<char>(root->symbol));
            }
        } else {
            BitReader reader(input);
            Node* current = root;
            std::uint64_t written = 0;
            while (written < header.originalSize) {
                current = reader.read() ? current->right : current->left;
                if (current == nullptr) {
                    throw std::runtime_error("Archive contains invalid Huffman data.");
                }
                if (current->isLeaf()) {
                    output.put(static_cast<char>(current->symbol));
                    current = root;
                    ++written;
                }
            }
        }
    }
    if (!output) {
        throw std::runtime_error("Could not finish writing output file: " + target.string());
    }
    restoredFileName = target.string();
}
