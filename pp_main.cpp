#include "preprocessor.h"
#include <iostream>
int main(int argc, char* argv[]) {
    if (argc < 2) { std::cerr << "usage: ./pp <file.s>\n"; return 1; }
    preprocess(argv[1]);
    return 0;
}