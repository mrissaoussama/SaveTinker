#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace st {

struct QrCode {
    int size = 0;
    std::vector<bool> modules;

    bool dark(int x, int y) const { return modules[static_cast<std::size_t>(y * size + x)]; }
};

std::optional<QrCode> makeQrCode(std::string_view text);

}
