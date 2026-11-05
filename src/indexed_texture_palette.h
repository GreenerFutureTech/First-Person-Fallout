#ifndef FALLOUT_INDEXED_TEXTURE_PALETTE_H_
#define FALLOUT_INDEXED_TEXTURE_PALETTE_H_

#include <array>
#include <bitset>
#include <cstring>

namespace fallout {

// Palette dependencies of an indexed sprite expanded to an RGBA texture.
// Index zero is transparent, so its RGB value never affects the texture.
class IndexedTexturePalette {
public:
    void useColor(unsigned char index)
    {
        if (index != 0) {
            usedColors_.set(index);
        }
    }

    void capture(const unsigned char* palette)
    {
        std::memcpy(colors_.data(), palette, colors_.size());
    }

    bool matches(const unsigned char* palette) const
    {
        for (int index = 1; index < 256; index++) {
            if (usedColors_.test(index)
                && std::memcmp(colors_.data() + index * 3, palette + index * 3, 3) != 0) {
                return false;
            }
        }
        return true;
    }

private:
    std::bitset<256> usedColors_;
    std::array<unsigned char, 256 * 3> colors_ {};
};

} // namespace fallout

#endif // FALLOUT_INDEXED_TEXTURE_PALETTE_H_
