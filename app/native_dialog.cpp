#include "native_dialog.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#else
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#endif

#include "savetinker/file_saves.hpp"

namespace app {

namespace fs = std::filesystem;

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

}

std::optional<std::optional<fs::path>> nativePickPath(const std::string& title, bool folder, const fs::path& start) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool ownCom = SUCCEEDED(init);
    std::optional<std::optional<fs::path>> result;
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog)))) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
        dialog->SetTitle(widen(title).c_str());
        IShellItem* startItem = nullptr;
        if (!start.empty() &&
            SUCCEEDED(SHCreateItemFromParsingName(start.wstring().c_str(), nullptr, IID_IShellItem, reinterpret_cast<void**>(&startItem)))) {
            dialog->SetFolder(startItem);
            startItem->Release();
        }
        const HRESULT shown = dialog->Show(GetActiveWindow());
        result = std::optional<fs::path>{};
        IShellItem* item = nullptr;
        if (SUCCEEDED(shown) && SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                *result = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
        dialog->Release();
    }
    if (ownCom) CoUninitialize();
    return result;
}

#else

namespace {

std::string quote(const std::string& text) {
    std::string quoted = "'";
    for (const char c : text) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

bool haveCommand(const char* name) {
    return std::system((std::string("command -v ") + name + " >/dev/null 2>&1").c_str()) == 0;
}

}

// Linux desktops have no common API without a toolkit, so the dialog of the desktop's own tools is used.
std::optional<std::optional<fs::path>> nativePickPath(const std::string& title, bool folder, const fs::path& start) {
    if (std::getenv("DISPLAY") == nullptr && std::getenv("WAYLAND_DISPLAY") == nullptr) return std::nullopt;
    const std::string begin = start.empty() ? "" : st::pathToUtf8(start) + "/";
    std::string command;
    if (haveCommand("zenity"))
        command = "zenity --file-selection" + std::string(folder ? " --directory" : "") + " --title=" + quote(title) +
                  (begin.empty() ? "" : " --filename=" + quote(begin));
    else if (haveCommand("kdialog"))
        command = std::string("kdialog ") + (folder ? "--getexistingdirectory " : "--getopenfilename ") +
                  quote(begin.empty() ? "." : begin) + " --title " + quote(title);
    else
        return std::nullopt;
    FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
    if (!pipe) return std::nullopt;
    std::string output;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) output += buffer;
    const int status = pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if (status != 0 || output.empty()) return std::optional<fs::path>{};
    return std::optional<fs::path>{st::pathFromUtf8(output)};
}

#endif

}
