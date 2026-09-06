#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <numeric>
#include <random>
#include <vector>

namespace ct {
enum class Repeat { Off, All, One };
class Order {
public:
    void reset(uint32_t count) {
        base_.clear(); tracks_.resize(count);
        std::iota(tracks_.begin(), tracks_.end(), 0);
        scoped_ = false; resetPosition();
    }
    void reset(const std::vector<uint32_t>& ids) {
        base_ = ids; tracks_ = ids; scoped_ = true;
        resetPosition();
    }
    void seed(uint32_t seed) { rng_.seed(seed); }
    void grow(uint32_t count) {
        if (scoped_) return;
        for (uint32_t id = tracks_.size(); id < count; ++id) tracks_.push_back(id);
    }
    void shuffle(bool enabled) {
        shuffle_ = enabled;
        if (enabled) std::shuffle(tracks_.begin(), tracks_.end(), rng_);
        else if (scoped_) tracks_ = base_;
        else std::sort(tracks_.begin(), tracks_.end());
        // Put the current track first when enabling shuffle, leaving every
        // other track in the upcoming cycle exactly once.
        auto found = std::find(tracks_.begin(), tracks_.end(), uint32_t(current_));
        if (enabled && found != tracks_.end()) std::iter_swap(tracks_.begin(), found);
        syncCursor();
    }
    bool shuffled() const { return shuffle_; }
    void repeat(Repeat value) { repeat_ = value; }
    Repeat repeat() const { return repeat_; }
    int current() const { return current_; }
    bool contains(uint32_t id) const { return scoped_ ? std::find(base_.begin(), base_.end(), id) != base_.end() : id < tracks_.size(); }
    int first() const { return tracks_.empty() ? -1 : int(tracks_.front()); }
    size_t size() const { return tracks_.size(); }
    void replace(const std::vector<uint32_t>& ids) {
        int current = current_;
        reset(ids);
        if (current >= 0 && contains(current)) { current_ = current; syncCursor(); }
    }
    bool enqueue(uint32_t id) {
        if (!contains(id) || queue_.size() >= 64) return false;
        queue_.push_back(id); return true;
    }
    void clearQueue() { queue_.clear(); }
    const std::deque<uint32_t>& queue() const { return queue_; }
    int select(uint32_t id) {
        if (!contains(id)) return -1;
        remember(); current_ = id; syncCursor(); return current_;
    }
    int next(bool finished = false) {
        if (tracks_.empty()) return -1;
        if (finished && repeat_ == Repeat::One && current_ >= 0) return current_;
        if (!queue_.empty()) { auto id = queue_.front(); queue_.pop_front(); return select(id); }
        int next = cursor_ + 1;
        if (next >= int(tracks_.size())) {
            if (repeat_ == Repeat::Off) return -1;
            next = 0;
        }
        return select(tracks_[next]);
    }
    int previous() {
        if (!history_.empty()) {
            current_ = history_.back(); history_.pop_back(); syncCursor(); return current_;
        }
        if (tracks_.empty()) return -1;
        current_ = tracks_[cursor_ > 0 ? cursor_ - 1 : 0]; syncCursor(); return current_;
    }
private:
    void resetPosition() {
        queue_.clear(); history_.clear(); cursor_ = -1; current_ = -1;
        if (shuffle_) std::shuffle(tracks_.begin(), tracks_.end(), rng_);
    }
    void remember() {
        if (current_ >= 0) { if (history_.size() >= 64) history_.pop_front(); history_.push_back(current_); }
    }
    void syncCursor() {
        auto it = std::find(tracks_.begin(), tracks_.end(), uint32_t(current_));
        cursor_ = it == tracks_.end() ? -1 : int(it - tracks_.begin());
    }
    std::vector<uint32_t> base_, tracks_;
    std::deque<uint32_t> queue_;
    std::deque<int> history_;
    std::mt19937 rng_{1};
    int cursor_ = -1, current_ = -1;
    bool shuffle_ = false;
    bool scoped_ = false;
    Repeat repeat_ = Repeat::All;
};
}
