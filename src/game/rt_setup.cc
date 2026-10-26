#include "game/rt_setup.h"

#include <string.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL.h>

#include "game/config.h"
#include "game/gconfig.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
#endif

namespace fallout {

namespace fs = std::filesystem;

#define RT_SETUP_TITLE L"Fallout 1 Real-Time"

// GOG's product id for Fallout 1 (its registry key under GOG.com\Games).
#define RT_SETUP_GOG_ID L"1440148836"

// The entry of `dir` named `name`, whatever its case.
static bool rt_setup_entry(const fs::path& dir, const char* name, fs::path* found)
{
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, error)) {
        if (SDL_strcasecmp(entry.path().filename().string().c_str(), name) == 0) {
            *found = entry.path();
            return true;
        }
    }
    return false;
}

// Whether `dir` is a Fallout 1 install (MASTER.DAT and CRITTER.DAT).
static bool rt_setup_is_install(const fs::path& dir, fs::path* master, fs::path* critter)
{
    std::error_code error;
    if (dir.empty() || !fs::is_directory(dir, error)) {
        return false;
    }
    return rt_setup_entry(dir, "MASTER.DAT", master) && rt_setup_entry(dir, "CRITTER.DAT", critter);
}

#ifdef _WIN32

static fs::path rt_setup_registry_path(HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, NULL, buffer, &size) != ERROR_SUCCESS) {
        return fs::path();
    }
    return fs::path(buffer);
}

// Steam libraries listed in `libraryfolders.vdf` ("path" entries).
static void rt_setup_steam_libraries(const fs::path& steam, std::vector<fs::path>& libraries)
{
    libraries.push_back(steam);

    std::ifstream stream(steam / "steamapps" / "libraryfolders.vdf");
    std::string line;
    while (std::getline(stream, line)) {
        size_t key = line.find("\"path\"");
        if (key == std::string::npos) {
            continue;
        }
        size_t start = line.find('"', key + 6);
        size_t end = start != std::string::npos ? line.find('"', start + 1) : std::string::npos;
        if (end == std::string::npos) {
            continue;
        }
        std::string value = line.substr(start + 1, end - start - 1);
        std::string unescaped;
        for (size_t index = 0; index < value.size(); index++) {
            if (value[index] == '\\' && index + 1 < value.size() && value[index + 1] == '\\') {
                index++;
            }
            unescaped += value[index];
        }
        libraries.push_back(fs::path(unescaped));
    }
}

// Where Fallout 1 may be installed: GOG (registry), Steam (every library),
// then the usual folders.
static void rt_setup_candidates(std::vector<fs::path>& candidates)
{
    const wchar_t* gogKeys[] = {
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\" RT_SETUP_GOG_ID,
        L"SOFTWARE\\GOG.com\\Games\\" RT_SETUP_GOG_ID,
    };
    for (const wchar_t* key : gogKeys) {
        candidates.push_back(rt_setup_registry_path(HKEY_LOCAL_MACHINE, key, L"path"));
    }

    std::vector<fs::path> steams = {
        rt_setup_registry_path(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
        rt_setup_registry_path(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
        fs::path(L"C:\\Program Files (x86)\\Steam"),
    };
    for (const fs::path& steam : steams) {
        if (steam.empty()) {
            continue;
        }
        std::vector<fs::path> libraries;
        rt_setup_steam_libraries(steam, libraries);
        for (const fs::path& library : libraries) {
            candidates.push_back(library / "steamapps" / "common" / "Fallout");
        }
    }

    candidates.push_back(fs::path(L"C:\\Program Files (x86)\\GOG Galaxy\\Games\\Fallout"));
    candidates.push_back(fs::path(L"C:\\GOG Games\\Fallout"));
}

// Asks the player for the folder Fallout 1 is installed in.
static bool rt_setup_pick_folder(fs::path* folder)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    BROWSEINFOW info;
    memset(&info, 0, sizeof(info));
    info.lpszTitle = L"Choose the folder Fallout 1 is installed in (the one with MASTER.DAT and CRITTER.DAT).";
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    bool picked = false;
    LPITEMIDLIST list = SHBrowseForFolderW(&info);
    if (list != NULL) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(list, path)) {
            *folder = fs::path(path);
            picked = true;
        }
        CoTaskMemFree(list);
    }

    CoUninitialize();
    return picked;
}

// Copies the install's loose data files (maps, scripts, sound, text, art)
// into this game's `data` folder: never its saves, never over a file that is
// already there.
static void rt_setup_copy_data(const fs::path& install)
{
    fs::path source;
    if (!rt_setup_entry(install, "DATA", &source)) {
        return;
    }

    std::error_code error;
    fs::create_directories("data", error);
    for (const fs::directory_entry& entry : fs::directory_iterator(source, error)) {
        if (SDL_strcasecmp(entry.path().filename().string().c_str(), "SAVEGAME") == 0) {
            continue;
        }
        std::error_code copyError;
        fs::copy(entry.path(), fs::path("data") / entry.path().filename(),
            fs::copy_options::recursive | fs::copy_options::skip_existing, copyError);
    }
}

#endif

bool rt_setup_game_files()
{
    char* master = NULL;
    char* critter = NULL;
    config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, &master);
    config_get_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, &critter);

    std::error_code error;
    if (master != NULL && critter != NULL && *master != '\0' && *critter != '\0'
        && fs::exists(master, error) && fs::exists(critter, error)) {
        return true;
    }

#ifdef _WIN32
    fs::path install;
    fs::path masterPath;
    fs::path critterPath;

    std::vector<fs::path> candidates;
    rt_setup_candidates(candidates);
    for (const fs::path& candidate : candidates) {
        if (rt_setup_is_install(candidate, &masterPath, &critterPath)) {
            install = candidate;
            break;
        }
    }

    if (install.empty()) {
        int answer = MessageBoxW(NULL,
            L"Fallout 1 Real-Time needs your own copy of Fallout 1 (GOG or Steam), and it wasn't found automatically.\n\n"
            L"Choose the folder Fallout 1 is installed in (the one with MASTER.DAT and CRITTER.DAT).",
            RT_SETUP_TITLE, MB_OKCANCEL | MB_ICONINFORMATION);
        while (answer == IDOK) {
            fs::path picked;
            if (!rt_setup_pick_folder(&picked)) {
                break;
            }
            if (rt_setup_is_install(picked, &masterPath, &critterPath)) {
                install = picked;
                break;
            }
            answer = MessageBoxW(NULL,
                L"That folder has no MASTER.DAT and CRITTER.DAT. Choose the folder Fallout 1 is installed in.",
                RT_SETUP_TITLE, MB_OKCANCEL | MB_ICONWARNING);
        }
    }

    if (install.empty()) {
        MessageBoxW(NULL,
            L"Fallout 1 Real-Time can't start without Fallout 1. Install Fallout 1 (GOG or Steam) and start it again.",
            RT_SETUP_TITLE, MB_OK | MB_ICONERROR);
        return false;
    }

    // Read from the install where it is; its loose files are copied here.
    rt_setup_copy_data(install);
    config_set_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_DAT_KEY, masterPath.string().c_str());
    config_set_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_DAT_KEY, critterPath.string().c_str());
    config_set_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_MASTER_PATCHES_KEY, "data");
    config_set_string(&game_config, GAME_CONFIG_SYSTEM_KEY, GAME_CONFIG_CRITTER_PATCHES_KEY, "data");
    gconfig_save();
#endif

    return true;
}

} // namespace fallout
