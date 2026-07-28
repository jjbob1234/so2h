// Scoped-down OOT ROM extractor. Mirrors env/2s2h/mm/2s2h/Extractor/Extract.cpp (the MM extractor) and
// env/soh/soh/soh/Extractor/Extract.cpp (SoH's full OOT extractor) but only knows about N64 NTSC US 1.0
// and only asks ZAPD to process assets/xml/N64_NTSC_10/textures, which contains just the texture
// archives referenced by the pause menu (ovl_kaleido_scope): icon_item*, item_name_static,
// map_48x85_static, map_name_static, parameter_static. Produces oot.o2r.
#ifdef _WIN32
#include <Windows.h>
#include <winuser.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")
#endif
#include "OotExtract.h"
#include "portable-file-dialogs.h"
#include <ship/utils/binarytools/BitConverter.h>
#include "build.h"

#ifdef unix
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _MSC_VER
#define OOT_BSWAP32 _byteswap_ulong
#define OOT_BSWAP16 _byteswap_ushort
#elif __has_include(<byteswap.h>)
#include <byteswap.h>
#define OOT_BSWAP32 bswap_32
#define OOT_BSWAP16 bswap_16
#else
#define OOT_BSWAP16(value) ((((value)&0xff) << 8) | ((value) >> 8))

#define OOT_BSWAP32(value) \
    (((uint32_t)OOT_BSWAP16((uint16_t)((value)&0xffff)) << 16) | (uint32_t)OOT_BSWAP16((uint16_t)((value) >> 16)))
#endif

#if defined(_MSC_VER)
#define OOT_UNREACHABLE __assume(0)
#elif __llvm__
#define OOT_UNREACHABLE __builtin_assume(0)
#else
#define OOT_UNREACHABLE __builtin_unreachable();
#endif

#include <stdlib.h>

#include <SDL2/SDL_messagebox.h>

#include <array>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <random>
#include <string>

extern "C" uint32_t CRC32C(unsigned char* data, size_t dataSize);

// Header CRC32 (word at ROM offset 0x10) for N64 NTSC US 1.0. Only version we ship menu XML for.
static constexpr uint32_t OOT_NTSC_10 = 0xEC7011B7;

static const std::unordered_map<uint32_t, const char*> ootVerMap = {
    { OOT_NTSC_10, "NTSC N64 1.0 (US)" },
};

// Full-body CRC32C for N64 NTSC US 1.0.
static constexpr std::array<const uint32_t, 1> ootGoodCrcs = {
    0x460C938C, // N64 NTSC US 1.0
};

enum class OotButtonId : int {
    YES,
    NO,
    FIND,
};

void OotExtractor::ShowErrorBox(const char* title, const char* text) {
#ifdef _WIN32
    MessageBoxA(nullptr, text, title, MB_OK | MB_ICONERROR);
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, nullptr);
#endif
}

void OotExtractor::ShowSizeErrorBox() const {
    std::unique_ptr<char[]> boxBuffer = std::make_unique<char[]>(mCurrentRomPath.size() + 100);
    snprintf(boxBuffer.get(), mCurrentRomPath.size() + 100,
             "The rom file %s was not a valid size. Was %zu MB, expecting 32, 54, or 64MB.", mCurrentRomPath.c_str(),
             mCurRomSize / MB_BASE);
    ShowErrorBox("Invalid Rom Size", boxBuffer.get());
}

void OotExtractor::ShowCrcErrorBox() const {
    ShowErrorBox(
        "Rom CRC invalid",
        "Rom CRC did not match. Only Ocarina of Time NTSC N64 1.0 (US) is currently supported for OOT menu assets.");
}

void OotExtractor::ShowCompressedErrorBox() const {
    ShowErrorBox("File is Compressed", "The selected file appears to be compressed. Please extract before using.");
}

int OotExtractor::ShowRomPickBox(uint32_t verCrc) const {
    std::unique_ptr<char[]> boxBuffer = std::make_unique<char[]>(mCurrentRomPath.size() + 100);
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[3] = { { 0 } };
    int ret;

    buttons[0].buttonid = 0;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = 1;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[2].buttonid = 2;
    buttons[2].text = "Find ROM";
    boxData.numbuttons = 3;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = boxBuffer.get();
    boxData.title = "OOT Rom Detected";
    boxData.window = nullptr;

    boxData.buttons = buttons;
    snprintf(boxBuffer.get(), mCurrentRomPath.size() + 100,
             "Rom detected: %s, Header CRC32: %8X. It appears to be: %s. Use this rom?", mCurrentRomPath.c_str(),
             verCrc, ootVerMap.at(verCrc));

    SDL_ShowMessageBox(&boxData, &ret);
    return ret;
}

