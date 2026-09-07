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
        base_ = ids; tracks_.clear(); scoped_ = true;
        for (uint32_t i = 0; i < ids.size(); ++i) if (ids[i] != UINT32_MAX) tracks_.push_back(i);
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
        else std::sort(tracks_.begin(), tracks_.end());
        // Put the current track first when enabling shuffle, leaving every
        // other track in the upcoming cycle exactly once.
        auto found = std::find(tracks_.begin(), tracks_.end(), uint32_t(currentEntry_));
        if (enabled && found != tracks_.end()) std::iter_swap(tracks_.begin(), found);
        syncCursor();
    }
    bool shuffled() const { return shuffle_; }
    void repeat(Repeat value) { repeat_ = value; }
    Repeat repeat() const { return repeat_; }
    int current() const { return current_; }
    int currentPosition() const { return currentEntry_; }
    int trackAt(uint32_t entry) const { return scoped_ ? (entry < base_.size() && base_[entry] != UINT32_MAX ? int(base_[entry]) : -1) : (entry < tracks_.size() ? int(entry) : -1); }
    bool contains(uint32_t id) const { return id != UINT32_MAX && (scoped_ ? std::find(base_.begin(), base_.end(), id) != base_.end() : id < tracks_.size()); }
    int first() const { return tracks_.empty() ? -1 : trackAt(tracks_.front()); }
    int firstPosition() const { return tracks_.empty() ? -1 : int(tracks_.front()); }
    size_t size() const { return tracks_.size(); }
    void replace(const std::vector<uint32_t>& ids) { replace(ids, currentEntry_); }
    void replace(const std::vector<uint32_t>& ids, int entry) {
        int current = current_;
        reset(ids);
        if (current >= 0 && entry >= 0 && trackAt(entry) == current) selectPosition(entry);
    }
    bool refreshAvailable(const std::vector<uint32_t>& ids) {
        if (!scoped_ || ids.size() != base_.size()) return false;
        for (size_t i = 0; i < ids.size(); ++i)
            if (base_[i] != UINT32_MAX && base_[i] != ids[i]) return false;
        // Uploads only fill holes. Keep existing shuffle order, history and queue.
        for (uint32_t i = 0; i < ids.size(); ++i)
            if (base_[i] == UINT32_MAX && ids[i] != UINT32_MAX) tracks_.push_back(i);
        base_ = ids;
        if (!shuffle_) std::sort(tracks_.begin(), tracks_.end());
        syncCursor(); return true;
    }
    bool enqueue(uint32_t id) {
        if (!contains(id) || queue_.size() >= 64) return false;
        queue_.push_back(id); return true;
    }
    void clearQueue() { queue_.clear(); }
    const std::deque<uint32_t>& queue() const { return queue_; }
    int select(uint32_t id) {
        if (!contains(id)) return -1;
        uint32_t entry = scoped_ ? std::find(base_.begin(), base_.end(), id) - base_.begin() : id;
        return selectPosition(entry);
    }
    int selectPosition(uint32_t entry) {
        int track = trackAt(entry);
        if (track < 0) return -1;
        remember(); currentEntry_ = entry; current_ = track; syncCursor(); return current_;
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
        return selectPosition(tracks_[next]);
    }
    int previous() {
        if (!history_.empty()) {
            currentEntry_ = history_.back(); history_.pop_back(); current_ = trackAt(currentEntry_); syncCursor(); return current_;
        }
        if (tracks_.empty()) return -1;
        currentEntry_ = tracks_[cursor_ > 0 ? cursor_ - 1 : 0]; current_ = trackAt(currentEntry_); syncCursor(); return current_;
    }
private:
    void resetPosition() {
        queue_.clear(); history_.clear(); cursor_ = -1; current_ = currentEntry_ = -1;
        if (shuffle_) std::shuffle(tracks_.begin(), tracks_.end(), rng_);
    }
    void remember() {
        if (currentEntry_ >= 0) { if (history_.size() >= 64) history_.pop_front(); history_.push_back(currentEntry_); }
    }
    void syncCursor() {
        auto it = std::find(tracks_.begin(), tracks_.end(), uint32_t(currentEntry_));
        cursor_ = it == tracks_.end() ? -1 : int(it - tracks_.begin());
    }
    std::vector<uint32_t> base_, tracks_;
    std::deque<uint32_t> queue_;
    std::deque<int> history_;
    std::mt19937 rng_{1};
    int cursor_ = -1, current_ = -1, currentEntry_ = -1;
    bool shuffle_ = false;
    bool scoped_ = false;
    Repeat repeat_ = Repeat::All;
};
}
