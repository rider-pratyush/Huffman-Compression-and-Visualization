#include <iostream>
#include <fstream>
#include "huffman.hpp"
using namespace std;

// Function to get file size in bytes
long long getFileSize(const char* filename) {
    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    if (!in) return -1;
    return static_cast<long long>(in.tellg());
}


int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: decode <compressedFile> <outputFile>\n";
        return 1;
    }

    // Print input file size before decompression
    long long compressedSize = getFileSize(argv[1]);
    if (compressedSize < 0) {
        cout << "Error: Cannot open " << argv[1] << endl;
        return 1;
    }
    cout << "Compressed file size: " << compressedSize << " bytes\n";

    huffman f(argv[1], argv[2]);
    f.decompress();

    cout << "Decompressed successfully." << endl;

    // Print output file size after decompression
    long long outputSize = getFileSize(argv[2]);
    if (outputSize < 0) {
        cout << "Error: Cannot open " << argv[2] << endl;
        return 1;
    }
    cout << "Output file size: " << outputSize << " bytes\n";
    return 0;
}