int OotExtractor::ShowYesNoBox(const char* title, const char* box) {
    int ret;
#ifdef _WIN32
    ret = MessageBoxA(nullptr, box, title, MB_YESNO | MB_ICONQUESTION);
#else
    SDL_MessageBoxData boxData = { 0 };
    SDL_MessageBoxButtonData buttons[2] = { { 0 } };

    buttons[0].buttonid = IDYES;
    buttons[0].text = "Yes";
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = IDNO;
    buttons[1].text = "No";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    boxData.numbuttons = 2;
    boxData.flags = SDL_MESSAGEBOX_INFORMATION;
    boxData.message = box;
    boxData.title = title;
    boxData.buttons = buttons;
    SDL_ShowMessageBox(&boxData, &ret);
#endif
    return ret;
}

void OotExtractor::SetRomInfo(const std::string& path) {
    mCurrentRomPath = path;
    mCurRomSize = GetCurRomSize();
}

void OotExtractor::FilterRoms(std::vector<std::string>& roms, RomSearchMode searchMode) {
    std::ifstream inFile;
    std::vector<std::string>::iterator it = roms.begin();

    while (it != roms.end()) {
        std::string rom = *it;
        SetRomInfo(rom);

        if (!ValidateRomSize()) {
            it++;
            continue;
        }

        inFile.open(rom, std::ios::in | std::ios::binary);
        inFile.read((char*)mRomData.get(), mCurRomSize);
        inFile.clear();
        inFile.close();

        BitConverter::RomToBigEndian(mRomData.get(), mCurRomSize);

        if (!ootVerMap.contains(GetRomVerCrc())) {
            it = roms.erase(it);
            continue;
        }

        it++;
    }
}

void OotExtractor::GetRoms(std::vector<std::string>& roms) {
#ifdef _WIN32
    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(".\\*", &ffd);

    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char* ext = PathFindExtensionA(ffd.cFileName);

            if ((strcmp(ext, ".z64") == 0) || (strcmp(ext, ".n64") == 0) || (strcmp(ext, ".v64") == 0))
                roms.push_back(ffd.cFileName);
        }
    } while (FindNextFileA(h, &ffd) != 0);
#elif unix
    DIR* d = opendir(mSearchPath.c_str());
    struct dirent* dir;

    if (d != NULL) {
        while ((dir = readdir(d)) != NULL) {
            struct stat path;

            auto fullPath = std::filesystem::path(mSearchPath) / dir->d_name;
            auto fullPathString = fullPath.string();
            const char* fullPathCStr = fullPathString.c_str();

            stat(fullPathCStr, &path);
            if (S_ISREG(path.st_mode)) {
                char* ext = strrchr(dir->d_name, '.');
                if (ext != NULL && (strcmp(ext, ".z64") == 0 || strcmp(ext, ".n64") == 0 || strcmp(ext, ".v64") == 0)) {
                    roms.push_back(fullPathCStr);
                }
            }
        }
    }
    closedir(d);
#else
    for (const auto& file : std::filesystem::directory_iterator(mSearchPath)) {
        if (file.is_directory())
            continue;
        if ((file.path().extension() == ".n64") || (file.path().extension() == ".z64") ||
            (file.path().extension() == ".v64")) {
            roms.push_back((file.path()));
        }
    }
#endif
}

bool OotExtractor::GetRomPathFromBox() {
#ifdef _WIN32
    OPENFILENAMEA box = { 0 };
    char nameBuffer[512];
    nameBuffer[0] = 0;

    box.lStructSize = sizeof(box);
    box.lpstrFile = nameBuffer;
    box.nMaxFile = sizeof(nameBuffer) / sizeof(nameBuffer[0]);
    box.lpstrTitle = "Open Ocarina of Time Rom";
    box.Flags =
        OFN_NOCHANGEDIR | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    box.lpstrFilter = "N64 Roms\0*.z64;*.v64;*.n64\0\0";
    if (!GetOpenFileNameA(&box)) {
        DWORD err = CommDlgExtendedError();
        if (err != 0) {
            const char* errStr = nullptr;
            switch (err) {
                case FNERR_BUFFERTOOSMALL:
                    errStr = "Path buffer too small. Move file closer to root of your drive";
                    break;
                case FNERR_INVALIDFILENAME:
                    errStr = "File name for rom provided is invalid.";
                    break;
                case FNERR_SUBCLASSFAILURE:
                    errStr = "Failed to open a filebox because there is not enough RAM to do so.";
                    break;
            }
            MessageBoxA(nullptr, "Box Error", errStr, MB_OK | MB_ICONERROR);
            return false;
        }
    }
    if (nameBuffer[0] == 0) {
        return false;
    }
    mCurrentRomPath = nameBuffer;
#else
    auto selection =
        pfd::open_file("Select an Ocarina of Time rom", mSearchPath, { "N64 Roms", "*.z64 *.n64 *.v64" }).result();

    if (selection.empty()) {
        return false;
    }

    mCurrentRomPath = selection[0];
#endif
    mCurRomSize = GetCurRomSize();
    return true;
}

