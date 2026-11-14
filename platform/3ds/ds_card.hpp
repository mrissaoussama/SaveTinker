#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace st {

class DsCard {
public:
    enum class Chip { Eeprom512, Eeprom8K, Eeprom64K, Eeprom128K, Flash, FlashInfrared };

    struct Info {
        std::string gameCode;
        std::string title;
        Chip chip = Chip::Flash;
        std::uint32_t size = 0;
    };

    static std::optional<Info> detect();

    static std::expected<Bytes, std::string> read(const Info& card);
    static std::expected<void, std::string> write(const Info& card, ByteView data);
};

}
