#ifndef OOT_EXTRACT_H
#define OOT_EXTRACT_H

#include <atomic>
#include <stdint.h>
#include <string>
#include <memory>
#include <vector>

// Values come from windows.h
#ifndef IDYES
#define IDYES 6
#endif
#ifndef IDNO
#define IDNO 7
#endif

// NOTE: MB_BASE/MB32/MB54/MB64 and RomSearchMode are already declared by Extract.h (the MM extractor)
// which lives in the same translation unit tree. Guard against duplicate definition when both are
// included by giving this a distinct include guard but reusing the shared constants/enum below only
// if they haven't been declared yet.
#ifndef EXTRACT_H
static constexpr size_t MB_BASE = 1024 * 1024;
static constexpr size_t MB32 = 32 * MB_BASE;
static constexpr size_t MB54 = 54 * MB_BASE;
static constexpr size_t MB64 = 64 * MB_BASE;

enum class RomSearchMode {
    Both = 0,
    Vanilla = 1,
    MQ = 2,
};
#endif

// Scoped-down OOT ROM extractor used to generate oot.o2r containing ONLY the assets needed to render
// the pause menu (item icons, dungeon/quest icons, map textures, name/parameter textures). This is
// intentionally not a full OOT asset extraction. Currently only supports N64 NTSC US 1.0, matching the
// only shipped assets/xml/N64_NTSC_10 + Config_N64_NTSC_10.xml asset set.
class OotExtractor {
    std::unique_ptr<unsigned char[]> mRomData = std::make_unique<unsigned char[]>(MB64);
    std::string mCurrentRomPath;
    std::string mSearchPath;
    size_t mCurRomSize = 0;

    bool GetRomPathFromBox();

    uint32_t GetRomVerCrc() const;
    size_t GetCurRomSize() const;
    bool ValidateAndFixRom();
    bool ValidateRomSize() const;

    bool ValidateRom(bool skipCrcBox = false);
    bool ValidateNotCompressed() const;
    const char* GetZapdVerStr() const;

    void FilterRoms(std::vector<std::string>& roms, RomSearchMode searchMode);
    void ShowSizeErrorBox() const;
    void ShowCrcErrorBox() const;
    void ShowCompressedErrorBox() const;
    int ShowRomPickBox(uint32_t verCrc) const;
    bool ManuallySearchForRom();

  public:
    static int ShowYesNoBox(const char* title, const char* text);
    static void ShowErrorBox(const char* title, const char* text);
    bool IsMasterQuest() const;
    bool ManuallySearchForRomMatchingType(RomSearchMode searchMode);

    void SetRomInfo(const std::string& path);
    void SetSearchPath(const std::string& path);
    void GetRoms(std::vector<std::string>& roms);
    bool RunFileStandalone(std::string file);
    bool Run(std::string searchPath, RomSearchMode searchMode = RomSearchMode::Vanilla);
    bool CallZapd(std::string installPath, std::string exportdir, std::atomic<size_t>* extractCount,
                  std::atomic<size_t>* totalExtract);
    std::string Mkdtemp();
};
#endif
