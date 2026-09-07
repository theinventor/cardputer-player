#include "order.h"
#include <cassert>
#include <set>

int main() {
    ct::Order order;
    order.reset(std::vector<uint32_t>{1, 2, 1, 3});
    order.selectPosition(2);
    order.replace({1, 1, 3}, 1); // Remove the preceding entry, not this occurrence.
    assert(order.current() == 1 && order.currentPosition() == 1);
    assert(order.next() == 3);
    order.selectPosition(1); order.replace({1, 3}, -1);
    assert(order.current() == -1); // Removing this occurrence stops even if A remains.
    order.reset(std::vector<uint32_t>{1, 2, 1, 3}); order.selectPosition(2);
    order.replace({1, 1, 2, 3}, 0); // Move the playing second A to the front.
    assert(order.currentPosition() == 0 && order.next() == 1);

    for (unsigned seed = 0; seed < 100; ++seed) {
        order.seed(seed); order.shuffle(true);
        order.reset(std::vector<uint32_t>{1, 2, 1, 3}); order.repeat(ct::Repeat::Off);
        assert(order.selectPosition(order.firstPosition()) == order.first());
        std::set<int> seen{order.currentPosition()};
        for (unsigned i = 0; i < 3; ++i) { assert(order.next() >= 0); assert(seen.insert(order.currentPosition()).second); }
        assert(order.next() == -1 && seen.size() == 4);
    }
    order.shuffle(false); order.reset(std::vector<uint32_t>{1, UINT32_MAX}); order.selectPosition(0);
    assert(order.enqueue(1));
    assert(order.refreshAvailable({1, 8})); // An upload fills a missing entry without clearing the queue.
    assert(order.currentPosition() == 0 && order.size() == 2);
    assert(order.queue().size() == 1); assert(order.next() == 1); assert(order.next() == 8);
    assert(!order.refreshAvailable({2, 8})); assert(order.current() == 8);
    for (unsigned seed = 0; seed < 100; ++seed) {
        order.seed(seed); order.shuffle(true); order.reset(std::vector<uint32_t>{1, 2, 1, UINT32_MAX});
        order.selectPosition(order.firstPosition());
        ct::Order unchanged = order;
        assert(order.refreshAvailable({1, 2, 1, 3}));
        for (unsigned i = 0; i < 2; ++i) {
            assert(order.next() == unchanged.next());
            assert(order.currentPosition() == unchanged.currentPosition());
        }
        assert(order.next() == 3 && order.currentPosition() == 3);
        assert(order.previous() == unchanged.current());
    }
}
