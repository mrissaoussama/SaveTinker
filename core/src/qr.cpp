#include "savetinker/qr.hpp"

#include <qrcodegen.hpp>
#include <string>

namespace st {

std::optional<QrCode> makeQrCode(std::string_view text) {
    try {
        const auto code = qrcodegen::QrCode::encodeText(std::string(text).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        QrCode result;
        result.size = code.getSize();
        result.modules.reserve(static_cast<std::size_t>(result.size * result.size));
        for (int y = 0; y < result.size; ++y)
            for (int x = 0; x < result.size; ++x) result.modules.push_back(code.getModule(x, y));
        return result;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

}
