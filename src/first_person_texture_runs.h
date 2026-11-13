#ifndef FALLOUT_FIRST_PERSON_TEXTURE_RUNS_H_
#define FALLOUT_FIRST_PERSON_TEXTURE_RUNS_H_

#include <algorithm>
#include <cmath>

namespace fallout {

// Preserve the original sampling expression, including its rounding.
// Call only for nonnegative texture coordinates and positive scale.
inline int firstPersonTextureRow(int y, double top, double height, int size)
{
    return static_cast<int>((y - top) * size / height);
}

// Exclusive end of this repeated source row. Correct the analytic estimate
// with the original expression so floating-point boundaries cannot shift art.
inline int firstPersonTextureRunEnd(int y, int bottom, double top,
    double height, int size)
{
    const int row = firstPersonTextureRow(y, top, height, size);
    const double edge = top + (row + 1.0) * height / size;
    int end = static_cast<int>(std::clamp(std::ceil(edge),
        static_cast<double>(y + 1), static_cast<double>(bottom)));
    while (end > y + 1 && firstPersonTextureRow(end - 1, top, height, size) > row) --end;
    while (end < bottom && firstPersonTextureRow(end, top, height, size) == row) ++end;
    return end;
}

} // namespace fallout
#endif
