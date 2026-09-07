#pragma once
#include <cstdint>

namespace ct {
class KeyEdges {
public:
    uint64_t press(uint64_t held) {
        uint64_t added = held & ~previous_;
        previous_ = held;
        return added;
    }
private:
    uint64_t previous_ = 0;
};
}
