#include <iostream>
#include <filesystem>
#include <stdexcept>
#include "huffman.hpp"
using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: encode <inputfile> <outputfile.huf>\n";
        return 1;
    }

    try {
        huffman archive(argv[1], argv[2]);
        archive.compress();

        const auto original = filesystem::file_size(argv[1]);
        const auto compressed = filesystem::file_size(argv[2]);
        cout << "Archive created successfully.\n";
        cout << "Original size: " << original << " bytes\n";
        cout << "Archive size: " << compressed << " bytes\n";
        if (original != 0) {
            cout << "Archive ratio: " << (100.0 * compressed / original) << " %\n";
        }
        return 0;
    } catch (const exception& error) {
        cerr << "Compression failed: " << error.what() << '\n';
        return 1;
    }
}
