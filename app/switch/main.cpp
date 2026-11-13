#include <algorithm>
#include <cstdio>
#include <map>
#include <switch.h>

#include "savetinker/backup.hpp"
#include "switch_platform.hpp"

namespace {

constexpr int kVisibleRows = 30;

struct SaveRow {
    st::SaveInfo save;
    std::string titleName;
    std::string userName;
};

std::vector<SaveRow> collectRows(st::SwitchPlatform& platform) {
    std::map<std::string, std::string> titleNames;
    for (const auto& title : platform.titles()) titleNames[title.titleId] = title.name;
    std::map<std::string, std::string> userNames;
    for (const auto& user : platform.users()) userNames[user.id] = user.nickname;

    std::vector<SaveRow> rows;
    for (const auto& save : platform.saves()) {
        SaveRow row{save, titleNames.contains(save.titleId) ? titleNames[save.titleId] : save.titleId, {}};
        if (save.kind == st::SaveKind::Account)
            row.userName = userNames.contains(save.userId) ? userNames[save.userId] : save.userId;
        else
            row.userName = std::string(st::saveKindName(save.kind));
        rows.push_back(std::move(row));
    }
    std::ranges::sort(rows, {}, &SaveRow::titleName);
    return rows;
}

void draw(const std::vector<SaveRow>& rows, std::size_t selected, const std::string& status, bool appletMode) {
    consoleClear();
    std::printf("SaveTinker %s\n", SAVETINKER_VERSION);
    if (appletMode) std::printf("\x1b[33mApplet mode: launch over a game (hold R) for full memory.\x1b[0m\n");
    std::printf("A: back up   +: exit\n\n");

    const std::size_t first = selected >= kVisibleRows ? selected - kVisibleRows + 1 : 0;
    for (std::size_t i = first; i < rows.size() && i < first + kVisibleRows; ++i) {
        const auto& row = rows[i];
        std::printf("%s %-40.40s %-16.16s %6llu KB\n", i == selected ? ">" : " ", row.titleName.c_str(),
                    row.userName.c_str(), static_cast<unsigned long long>(row.save.size / 1024));
    }
    if (rows.empty()) std::printf("No saves found.\n");
    std::printf("\n%s\n", status.c_str());
}

}

int main() {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    st::SwitchPlatform platform;
    st::BackupManager backups(platform.dataRoot() / "backups");
    auto rows = collectRows(platform);
    std::size_t selected = 0;
    std::string status;
    bool redraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if ((pressed & HidNpadButton_AnyDown) && selected + 1 < rows.size()) { ++selected; redraw = true; }
        if ((pressed & HidNpadButton_AnyUp) && selected > 0) { --selected; redraw = true; }
        if ((pressed & HidNpadButton_A) && !rows.empty()) {
            const auto& row = rows[selected];
            if (auto mount = platform.mount(row.save); !mount) {
                status = "Error: " + mount.error();
            } else if (auto record = backups.create(**mount, row.save, row.titleName, "manual"); !record) {
                status = "Error: " + record.error();
            } else {
                status = "Backed up " + std::to_string(record->files.size()) + " files to " + record->directory.string();
            }
            redraw = true;
        }
        if (redraw) {
            draw(rows, selected, status, platform.appletMode());
            consoleUpdate(nullptr);
            redraw = false;
        }
    }

    consoleExit(nullptr);
    return 0;
}
