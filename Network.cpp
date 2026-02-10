#include "MyNetLib/Network.h"
#include <iostream>

namespace MyNetLib {
    void Connector::Init(std::string name) {
        std::cout << "[MyNetLib] " << name << " initialized via FetchContent!" << std::endl;
    }
}