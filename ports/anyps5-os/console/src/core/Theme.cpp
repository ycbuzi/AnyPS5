#include "core/Theme.h"

namespace ui {

const std::array<std::pair<Color, Color>, 6>& avatarColors() {
    static const std::array<std::pair<Color, Color>, 6> c = {{
        {hex(0x3b82f6), hex(0x22d3ee)},
        {hex(0xa855f7), hex(0xec4899)},
        {hex(0xf59e0b), hex(0xef4444)},
        {hex(0x10b981), hex(0x84cc16)},
        {hex(0x64748b), hex(0x94a3b8)},
        {hex(0xf43f5e), hex(0xfb923c)},
    }};
    return c;
}

}