uint32_t OotExtractor::GetRomVerCrc() const {
    return OOT_BSWAP32(((uint32_t*)mRomData.get())[4]);
}

size_t OotExtractor::GetCurRomSize() const {
    return std::filesystem::file_size(mCurrentRomPath);
}

bool OotExtractor::ValidateAndFixRom() {
    const uint32_t actualCrc = CRC32C(mRomData.get(), mCurRomSize);

    for (const uint32_t crc : ootGoodCrcs) {
        if (actualCrc == crc) {
            return true;
        }
    }
    return false;
}

bool OotExtractor::ValidateNotCompressed() const {
    if (mRomData[0] == 'P' && mRomData[1] == 'K' && mRomData[2] == 0x03 && mRomData[3] == 0x04) {
        return false;
    }
    if (mRomData[0] == 'R' && mRomData[1] == 'a' && mRomData[2] == 'r' && mRomData[3] == 0x21) {
        return false;
    }
    if (mRomData[0] == '7' && mRomData[1] == 'z' && mRomData[2] == 0xBC && mRomData[3] == 0xAF && mRomData[4] == 0x27 &&
        mRomData[5] == 0x1C) {
        return false;
    }

    return true;
}

bool OotExtractor::ValidateRomSize() const {
    if (mCurRomSize != MB32 && mCurRomSize != MB54 && mCurRomSize != MB64) {
        return false;
    }
    return true;
}

bool OotExtractor::ValidateRom(bool skipCrcTextBox) {
    if (!ValidateNotCompressed()) {
        ShowCompressedErrorBox();
        return false;
    }
    if (!ValidateRomSize()) {
        ShowSizeErrorBox();
        return false;
    }
    if (!ValidateAndFixRom()) {
        if (!skipCrcTextBox) {
            ShowCrcErrorBox();
        }
        return false;
    }
    return true;
}

bool OotExtractor::ManuallySearchForRom() {
    std::ifstream inFile;

    if (!GetRomPathFromBox()) {
        return false;
    }

    inFile.open(mCurrentRomPath, std::ios::in | std::ios::binary);

    if (!inFile.is_open()) {
        return false;
    }

    inFile.read((char*)mRomData.get(), mCurRomSize);
    inFile.close();
    BitConverter::RomToBigEndian(mRomData.get(), mCurRomSize);

    if (!ValidateRom()) {
        return false;
    }

    return true;
}

bool OotExtractor::ManuallySearchForRomMatchingType(RomSearchMode searchMode) {
    if (!ManuallySearchForRom()) {
        return false;
    }

    // We only support Vanilla for the scoped menu-asset extraction.
    char msgBuf[150];
    snprintf(msgBuf, 150,
             "The selected rom does not match the expected game type\nExpected: Ocarina of Time NTSC N64 1.0 "
             "(US).\n\nDo you want to search again?");

    while (!ootVerMap.contains(GetRomVerCrc())) {
        int ret = ShowYesNoBox("Wrong Game Type", msgBuf);
        switch (ret) {
            case IDYES:
                if (!ManuallySearchForRom()) {
                    return false;
                }
                continue;
            case IDNO:
                return false;
            default:
                OOT_UNREACHABLE;
                break;
        }
    }

    return true;
}

bool OotExtractor::RunFileStandalone(std::string rom) {
    if (std::filesystem::is_directory(rom)) {
        return false;
    }
    auto file = std::filesystem::path(rom);
    if ((file.extension() != ".n64") && (file.extension() != ".z64") && (file.extension() != ".v64")) {
        return false;
    }
    SetRomInfo(rom);

    if (!ValidateRomSize()) {
        return false;
    }
    std::ifstream inFile;

    inFile.open(rom, std::ios::in | std::ios::binary);
    inFile.read((char*)mRomData.get(), mCurRomSize);
    inFile.clear();
    inFile.close();
    BitConverter::RomToBigEndian(mRomData.get(), mCurRomSize);

    if (!ValidateRom(true)) {
        return false;
    }

    return true;
}

void OotExtractor::SetSearchPath(const std::string& path) {
    mSearchPath = path;
}

