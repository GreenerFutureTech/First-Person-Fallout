#ifndef FALLOUT_SLOW_FRAME_LOG_H_
#define FALLOUT_SLOW_FRAME_LOG_H_

#include <algorithm>
#include <cstdint>

namespace fallout {

struct SlowFrameLogSample {
    bool emit = false;
    unsigned int count = 0;
    unsigned int worstMs = 0;
};

class SlowFrameLog {
public:
    SlowFrameLogSample record(uint32_t now, unsigned int durationMs)
    {
        if (durationMs < 50) return {};
        ++count_;
        worstMs_ = std::max(worstMs_, durationMs);
        // Unsigned subtraction also handles the millisecond clock wrapping.
        if (emitted_ && uint32_t(now - last_) < 1000) return {};
        const SlowFrameLogSample sample { true, count_, worstMs_ };
        emitted_ = true;
        last_ = now;
        count_ = 0;
        worstMs_ = 0;
        return sample;
    }

private:
    bool emitted_ = false;
    uint32_t last_ = 0;
    unsigned int count_ = 0;
    unsigned int worstMs_ = 0;
};

} // namespace fallout
#endif
