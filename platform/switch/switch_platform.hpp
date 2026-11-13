#pragma once

#include <map>

#include "savetinker/platform.hpp"

namespace st {

class SwitchPlatform : public Platform {
public:
    SwitchPlatform();
    ~SwitchPlatform() override;

    std::string gamePlatform() const override { return "switch"; }
    std::filesystem::path dataRoot() const override { return "sdmc:/switch/SaveTinker"; }

    std::vector<UserAccount> users() override;
    std::vector<TitleInfo> titles() override;
    std::vector<SaveInfo> saves() override;
    std::optional<std::string> runningTitleId() override;
    std::expected<std::unique_ptr<SaveMount>, std::string> mount(const SaveInfo& save) override;

    // Launched from the album instead of over a game: less memory available.
    bool appletMode() const;

private:
    struct JournalSizes {
        std::uint64_t account = 0;
        std::uint64_t device = 0;
    };
    std::map<std::string, JournalSizes> journalSizes_;
    bool accountReady_ = false;
    bool nsReady_ = false;
    bool pmdmntReady_ = false;
    bool pminfoReady_ = false;
};

}
