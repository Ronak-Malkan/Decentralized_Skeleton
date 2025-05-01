#include "server.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <node_id> <listen_addr> <neighbor1> [neighbor2 ...]\n";
        return 1;
    }

    std::string node_id     = argv[1];
    std::string listen_addr = argv[2];
    std::vector<std::string> neighbors;
    for (int i = 3; i < argc; ++i) {
        neighbors.push_back(argv[i]);
    }

    Server srv(node_id, listen_addr, neighbors);
    srv.run();
    return 0;
}
