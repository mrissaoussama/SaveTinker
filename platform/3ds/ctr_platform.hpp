#pragma once

#include <map>
#include <optional>

#include "ds_card.hpp"
#include "savetinker/platform.hpp"

namespace st {

class CtrPlatform : public Platform {
public:
    CtrPlatform();
    ~CtrPlatform() override;

    std::string gamePlatform() const override { return "3ds"; }
    std::filesystem::path dataRoot() const override { return "sdmc:/3ds/SaveTinker"; }

    std::vector<UserAccount> users() override;
    std::vector<TitleInfo> titles() override;
    std::vector<SaveInfo> saves() override;
    std::optional<std::string> runningTitleId() override { return std::nullopt; }
    std::expected<std::unique_ptr<SaveMount>, std::string> mount(const SaveInfo& save) override;
    std::expected<std::shared_ptr<GameData>, std::string> openGameData(const std::string& titleId) override;
    void afterRestore(const SaveInfo& save) override;

    static std::string dsTitleId(const std::string& gameCode);

private:
    struct InstalledTitle {
        std::uint64_t id = 0;
        std::uint8_t mediaType = 0;
        std::uint16_t version = 0;
        std::optional<std::uint16_t> updateVersion;
        std::vector<std::string> addOns;
        bool dsiWare = false;
    };

    std::vector<InstalledTitle> scan();
    const InstalledTitle* find(const std::string& titleId);

    std::map<std::string, InstalledTitle> installed_;
    std::optional<DsCard::Info> dsCard_;
    bool amReady_ = false;
    bool cfgReady_ = false;
};

}
