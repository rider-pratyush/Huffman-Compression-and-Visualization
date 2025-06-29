#include <iostream>
#include <fstream>
#include "huffman.hpp"
using namespace std;

// Returns file size in bytes, or -1 on failure
long long getFileSize(const char* filename) {
    ifstream in(filename, ios::binary | ios::ate);
    if (!in) return -1;
    return static_cast<long long>(in.tellg());
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cout << "Usage: encode <inputfile> <outputfile>\n";
        return 1;
    }
    
    huffman f(argv[1], argv[2]);
    f.compress();
    cout << "Compressed successfully" << endl;
    
    f.outputTreeAsDot("huffman_tree.dot");
    cout << "Huffman tree saved to huffman_tree.dot" << endl;

    

    long long original = getFileSize(argv[1]);
    long long compressed = getFileSize(argv[2]);
    if (original < 0) {
        cout << "Error: Could not read original file size!\n";
        return 1;
    }
    if (compressed < 0) {
        cout << "Error: Could not read compressed file size!\n";
        return 1;
    }
    double ratio = 100.0 * compressed / original;

    cout << "Original size: " << original << " bytes\n";
    cout << "Compressed size: " << compressed << " bytes\n";
    cout << "Compression ratio: " << ratio << " %\n";

    return 0;
}
