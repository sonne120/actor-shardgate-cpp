#include <iostream>

#include "sha256.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: keyhash <api-key>\n";
        return 1;
    }
    std::cout << shardgate::hash_api_key(argv[1]) << "\n";
    return 0;
}
