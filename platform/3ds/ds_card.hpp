#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "savetinker/byte_buffer.hpp"

namespace st {

// Save chip of a DS cartridge in the 3DS card slot, reached over SPI through pxi:dev.
class DsCard {
public:
    enum class Chip { Eeprom512, Eeprom8K, Eeprom64K, Eeprom128K, Flash, FlashInfrared };

    struct Info {
        std::string gameCode;
        std::string title;
        Chip chip = Chip::Flash;
        std::uint32_t size = 0;
    };

    // The inserted DS cartridge and its save chip, or nothing when there is none.
    static std::optional<Info> detect();

    static std::expected<Bytes, std::string> read(const Info& card);
    static std::expected<void, std::string> write(const Info& card, ByteView data);
};

}
