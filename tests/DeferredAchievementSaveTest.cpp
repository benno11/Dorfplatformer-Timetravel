#include "DeferredAchievementSave.h"
#include <stdexcept>
static void require(bool value) { if (!value) throw std::runtime_error("Deferred achievement save regression"); }
int main() {
    DeferredAchievementSave save;
    require(!save.pending() && !save.due(10000));
    save.mark(0); require(save.pending() && !save.due(999));
    save.mark(900); require(!save.due(1000) && save.due(1900));
    save.saved(); require(!save.pending());
    int writes = 0;
    for (uint64_t now = 0; now <= 30000; now += 100) {
        save.mark(now);
        if (save.due(now)) { ++writes; save.saved(); }
    }
    require(writes == 2); // 301 pickups, bounded batches, no per-coin writes.
    require(save.pending());
    save.saved(); require(!save.pending()); // Forced lifecycle flush.
    save.mark(40000); require(save.due(41000));
    save.failed(41000); require(save.pending() && !save.due(41001));
    require(save.due(42000)); save.saved(); require(!save.pending());
}
