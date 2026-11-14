#include "ds_card.hpp"

#include <3ds.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <vector>

namespace st {

namespace {

constexpr std::uint8_t kRead = 0x03;
constexpr std::uint8_t kReadHigh = 0x0B;
constexpr std::uint8_t kWrite = 0x02;
constexpr std::uint8_t kWriteHigh = 0x0A;
constexpr std::uint8_t kFlashPageWrite = 0x0A;
constexpr std::uint8_t kWriteEnable = 0x06;
constexpr std::uint8_t kReadStatus = 0x05;
constexpr std::uint8_t kReadId = 0x9F;
constexpr std::uint8_t kBusy = 0x01;
constexpr std::uint32_t kNoChip = 0xFFFFFF;
constexpr std::uint32_t kReadChunk = 0x8000;

using Chip = DsCard::Chip;

class PxiSession {
public:
    PxiSession() : ready_(R_SUCCEEDED(pxiDevInit())) {}
    ~PxiSession() {
        if (ready_) pxiDevExit();
    }
    bool ready() const { return ready_; }

private:
    bool ready_;
};

// One chip-select transaction: command bytes out, then `answer` in, then `data` out.
Result transfer(bool infrared, const std::vector<std::uint8_t>& command, void* answer, std::uint32_t answerSize,
                const void* data = nullptr, std::uint32_t dataSize = 0) {
    std::uint8_t headerByte = 0;
    const u8 normal = pxiDevMakeTransferOption(BAUDRATE_4MHZ, BUSMODE_1BIT);
    const u64 noWait = pxiDevMakeWaitOperation(WAIT_NONE, DEASSERT_NONE, 0);
    PXIDEV_SPIBuffer header{infrared ? &headerByte : nullptr, infrared ? 1u : 0u,
                            pxiDevMakeTransferOption(BAUDRATE_1MHZ, BUSMODE_1BIT), noWait};
    PXIDEV_SPIBuffer out{const_cast<std::uint8_t*>(command.data()), static_cast<u32>(command.size()), normal, noWait};
    PXIDEV_SPIBuffer in{answer, answerSize, normal, noWait};
    PXIDEV_SPIBuffer payload{const_cast<void*>(data), dataSize, normal, noWait};
    PXIDEV_SPIBuffer none{nullptr, 0, normal, noWait};
    PXIDEV_SPIBuffer footer{nullptr, 0, normal, noWait};
    return PXIDEV_SPIMultiWriteRead(&header, &out, &in, &payload, &none, &footer);
}

bool isInfrared(Chip chip) {
    return chip == Chip::FlashInfrared;
}

std::uint32_t pageSize(Chip chip) {
    switch (chip) {
    case Chip::Eeprom512: return 16;
    case Chip::Eeprom8K: return 32;
    case Chip::Eeprom64K: return 128;
    default: return 256;
    }
}

std::vector<std::uint8_t> addressed(Chip chip, std::uint8_t command, std::uint32_t address, bool forWrite) {
    switch (chip) {
    case Chip::Eeprom512:
        return {address >= 0x100 ? (forWrite ? kWriteHigh : kReadHigh) : command, static_cast<std::uint8_t>(address & 0xFF)};
    case Chip::Eeprom8K:
    case Chip::Eeprom64K:
        return {command, static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address)};
    default:
        return {command, static_cast<std::uint8_t>(address >> 16), static_cast<std::uint8_t>(address >> 8),
                static_cast<std::uint8_t>(address)};
    }
}

std::uint32_t readId(bool infrared) {
    std::array<std::uint8_t, 3> id{};
    if (R_FAILED(transfer(infrared, {kReadId}, id.data(), 3))) return kNoChip;
    return (std::uint32_t{id[0]} << 16) | (std::uint32_t{id[1]} << 8) | id[2];
}

std::uint8_t readStatus(bool infrared) {
    std::uint8_t status = 0xFF;
    transfer(infrared, {kReadStatus}, &status, 1);
    return status;
}

bool waitWhileBusy(bool infrared) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (!(readStatus(infrared) & kBusy)) return true;
        svcSleepThread(1'000'000);
    }
    return false;
}

Result readRange(Chip chip, std::uint32_t address, std::uint8_t* out, std::uint32_t size) {
    if (chip == Chip::Eeprom512 && address < 0x100 && address + size > 0x100) {
        const std::uint32_t low = 0x100 - address;
        if (const Result rc = readRange(chip, address, out, low); R_FAILED(rc)) return rc;
        return readRange(chip, 0x100, out + low, size - low);
    }
    return transfer(isInfrared(chip), addressed(chip, kRead, address, false), out, size);
}

// EEPROMs mirror their contents past their size, so a small chip reads the same data at 0 and at its size.
std::optional<Chip> probeEeprom() {
    std::array<std::uint8_t, 64> first{}, mirrored{};
    for (const auto [chip, size] : {std::pair{Chip::Eeprom8K, 0x2000u}, std::pair{Chip::Eeprom64K, 0x10000u}}) {
        if (R_FAILED(readRange(chip, 0, first.data(), first.size())) ||
            R_FAILED(readRange(chip, size, mirrored.data(), mirrored.size())))
            return std::nullopt;
        if (first == mirrored) return chip;
    }
    return Chip::Eeprom128K;
}

