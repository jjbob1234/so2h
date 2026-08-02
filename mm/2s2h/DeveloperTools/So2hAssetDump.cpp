/*
 * File: So2hAssetDump.cpp
 * Description: SO2H [Menu] developer asset dumper.
 *
 * Why this exists: the merged quest bar draws OOT's pause quest-status hexagon from six of
 * the fifteen 80x32 gPauseQuestStatus<row><col> tiles, chosen from soh's icon_item_static.xml
 * naming (rows 0-2, cols 0-4). On a real merged oot.o2r the tiles that come back are not the
 * hexagon - they are the treble clef, staff lines and the bottom-right song box - so either
 * the merge names them differently or the row/col convention is transposed.
 *
 * That cannot be resolved by reading source. This command dumps all fifteen tiles, as
 * individual BMPs plus one stitched 5-wide x 3-tall contact sheet, so the actual bytes can be
 * looked at and the six correct tiles identified once and for all.
 *
 * Usage, from the in-game console:   so2h_dump_quest_tiles
 * Output:                            <cwd>/so2h_dump/
 */

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/Window.h>
#include <ship/window/gui/ConsoleWindow.h>
#include <fast/resource/type/Texture.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "2s2h/OotAssets.h"

namespace {

constexpr int kTileW = 80;
constexpr int kTileH = 32;
constexpr int kSheetCols = 5;
constexpr int kSheetRows = 3;

// Transparent pixels are flattened onto magenta rather than black, so "this tile is empty"
// and "this tile is black line-art on transparent" can be told apart at a glance.
constexpr uint8_t kBackdrop[3] = { 255, 0, 255 };

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

/**
 * A simple RGB image with an origin-at-top-left layout.
 */
struct So2hDumpImage {
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;

    So2hDumpImage() = default;
    So2hDumpImage(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * h) {
        for (auto& px : pixels) {
            px = { kBackdrop[0], kBackdrop[1], kBackdrop[2] };
        }
    }

    void Set(int x, int y, Rgb value) {
        if ((x < 0) || (y < 0) || (x >= width) || (y >= height)) {
            return;
        }
        pixels[(static_cast<size_t>(y) * width) + x] = value;
    }

