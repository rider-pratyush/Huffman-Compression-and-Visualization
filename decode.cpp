#include <iostream>
#include <filesystem>
#include <stdexcept>
#include "huffman.hpp"
using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3) {
        cerr << "Usage: decode <archive.huf> [outputfile]\n";
        return 1;
    }

    try {
        huffman archive(argv[1], argc == 3 ? argv[2] : "");
        archive.decompress();
        cout << "File restored successfully.\n";
        cout << "Output file: " << archive.getRestoredFileName() << '\n';
        cout << "Restored size: " << filesystem::file_size(archive.getRestoredFileName()) << " bytes\n";
        return 0;
    } catch (const exception& error) {
        cerr << "Decompression failed: " << error.what() << '\n';
        return 1;
    }
}
