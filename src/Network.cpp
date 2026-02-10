#include "MyUtils/Network.h"
#include <iostream>

namespace MyUtils {
    void Connector::Init(std::string name) {
        std::cout << "[MyUtils] " << name << " initialized via FetchContent!" << std::endl;
    }
}