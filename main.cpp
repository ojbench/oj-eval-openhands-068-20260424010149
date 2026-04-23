



#include "allocator.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

int main() {
    std::size_t poolSize;
    std::cin >> poolSize;
    
    TLSFAllocator allocator(poolSize);
    
    std::string command;
    while (std::cin >> command) {
        if (command == "ALLOC") {
            std::size_t size;
            std::cin >> size;
            void* ptr = allocator.allocate(size);
            if (ptr) {
                std::cout << "ALLOCATED " << ptr << std::endl;
            } else {
                std::cout << "FAILED" << std::endl;
            }
        } else if (command == "FREE") {
            void* ptr;
            std::string ptrStr;
            std::cin >> ptrStr;
            // Convert string representation of pointer back to void*
            std::stringstream ss;
            ss << std::hex << ptrStr;
            ss >> reinterpret_cast<uintptr_t&>(ptr);
            allocator.deallocate(ptr);
            std::cout << "FREED" << std::endl;
        } else if (command == "MAX") {
            std::size_t maxSize = allocator.getMaxAvailableBlockSize();
            std::cout << "MAX " << maxSize << std::endl;
        } else if (command == "POOL") {
            void* poolStart = allocator.getMemoryPoolStart();
            std::size_t poolSize = allocator.getMemoryPoolSize();
            std::cout << "POOL " << poolStart << " " << poolSize << std::endl;
        }
    }
    
    return 0;
}



