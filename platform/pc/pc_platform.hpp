#pragma once

#include <filesystem>

#include "savetinker/platform.hpp"

namespace st {

// Emulates a console from a folder:
//   <root>/titles.json   [{"title_id", "name", "publisher", "version"}]
//   <root>/users.json    [{"id", "nickname"}]
//   <root>/saves/<TITLE_ID>/<account-USERID | device | bcat | cache>/...
//   <root>/running.txt   optional, title id of the "running" game
class PcPlatform : public Platform {
public:
    explicit PcPlatform(std::filesystem::path root, std::string gamePlatform = "switch");

    std::string gamePlatform() const override { return gamePlatform_; }
    std::filesystem::path dataRoot() const override { return root_ / "SaveTinker"; }

    std::vector<UserAccount> users() override;
    std::vector<TitleInfo> titles() override;
    std::vector<SaveInfo> saves() override;
    std::optional<std::string> runningTitleId() override;
    std::expected<std::unique_ptr<SaveMount>, std::string> mount(const SaveInfo& save) override;

private:
    std::filesystem::path slotPath(const SaveInfo& save) const;

    std::filesystem::path root_;
    std::string gamePlatform_;
};

}
