#include <iostream>
#include "server.hpp"

int main(int /*argc*/, char * /*argv*/[]) { 
    try {
        RTPServer server(5004); 
        server.run();
    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " + std::string(e.what()) << std::endl;
        return 1;
    }
    return 0;
}