    Rgb Get(int x, int y) const {
        if ((x < 0) || (y < 0) || (x >= width) || (y >= height)) {
            return { kBackdrop[0], kBackdrop[1], kBackdrop[2] };
        }
        return pixels[(static_cast<size_t>(y) * width) + x];
    }
};

Rgb Blend(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const int inv = 255 - a;

    return { static_cast<uint8_t>(((r * a) + (kBackdrop[0] * inv)) / 255),
             static_cast<uint8_t>(((g * a) + (kBackdrop[1] * inv)) / 255),
             static_cast<uint8_t>(((b * a) + (kBackdrop[2] * inv)) / 255) };
}

/**
 * Decodes whatever the resource manager says the texture is. Deliberately format-driven
 * rather than assuming IA8: half the point of this dump is to find out whether the merged
 * tiles are the format soh's XML claims they are.
 */
bool DecodeTexture(const std::shared_ptr<Fast::Texture>& tex, So2hDumpImage& out, std::string& formatName) {
    if ((tex == nullptr) || (tex->ImageData == nullptr) || (tex->Width == 0) || (tex->Height == 0)) {
        return false;
    }

    const int w = tex->Width;
    const int h = tex->Height;
    const uint8_t* data = tex->ImageData;
    const size_t size = tex->ImageDataSize;

    out = So2hDumpImage(w, h);

    auto need = [&](size_t bytes) { return size >= bytes; };

    switch (tex->Type) {
        case Fast::TextureType::GrayscaleAlpha8bpp: { // IA8: 4 bits intensity, 4 bits alpha
            formatName = "IA8";
            if (!need(static_cast<size_t>(w) * h)) {
                return false;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const uint8_t v = data[(y * w) + x];
                    const uint8_t i = static_cast<uint8_t>(((v >> 4) & 0xF) * 17);
                    const uint8_t a = static_cast<uint8_t>((v & 0xF) * 17);

                    out.Set(x, y, Blend(i, i, i, a));
                }
            }
            return true;
        }
        case Fast::TextureType::GrayscaleAlpha16bpp: { // IA16
            formatName = "IA16";
            if (!need(static_cast<size_t>(w) * h * 2)) {
                return false;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const size_t o = (static_cast<size_t>(y) * w + x) * 2;

                    out.Set(x, y, Blend(data[o], data[o], data[o], data[o + 1]));
                }
            }
            return true;
        }
        case Fast::TextureType::Grayscale8bpp: { // I8
            formatName = "I8";
            if (!need(static_cast<size_t>(w) * h)) {
                return false;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const uint8_t v = data[(y * w) + x];

                    out.Set(x, y, Rgb{ v, v, v });
                }
            }
            return true;
        }
        case Fast::TextureType::RGBA16bpp: { // RGBA5551, big endian
            formatName = "RGBA16";
            if (!need(static_cast<size_t>(w) * h * 2)) {
                return false;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const size_t o = (static_cast<size_t>(y) * w + x) * 2;
                    const uint16_t v = static_cast<uint16_t>((data[o] << 8) | data[o + 1]);
                    const uint8_t r = static_cast<uint8_t>((((v >> 11) & 0x1F) * 255) / 31);
                    const uint8_t g = static_cast<uint8_t>((((v >> 6) & 0x1F) * 255) / 31);
                    const uint8_t b = static_cast<uint8_t>((((v >> 1) & 0x1F) * 255) / 31);

                    out.Set(x, y, Blend(r, g, b, (v & 1) ? 255 : 0));
                }
            }
            return true;
        }
        case Fast::TextureType::RGBA32bpp: {
            formatName = "RGBA32";
            if (!need(static_cast<size_t>(w) * h * 4)) {
                return false;
            }
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    const size_t o = (static_cast<size_t>(y) * w + x) * 4;

                    out.Set(x, y, Blend(data[o], data[o + 1], data[o + 2], data[o + 3]));
                }
            }
            return true;
        }
        default:
            formatName = "unsupported(" + std::to_string(static_cast<int>(tex->Type)) + ")";
            return false;
    }
}

/**
 * 24-bit bottom-up BMP. Chosen over PNG on purpose: no encoder dependency, and every OS can
 * open it. These are 80x32 tiles, the file size does not matter.
 */