std::optional<std::pair<Chip, std::uint32_t>> flashSize(std::uint32_t id) {
    switch (id) {
    case 0x204012:
    case 0x621600: return std::pair{Chip::Flash, 0x40000u};
    case 0x204013:
    case 0x621100:
    case 0x208013: return std::pair{Chip::Flash, 0x80000u};
    case 0x204014: return std::pair{Chip::Flash, 0x100000u};
    default: return std::nullopt;
    }
}

std::uint32_t eepromSize(Chip chip) {
    switch (chip) {
    case Chip::Eeprom512: return 0x200;
    case Chip::Eeprom8K: return 0x2000;
    case Chip::Eeprom64K: return 0x10000;
    default: return 0x20000;
    }
}

}

std::optional<DsCard::Info> DsCard::detect() {
    bool inserted = false;
    FS_CardType type = CARD_CTR;
    if (R_FAILED(FSUSER_CardSlotIsInserted(&inserted)) || !inserted) return std::nullopt;
    if (R_FAILED(FSUSER_GetCardType(&type)) || type != CARD_TWL) return std::nullopt;

    std::array<std::uint8_t, 0x3B4> header{};
    if (R_FAILED(FSUSER_GetLegacyRomHeader(MEDIATYPE_GAME_CARD, 0, header.data()))) return std::nullopt;
    Info info;
    info.title.assign(reinterpret_cast<const char*>(header.data()), strnlen(reinterpret_cast<const char*>(header.data()), 12));
    info.gameCode.assign(reinterpret_cast<const char*>(header.data() + 0x0C), 4);

    const PxiSession session;
    if (!session.ready()) return std::nullopt;
    const std::uint32_t id = readId(false);
    if (id == kNoChip || id == 0) {
        const std::uint8_t status = readStatus(false) & 0xFD;
        if (status == 0xF0) info.chip = Chip::Eeprom512;
        else if (status == 0x00) {
            auto chip = probeEeprom();
            if (!chip) return std::nullopt;
            info.chip = *chip;
        } else {
            return std::nullopt;
        }
        info.size = eepromSize(info.chip);
        return info;
    }
    if (auto flash = flashSize(id)) {
        info.chip = flash->first;
        info.size = flash->second;
        return info;
    }
    // Infrared cartridges (game codes starting with 'I') answer only after an extra header byte.
    if (info.gameCode.starts_with('I')) {
        const std::uint32_t infraredId = readId(true);
        info.chip = Chip::FlashInfrared;
        info.size = (infraredId == 0x204012 || infraredId == 0x621600) ? 0x40000u : 0x80000u;
        return info;
    }
    return std::nullopt;
}

std::expected<Bytes, std::string> DsCard::read(const Info& card) {
    const PxiSession session;
    if (!session.ready()) return std::unexpected("cannot reach the card slot (pxi:dev)");
    Bytes data(card.size);
    for (std::uint32_t address = 0; address < card.size; address += kReadChunk) {
        const std::uint32_t length = std::min(kReadChunk, card.size - address);
        if (const Result rc = readRange(card.chip, address, data.data() + address, length); R_FAILED(rc))
            return std::unexpected(std::format("cartridge read failed (0x{:08X})", static_cast<std::uint32_t>(rc)));
    }
    return data;
}

std::expected<void, std::string> DsCard::write(const Info& card, ByteView data) {
    if (data.size() != card.size) return std::unexpected("the save must keep the cartridge's size");
    auto current = read(card);
    if (!current) return std::unexpected(current.error());
    const PxiSession session;
    if (!session.ready()) return std::unexpected("cannot reach the card slot (pxi:dev)");
    const bool infrared = isInfrared(card.chip);
    const std::uint32_t page = pageSize(card.chip);
    const bool flash = card.chip == Chip::Flash || card.chip == Chip::FlashInfrared;
    for (std::uint32_t address = 0; address < card.size; address += page) {
        if (std::memcmp(current->data() + address, data.data() + address, page) == 0) continue;
        if (R_FAILED(transfer(infrared, {kWriteEnable}, nullptr, 0)))
            return std::unexpected(std::format("write enable failed at 0x{:X}", address));
        const auto command = addressed(card.chip, flash ? kFlashPageWrite : kWrite, address, true);
        if (const Result rc = transfer(infrared, command, nullptr, 0, data.data() + address, page); R_FAILED(rc))
            return std::unexpected(std::format("cartridge write failed at 0x{:X} (0x{:08X})", address, static_cast<std::uint32_t>(rc)));
        if (card.chip == Chip::Eeprom512) svcSleepThread(10'000'000);
        else if (!waitWhileBusy(infrared)) return std::unexpected(std::format("cartridge stayed busy at 0x{:X}", address));
    }
    return {};
}

}