bool OotExtractor::Run(std::string searchPath, RomSearchMode searchMode) {
    std::vector<std::string> roms;
    std::ifstream inFile;

    SetSearchPath(searchPath);

    GetRoms(roms);
    FilterRoms(roms, searchMode);

    if (roms.empty()) {
        int ret = ShowYesNoBox("No OOT roms found", "No Ocarina of Time roms found. Look for one?");

        switch (ret) {
            case IDYES:
                if (!ManuallySearchForRomMatchingType(searchMode)) {
                    return false;
                }
                break;
            case IDNO:
                ShowErrorBox("No rom selected", "No rom selected. Skipping OOT extraction.");
                return false;
            default:
                OOT_UNREACHABLE;
                break;
        }
    }

    for (const auto& rom : roms) {
        SetRomInfo(rom);

        if (!ValidateRomSize()) {
            ShowSizeErrorBox();
            continue;
        }

        inFile.open(rom, std::ios::in | std::ios::binary);
        inFile.read((char*)mRomData.get(), mCurRomSize);
        inFile.clear();
        inFile.close();
        BitConverter::RomToBigEndian(mRomData.get(), mCurRomSize);

        int option = ShowRomPickBox(GetRomVerCrc());

        if (option == (int)OotButtonId::YES) {
            if (!ValidateRom(true)) {
                continue;
            }
            break;
        } else if (option == (int)OotButtonId::FIND) {
            if (!ManuallySearchForRomMatchingType(searchMode)) {
                return false;
            }
            break;
        } else if (option == (int)OotButtonId::NO) {
            continue;
        }
        break;
    }
    return true;
}

bool OotExtractor::IsMasterQuest() const {
    // Menu-only extraction only ships a vanilla XML set.
    return false;
}

const char* OotExtractor::GetZapdVerStr() const {
    switch (GetRomVerCrc()) {
        case OOT_NTSC_10:
            return "N64_NTSC_10";
        default:
            OOT_UNREACHABLE;
            break;
    }
}

std::string OotExtractor::Mkdtemp() {
    std::string temp_dir = std::filesystem::temp_directory_path().string();

    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 1);

    char randchr[7];
    for (int i = 0; i < 6; i++) {
        randchr[i] = charset[dist(gen)];
    }
    randchr[6] = '\0';

    std::string tmppath = temp_dir + "/oot-extractor-" + randchr;
    std::filesystem::create_directory(tmppath);
    return tmppath;
}

extern "C" int zapd_report(int argc, char** argv, std::atomic<size_t>* extractCount, std::atomic<size_t>* totalExtract);

bool OotExtractor::CallZapd(std::string installPath, std::string exportdir, std::atomic<size_t>* extractCount,
                            std::atomic<size_t>* totalExtract) {
    constexpr int argc = 22;
    char xmlPath[1024];
    char confPath[1024];
    char portVersion[18];
    std::array<const char*, argc> argv;
    const char* version = GetZapdVerStr();
    // Menu-only scope: point ZAPD at just the textures subfolder (icon/map/parameter archives) instead
    // of the whole assets/xml/<version> tree, so oot.o2r only contains pause-menu assets.
    const char* otrFile = "oot.o2r";

    std::string romPath = std::filesystem::absolute(mCurrentRomPath).string();
    installPath = std::filesystem::absolute(installPath).string();
    exportdir = std::filesystem::absolute(exportdir).string();
    std::string tempdir = Mkdtemp();
    std::string curdir = std::filesystem::current_path().string();
#ifdef _WIN32
    std::filesystem::copy(installPath + "/assets", tempdir + "/assets",
                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::update_existing);
#else
    std::filesystem::create_symlink(installPath + "/assets", tempdir + "/assets");
#endif

    std::filesystem::current_path(tempdir);

    snprintf(xmlPath, 1024, "assets/xml/%s/textures", version);
    snprintf(confPath, 1024, "assets/Config_%s.xml", version);
    snprintf(portVersion, 18, "%d.%d.%d", gBuildVersionMajor, gBuildVersionMinor, gBuildVersionPatch);

    argv[0] = "ZAPD";
    argv[1] = "ed";
    argv[2] = "-i";
    argv[3] = xmlPath;
    argv[4] = "-b";
    argv[5] = romPath.c_str();
    argv[6] = "-fl";
    argv[7] = "assets/filelists";
    argv[8] = "-gsf";
    argv[9] = "0";
    argv[10] = "-rconf";
    argv[11] = confPath;
    argv[12] = "-se";
    argv[13] = "OTR";
    argv[14] = "--otrfile";
    argv[15] = otrFile;
    argv[16] = "--portVer";
    argv[17] = portVersion;
    argv[18] = "-o";
    argv[19] = "placeholder";
    argv[20] = "-osf";
    argv[21] = "placeholder";

    zapd_report(argc, (char**)argv.data(), extractCount, totalExtract);

    std::filesystem::copy(otrFile, exportdir + "/" + otrFile, std::filesystem::copy_options::overwrite_existing);

    std::filesystem::current_path(curdir);
    std::filesystem::remove_all(tempdir);

    return false;
}