bool WriteBmp(const std::filesystem::path& path, const So2hDumpImage& img) {
    if ((img.width <= 0) || (img.height <= 0)) {
        return false;
    }

    const int rowBytes = img.width * 3;
    const int padding = (4 - (rowBytes % 4)) % 4;
    const uint32_t pixelBytes = static_cast<uint32_t>((rowBytes + padding) * img.height);
    const uint32_t fileSize = 54 + pixelBytes;

    FILE* f = fopen(path.string().c_str(), "wb");
    if (f == nullptr) {
        return false;
    }

    uint8_t header[54] = {};
    auto put32 = [&](int off, uint32_t v) {
        header[off + 0] = static_cast<uint8_t>(v & 0xFF);
        header[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        header[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        header[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    auto put16 = [&](int off, uint16_t v) {
        header[off + 0] = static_cast<uint8_t>(v & 0xFF);
        header[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };

    header[0] = 'B';
    header[1] = 'M';
    put32(2, fileSize);
    put32(10, 54); // pixel data offset
    put32(14, 40); // DIB header size
    put32(18, static_cast<uint32_t>(img.width));
    put32(22, static_cast<uint32_t>(img.height));
    put16(26, 1);  // planes
    put16(28, 24); // bits per pixel
    put32(34, pixelBytes);
    put32(38, 2835); // 72 DPI
    put32(42, 2835);

    fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> row(static_cast<size_t>(rowBytes + padding), 0);
    for (int y = img.height - 1; y >= 0; y--) {
        for (int x = 0; x < img.width; x++) {
            const Rgb px = img.Get(x, y);

            row[(static_cast<size_t>(x) * 3) + 0] = px.b; // BMP is BGR
            row[(static_cast<size_t>(x) * 3) + 1] = px.g;
            row[(static_cast<size_t>(x) * 3) + 2] = px.r;
        }
        fwrite(row.data(), 1, row.size(), f);
    }

    fclose(f);
    return true;
}

void Blit(So2hDumpImage& dst, const So2hDumpImage& src, int dx, int dy) {
    for (int y = 0; y < src.height; y++) {
        for (int x = 0; x < src.width; x++) {
            dst.Set(dx + x, dy + y, src.Get(x, y));
        }
    }
}

} // namespace

/**
 * Dumps the fifteen OOT pause quest-status tiles. Returns a human-readable report.
 */
std::string So2hAssetDump_QuestStatusTiles() {
    if (!OotAssets::IsOotContentAvailable()) {
        return "No merged OOT content present (unmerged mm.o2r). Nothing to dump.";
    }

    std::error_code ec;
    const std::filesystem::path outDir = std::filesystem::path("so2h_dump");
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        return "Could not create output directory so2h_dump: " + ec.message();
    }

    auto resourceMgr = Ship::Context::GetRawInstance()->GetResourceManager();
    auto archiveManager = (resourceMgr != nullptr) ? resourceMgr->GetArchiveManager() : nullptr;

    So2hDumpImage sheet(kTileW * kSheetCols, kTileH * kSheetRows);
    std::string report;
    int found = 0;

    for (int row = 0; row < kSheetRows; row++) {
        for (int col = 0; col < kSheetCols; col++) {
            const std::string name =
                "textures/icon_item_static/gPauseQuestStatus" + std::to_string(row) + std::to_string(col) + "Tex";
            const std::string resolved = OotAssets::ResolveOotPath(name.c_str());
            // ResolveOotPath returns a TexturePtr-style "__OTR__..." string; both HasFile and
            // LoadResource want the raw archive entry name.
            const std::string entryName = (resolved.rfind("__OTR__", 0) == 0) ? resolved.substr(7) : resolved;

            if ((archiveManager == nullptr) || !archiveManager->HasFile(entryName)) {
                report += "  " + std::to_string(row) + std::to_string(col) + ": MISSING (" + entryName + ")\n";
                continue;
            }

            auto tex = std::static_pointer_cast<Fast::Texture>(resourceMgr->LoadResource(entryName.c_str()));
            So2hDumpImage img;
            std::string formatName;

            if (!DecodeTexture(tex, img, formatName)) {
                report += "  " + std::to_string(row) + std::to_string(col) + ": present but UNDECODABLE (" +
                          (formatName.empty() ? "null resource" : formatName) + ")\n";
                continue;
            }

            found++;
            report += "  " + std::to_string(row) + std::to_string(col) + ": " + std::to_string(img.width) + "x" +
                      std::to_string(img.height) + " " + formatName + "\n";

            WriteBmp(outDir / ("questStatus" + std::to_string(row) + std::to_string(col) + ".bmp"), img);
            Blit(sheet, img, col * kTileW, row * kTileH);
        }
    }

    WriteBmp(outDir / "questStatus_contact_sheet.bmp", sheet);

    const std::string summary = "Dumped " + std::to_string(found) + "/15 gPauseQuestStatus tiles to so2h_dump/ " +
                                "(individual BMPs + questStatus_contact_sheet.bmp, 5 cols x 3 rows, " +
                                "transparent shown as magenta).\n";
    SPDLOG_INFO("So2hAssetDump: {}{}", summary, report);

    return summary + report;
}

bool So2hAssetDump_QuestStatusTilesHandler(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                                           std::string* output) {
    const std::string result = So2hAssetDump_QuestStatusTiles();

    if (output != nullptr) {
        *output = result;
    }
    std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
        ->SendInfoMessage("%s", result.c_str());

    return 0;
}
