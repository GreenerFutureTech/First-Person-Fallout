#include "slow_frame_log.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void require(bool value) { if (!value) std::abort(); }

int main()
{
    fallout::SlowFrameLog log;
    require(!log.record(0, 49).emit);
    auto first = log.record(0, 50);
    require(first.emit && first.count == 1 && first.worstMs == 50);
    for (unsigned int tick = 1; tick < 1000; ++tick) {
        require(!log.record(tick, tick == 500 ? 900 : 60).emit);
    }
    auto next = log.record(1000, 70);
    require(next.emit && next.count == 1000 && next.worstMs == 900);
    require(!log.record(2000, 10).emit);
    auto isolated = log.record(3000, 80);
    require(isolated.emit && isolated.count == 1 && isolated.worstMs == 80);
    log = {};
    require(log.record(3001, 50).emit);
    log = {};
    const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 499;
    require(log.record(nearWrap, 50).emit);
    require(!log.record(499, 60).emit);
    auto wrapped = log.record(500, 70);
    require(wrapped.emit && wrapped.count == 2 && wrapped.worstMs == 70);
    std::puts("Slow frame log tests passed");
}
