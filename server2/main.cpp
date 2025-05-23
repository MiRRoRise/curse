#include <iostream>
#include "server.hpp"

int main(int /*argc*/, char * /*argv*/[]) { // Подавляем предупреждения, убрав имена параметров
    try {
        RTPServer server(5004); // Теперь конструктор принимает порт
        server.run();
    } catch (const std::exception &e) {
        std::cerr << "Fatal error: " + std::string(e.what()) << std::endl;
        return 1;
    }
    return 0;
}