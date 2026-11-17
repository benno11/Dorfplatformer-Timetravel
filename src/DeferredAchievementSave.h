#pragma once
#include <cstdint>

// Coalesce coin pickups. Save after one quiet second, or at most every ten
// seconds during continuous collection. Lifecycle boundaries force a flush.
class DeferredAchievementSave {
public:
    void mark(uint64_t now) {
        if (!dirty_) first_ = now;
        dirty_ = true; last_ = now;
    }
    bool pending() const { return dirty_; }
    bool due(uint64_t now) const {
        return dirty_ && (!retried_ || now - attempted_ >= 1000) &&
            (now - last_ >= 1000 || now - first_ >= 10000);
    }
    void saved() { dirty_ = false; retried_ = false; }
    void failed(uint64_t now) { attempted_ = now; retried_ = true; }
private:
    bool dirty_ = false, retried_ = false;
    uint64_t first_ = 0, last_ = 0, attempted_ = 0;
};
