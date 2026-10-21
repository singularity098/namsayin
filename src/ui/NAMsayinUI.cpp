// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"

#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/blocks/CompressorBlock.hpp"
#include "engine/blocks/DelayBlock.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/NamBlock.hpp"
#include "state/ChainExchange.hpp"
#include "state/ChainState.hpp"
#include "state/Library.hpp"
#include "state/LibraryScanner.hpp"
#include "state/Paths.hpp"
#include "state/Preset.hpp"
#include "state/Settings.hpp"
#include "state/UndoStack.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace namsayin;

namespace {

// Theme tokens. Very dark: a pure black window with flat near-black grey surfaces on it.
namespace theme {
const Color background(0x00, 0x00, 0x00);
const Color bar(0x0c, 0x0c, 0x0e);
const Color panel(0x0c, 0x0c, 0x0e);
const Color tile(0x16, 0x16, 0x1a);
const Color tileHover(0x1d, 0x1d, 0x22);
const Color inset(0x05, 0x05, 0x06);
const Color overlay(0, 0, 0, 0.7f);
const Color dialog(0x10, 0x10, 0x13);
const Color border(255, 255, 255, 0.10f);
const Color text(0xe6, 0xe6, 0xea);
const Color textDim(0x78, 0x78, 0x82);
const Color good(0x4c, 0xc9, 0x7a);
const Color warn(0xf0, 0xc0, 0x40);
const Color bad(0xe5, 0x48, 0x4d);
// Highlight colour, chosen in Settings from these
const Color accents[] = {
    Color(0x9b, 0x5c, 0xff), // purple (default)
    Color(0x4c, 0x8d, 0xff), // blue
    Color(0x2f, 0xbf, 0xa8), // teal
    Color(0x4c, 0xc9, 0x7a), // green
    Color(0xf0, 0xa0, 0x30), // amber
    Color(0xe5, 0x48, 0x4d), // red
    Color(0xe0, 0x5c, 0xb0), // pink
};
constexpr int accentCount = sizeof(accents) / sizeof(accents[0]);
Color accent = accents[0];
// Placeholder block art, by kind of block
const Color amp(0x8a, 0x5a, 0x3a);
const Color cab(0x4a, 0x4a, 0x52);
const Color pedalDelay(0x2f, 0x9e, 0x91);
const Color pedalReverb(0x4c, 0x7c, 0xe0);
const Color pedalDynamics(0xd0, 0x82, 0x2e);
const Color pedalOther(0x70, 0x70, 0x7a);
} // namespace theme

constexpr float kTopBarHeight = 48.0f;
constexpr float kBottomBarHeight = 132.0f;
constexpr float kTileSize = 120.0f;
constexpr float kTileGap = 16.0f;
constexpr float kChainTop = kTopBarHeight + 24.0f;
constexpr float kKnobRadius = 22.0f;
// Block panel: one cell per control, right-justified next to the block image
constexpr float kCellWidth = 96.0f;
constexpr float kCellHeight = 116.0f;
constexpr float kPanelMargin = 16.0f;
constexpr uint kDoubleClickMs = 350;
// How long the audio engine gets to start a changed chain before the UI reports blocks that aren't running
constexpr auto kStatusGrace = std::chrono::milliseconds(2500);

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Hit {
    Knob,         // index = host parameter
    Toggle,       // index = host parameter (0/1 parameters)
    Tile,         // index = position in the chain
    TileBypass,
    TileRemove,
    AddBlock,
    A2Size,       // index = position in the chain
    Choice,       // index = host parameter; click steps to the next choice
    InClip,
    OutClip,
    DialogBackdrop,
    DialogPanel,
    DialogClose,
    DialogCategory, // index = Category
    DialogItem,     // index = position in the current category's item list
    LibraryItem,    // index = row in the visible library list
    LibraryStar,    // index = row in the visible library list
    LibraryFavorites,
    LibraryBrowse,
    TextField,      // index = Field
    OpenSettings,
    SettingsChoose, // index = Field (the folder field it fills)
    SettingsRescan,
    SettingsA2,     // index = 1 for Lite
    SettingsNormalize,
    SettingsParallel,
    SettingsAccent, // index = accent
    PresetMenu,
    Undo,
    Redo,
    MenuItem,       // index = position in fMenu
    Locate,         // index = position in the chain
    UseLibraryMatch, // index = position in the chain
};

// What a context or preset menu item does
enum class MenuAction {
    None, // separator / label
    MidiLearn,
    MidiClear,
    ResetParam,
    ToggleBypass,
    RemoveBlock,
    PresetNew,
    PresetOpen,
    PresetSave,
    PresetSaveAs,
    PresetEmbed,
    PresetRecent, // arg = index in the recent list
};

struct MenuEntry {
    std::string label;
    MenuAction action;
    int arg = 0;
};

// Editable text fields
enum Field {
    kFieldSearch,
    kFieldNamDir,
    kFieldIrDir,
    kFieldCount
};

enum class Dialog { None, AddBlock, Settings, Menu };

// What a file picked in the file browser is for
enum class BrowseTarget { AddBlock, NamDir, IrDir, OpenPreset, SavePreset, Locate };

struct HitArea {
    Rect rect;
    Hit kind;
    int index;
};

std::string fileStem(const std::string& path)
{
    return pathToUtf8(pathFromUtf8(path).stem());
}

std::string lowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Whether every space-separated word of `query` appears in `entry` (case-insensitive)
bool matchesSearch(const LibraryEntry& entry, const std::string& query)
{
    const std::string haystack = lowerCase(entry.name + " " + entry.folder + " " + entry.info);
    size_t pos = 0;
    const std::string q = lowerCase(query);
    while (pos < q.size())
    {
        const size_t end = q.find(' ', pos);
        const std::string word = q.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!word.empty() && haystack.find(word) == std::string::npos)
            return false;
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }
    return true;
}

// Removes the last UTF-8 character
void popUtf8(std::string& text)
{
    while (!text.empty())
    {
        const auto c = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((c & 0xc0) != 0x80)
            break;
    }
}

int64_t fileTime(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    return ec ? 0 : static_cast<int64_t>(time.time_since_epoch().count());
}

// The block type for a file the user picked, by extension: .nam -> NAM model, .wav -> IR
const BlockDescriptor* blockTypeForFile(const char* filename)
{
    const size_t len = std::strlen(filename);
    if (len < 4)
        return nullptr;
    std::string ext(filename + len - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".nam")
        return &kNamBlockDescriptor;
    if (ext == ".wav")
        return &kIrBlockDescriptor;
    return nullptr;
}

// One built-in block type in the Add Block dialog. (Local NAM and Local IR show the library instead.)
struct AddItem {
    std::string label;
    const BlockDescriptor* type;
};

bool isLibraryCategory(Category category)
{
    return category == Category::LocalNam || category == Category::LocalIr;
}

std::vector<AddItem> addItems(Category category)
{
    std::vector<AddItem> items;
    if (isLibraryCategory(category))
        return items;
    for (const BlockDescriptor* type : BlockRegistry::builtin().types())
    {
        if (type->category == category)
            items.push_back({ type->displayName, type });
    }
    return items;
}

} // namespace

class NAMsayinUI : public UI
{
public:
    NAMsayinUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();

        for (uint32_t i = 0; i < kParamCount; ++i)
        {
            if (!isSlotParam(i))
                fParams[i] = globalParamSpec(i).def;
        }

        fSettings = Settings::load();
        fSettingsTime = fileTime(Settings::defaultPath());
        applySettings();
        rescan();

        const double scaleFactor = getScaleFactor();
        setGeometryConstraints(NAMSAYIN_UI_MIN_WIDTH * scaleFactor, NAMSAYIN_UI_MIN_HEIGHT * scaleFactor, false);
        if (d_isNotEqual(scaleFactor, 1.0))
            setSize(DISTRHO_UI_DEFAULT_WIDTH * scaleFactor, DISTRHO_UI_DEFAULT_HEIGHT * scaleFactor);
    }

protected:
    // ---------------------------------------------------------------------------------------------------------------
    // DSP -> UI

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index >= kParamCount)
            return;
        fParams[index] = value;
        // automation and MIDI change values in the host; presets and undo need them too
        if (isSlotParam(index))
            syncChainValue(index, value);
        if (index == kParamInstanceId && value >= 1.0f)
        {
            fInstanceId = static_cast<uint32_t>(value);
            // The host may not have sent us the chain (Reaper's LV2 doesn't): read the DSP's copy
            if (!fGotChainState)
                loadExchangeChain();
        }
        if (index == kParamMidiCcCount && fLearnParam >= 0 && value != fLearnCcCount)
            finishMidiLearn();
        else if (index == kParamMidiCcCount && fLearnParam < 0)
            forwardMidiCc();
        if (index == kParamInClip && value >= 0.5f)
            fInClipLatched = true;
        if (index == kParamOutClip && value >= 0.5f)
            fOutClipLatched = true;
        repaint();
    }

    void stateChanged(const char* const key, const char* const value) override
    {
        if (std::strcmp(key, "chain") != 0)
            return;
        if (std::optional<ChainState> chain = ChainState::fromJson(value))
        {
            fGotChainState = true;
            fChain = std::move(*chain);
            fSavedChain = fChain; // the host's state: nothing to mark as modified, nothing to undo
            fUndo.clear();
            chainChanged();
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void uiFileBrowserSelected(const char* const filename) override
    {
        if (filename == nullptr)
            return;
        switch (fBrowseTarget)
        {
        case BrowseTarget::OpenPreset:
            openPreset(filename);
            return;
        case BrowseTarget::SavePreset: {
            std::string path = filename;
            if (!lowerCase(path).ends_with(Preset::kExtension))
                path += Preset::kExtension;
            savePreset(path);
            return;
        }
        case BrowseTarget::Locate:
            locateFile(fLocateIndex, filename);
            return;
        default:
            break;
        }
        if (fBrowseTarget == BrowseTarget::NamDir || fBrowseTarget == BrowseTarget::IrDir)
        {
            // DPF's browser picks files, so the folder is the one the picked file is in
            const Field field = fBrowseTarget == BrowseTarget::NamDir ? kFieldNamDir : kFieldIrDir;
            fFields[field] = pathToUtf8(pathFromUtf8(filename).parent_path());
            commitField(field);
            return;
        }
        const BlockDescriptor* type = blockTypeForFile(filename);
        if (type == nullptr)
            return;
        nlohmann::json config = { { "path", filename } };
        if (const std::optional<std::string> hash = hashLibraryFile(filename))
            config["hash"] = *hash;
        addBlock(type, std::move(config));
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Drawing

    void onNanoDisplay() override
    {
        const float scale = static_cast<float>(getScaleFactor());
        fWidth = getWidth() / scale;
        fHeight = getHeight() / scale;
        fHits.clear();

        save();
        this->scale(scale, scale);
        fontSize(14.0f);

        fillRect(0, 0, fWidth, fHeight, theme::background);
        drawTopBar();
        drawChain();
        drawSelectedPanel();
        drawBottomBar();
        drawToast();
        if (fDialog == Dialog::AddBlock)
            drawAddDialog();
        else if (fDialog == Dialog::Settings)
            drawSettingsDialog();
        else if (fDialog == Dialog::Menu)
            drawMenu();

        restore();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Input

    bool onMouse(const MouseEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX() / getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());

        if (!ev.press)
        {
            if (ev.button == kMouseButtonLeft)
                return onRelease(x);
            return false;
        }
        if (ev.button == kMouseButtonRight)
            return onRightClick(x, y);
        if (ev.button != kMouseButtonLeft)
            return false;

        const bool doubleClick = ev.time - fLastClickTime < kDoubleClickMs;
        fLastClickTime = ev.time;

        const HitArea* hit = hitAt(x, y);

        // clicking anywhere but the focused field ends editing it
        if (fFocusedField >= 0 && !(hit && hit->kind == Hit::TextField && hit->index == fFocusedField))
            blurField(true);

        if (hit == nullptr)
            return false;

        switch (hit->kind)
        {
        case Hit::Knob:
            if (isSlot(hit->index))
                beginEdit(); // one undo step per gesture
            if (doubleClick)
            {
                setNormalized(hit->index, defaultNormalized(hit->index), true);
                return true;
            }
            fDragParam = hit->index;
            fDragStartY = y;
            fDragStartValue = normalized(hit->index);
            editParameter(hit->index, true);
            return true;

        case Hit::Toggle:
            if (isSlot(hit->index))
                beginEdit();
            setNormalized(hit->index, normalized(hit->index) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(hit->index);
            return true;

        case Hit::Tile:
            fSelected = hit->index;
            fPressTile = hit->index;
            fPressX = x;
            fDraggingTile = false;
            repaint();
            return true;

        case Hit::TileBypass: {
            beginEdit();
            const uint32_t index = slotBypassParam(fChain.blocks[hit->index].slot);
            setNormalized(index, normalized(index) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(index);
            return true;
        }

        case Hit::TileRemove:
            removeBlock(hit->index);
            return true;

        case Hit::AddBlock:
            openDialog(Dialog::AddBlock);
            return true;

        case Hit::Choice: {
            const ParamSpec* spec = slotSpec(hit->index);
            if (!spec)
                return true;
            const int steps = static_cast<int>(spec->max - spec->min);
            const int current = static_cast<int>(spec->toValue(normalized(hit->index)) - spec->min);
            const int next = (current + 1) % (steps + 1);
            beginEdit();
            setNormalized(hit->index, spec->toNormalized(spec->min + next), true);
            return true;
        }

        case Hit::DialogBackdrop:
        case Hit::DialogClose:
            closeDialog();
            return true;

        case Hit::DialogPanel:
            return true;

        case Hit::DialogCategory:
            fDialogCategory = static_cast<Category>(hit->index);
            fListScroll = 0.0f;
            repaint();
            return true;

        case Hit::DialogItem: {
            const std::vector<AddItem> items = addItems(fDialogCategory);
            if (hit->index >= static_cast<int>(items.size()))
                return true;
            const AddItem& item = items[hit->index];
            closeDialog();
            addBlock(item.type, nlohmann::json::object());
            return true;
        }

        case Hit::LibraryItem: {
            if (hit->index >= static_cast<int>(fVisibleEntries.size()))
                return true;
            const LibraryEntry entry = *fVisibleEntries[hit->index];
            const bool ir = fDialogCategory == Category::LocalIr;
            closeDialog();
            addBlock(ir ? &kIrBlockDescriptor : &kNamBlockDescriptor,
                     { { "path", entry.path }, { "hash", entry.hash } });
            return true;
        }

        case Hit::LibraryStar: {
            if (hit->index >= static_cast<int>(fVisibleEntries.size()))
                return true;
            const std::string hash = fVisibleEntries[hit->index]->hash;
            updateSettings([&hash](Settings& settings) {
                if (!settings.favorites.erase(hash))
                    settings.favorites.insert(hash);
            });
            return true;
        }

        case Hit::LibraryFavorites:
            fFavoritesOnly = !fFavoritesOnly;
            fListScroll = 0.0f;
            repaint();
            return true;

        case Hit::LibraryBrowse: {
            const bool ir = fDialogCategory == Category::LocalIr;
            closeDialog();
            browse(BrowseTarget::AddBlock, ir ? "Add a cab IR (.wav)" : "Add a NAM model (.nam)",
                   ir ? fSettings.irDir : fSettings.namDir);
            return true;
        }

        case Hit::TextField:
            if (fFocusedField != hit->index)
            {
                fFocusedField = hit->index;
                fFieldBeforeEdit = fFields[hit->index];
            }
            repaint();
            return true;

        case Hit::OpenSettings:
            openDialog(Dialog::Settings);
            return true;

        case Hit::SettingsChoose: {
            const bool nam = hit->index == kFieldNamDir;
            browse(nam ? BrowseTarget::NamDir : BrowseTarget::IrDir,
                   nam ? "Pick any file in your NAM folder" : "Pick any file in your IR folder",
                   nam ? fSettings.namDir : fSettings.irDir);
            return true;
        }

        case Hit::SettingsRescan:
            rescan();
            repaint();
            return true;

        case Hit::SettingsA2:
            updateSettings([lite = hit->index == 1](Settings& settings) { settings.a2Lite = lite; });
            return true;

        case Hit::SettingsNormalize:
            updateSettings([](Settings& settings) { settings.normalize = !settings.normalize; });
            return true;

        case Hit::SettingsParallel:
            updateSettings([](Settings& settings) { settings.parallel = !settings.parallel; });
            return true;

        case Hit::SettingsAccent:
            updateSettings([accent = hit->index](Settings& settings) { settings.accent = accent; });
            return true;

        case Hit::PresetMenu:
            openPresetMenu();
            return true;

        case Hit::Undo:
            undo();
            return true;

        case Hit::Redo:
            redo();
            return true;

        case Hit::MenuItem:
            runMenu(hit->index);
            return true;

        case Hit::Locate:
            fLocateIndex = hit->index;
            browse(BrowseTarget::Locate, "Locate the missing file",
                   fChain.blocks[hit->index].typeId == kIrBlockDescriptor.typeId ? fSettings.irDir : fSettings.namDir);
            return true;

        case Hit::UseLibraryMatch: {
            const LibraryEntry* match = libraryMatch(fChain.blocks[hit->index]);
            if (match)
                locateFile(hit->index, match->path);
            return true;
        }

        case Hit::A2Size: {
            beginEdit();
            nlohmann::json& config = fChain.blocks[hit->index].config;
            config["a2Size"] = config.value("a2Size", 1.0) >= 0.5 ? 0.0 : 1.0;
            sendChain();
            return true;
        }

        case Hit::InClip:
            fInClipLatched = false;
            repaint();
            return true;

        case Hit::OutClip:
            fOutClipLatched = false;
            repaint();
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX() / getScaleFactor());
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());
        fMouseX = x;
        fMouseY = y;

        if (fDragParam >= 0)
        {
            // 200 px for the full range; Shift for fine control
            const float range = (ev.mod & kModifierShift) ? 2000.0f : 200.0f;
            const float value = std::clamp(fDragStartValue + (fDragStartY - y) / range, 0.0f, 1.0f);
            setNormalized(static_cast<uint32_t>(fDragParam), value, false);
            fDragStartValue = value;
            fDragStartY = y;
            return true;
        }

        if (fPressTile >= 0 && std::fabs(x - fPressX) > 6.0f)
            fDraggingTile = true;

        repaint();
        return fDraggingTile;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!ev.press)
            return fFocusedField >= 0;

        if (fFocusedField >= 0)
        {
            std::string& text = fFields[fFocusedField];
            if (ev.key == kKeyBackspace)
                popUtf8(text);
            else if (ev.key == kKeyEnter)
                blurField(true);
            else if (ev.key == kKeyEscape)
                blurField(false);
            else if ((ev.mod & kModifierControl) && (ev.key == 'v' || ev.key == 'V'))
                pasteInto(text);
            else if ((ev.mod & kModifierControl) && (ev.key == 'u' || ev.key == 'U'))
                text.clear();
            else
                return true; // characters arrive through onCharacterInput
            if (fFocusedField == kFieldSearch)
                fListScroll = 0.0f;
            repaint();
            return true;
        }

        if (ev.key == kKeyEscape && fLearnParam >= 0)
        {
            cancelMidiLearn();
            return true;
        }
        if (ev.key == kKeyEscape && fDialog != Dialog::None)
        {
            closeDialog();
            return true;
        }
        if ((ev.mod & kModifierControl) && (ev.key == 'z' || ev.key == 'Z'))
        {
            if (ev.mod & kModifierShift)
                redo();
            else
                undo();
            return true;
        }
        if ((ev.mod & kModifierControl) && (ev.key == 'y' || ev.key == 'Y'))
        {
            redo();
            return true;
        }
        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (fFocusedField < 0)
            return false;
        // control characters (and Ctrl+key combinations) are handled in onKeyboard
        if (ev.character >= 32 && ev.character != 127)
        {
            fFields[fFocusedField] += ev.string;
            if (fFocusedField == kFieldSearch)
                fListScroll = 0.0f;
            repaint();
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (fDialog != Dialog::None)
        {
            fListScroll = std::max(0.0f, fListScroll - static_cast<float>(ev.delta.getY()) * 40.0f);
            repaint();
            return true;
        }
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());
        if (y < kChainTop || y > kChainTop + kTileSize)
            return false;
        const float delta = static_cast<float>(ev.delta.getY() != 0.0 ? ev.delta.getY() : ev.delta.getX());
        fScrollX = std::max(0.0f, fScrollX - delta * 40.0f);
        repaint();
        return true;
    }

    void uiIdle() override
    {
        if (!fToast.empty() && std::chrono::steady_clock::now() - fToastAt > std::chrono::seconds(5))
            repaint();
        if (std::optional<LibraryScanner::Result> result = fScanner.poll())
        {
            fNamLibrary = std::move(result->nam);
            fIrLibrary = std::move(result->ir);
            fScanned = true;
            repaint();
        }
        if (fScanning != fScanner.busy())
        {
            fScanning = fScanner.busy();
            repaint();
        }

        // Settings are shared by all instances: pick up changes made by another one
        const auto now = std::chrono::steady_clock::now();
        if (now - fSettingsCheckedAt > std::chrono::seconds(1))
        {
            fSettingsCheckedAt = now;
            const int64_t time = fileTime(Settings::defaultPath());
            if (time != fSettingsTime)
            {
                fSettingsTime = time;
                const Settings settings = Settings::load();
                const bool foldersChanged = settings.namDir != fSettings.namDir || settings.irDir != fSettings.irDir;
                fSettings = settings;
                applySettings();
                if (foldersChanged)
                    rescan();
                repaint();
            }
        }

        // redraw once the grace period ends, so blocks that didn't start get marked
        if (fStatusPending && statusSettled())
        {
            fStatusPending = false;
            repaint();
        }
    }

private:
    // ---------------------------------------------------------------------------------------------------------------
    // Parameters

    bool isSlot(uint32_t index) const { return isSlotParam(index); }

    const BlockState* blockInSlot(int slot) const
    {
        for (const BlockState& block : fChain.blocks)
        {
            if (block.slot == slot)
                return &block;
        }
        return nullptr;
    }

    const ParamSpec* slotSpec(uint32_t index) const
    {
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        if (param < 0)
            return nullptr;
        for (const BlockState& block : fChain.blocks)
        {
            if (block.slot == slot)
            {
                const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
                return type && param < type->numParams ? &type->params[param] : nullptr;
            }
        }
        return nullptr;
    }

    float normalized(uint32_t index) const
    {
        if (isSlot(index))
            return fParams[index];
        const GlobalParamSpec& spec = globalParamSpec(index);
        return (fParams[index] - spec.min) / (spec.max - spec.min);
    }

    float defaultNormalized(uint32_t index) const
    {
        if (isSlot(index))
        {
            const ParamSpec* spec = slotSpec(index);
            return spec ? spec->toNormalized(spec->def) : 0.0f;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        return (spec.def - spec.min) / (spec.max - spec.min);
    }

    void setNormalized(uint32_t index, float n, bool gesture)
    {
        float value = n;
        if (!isSlot(index))
        {
            const GlobalParamSpec& spec = globalParamSpec(index);
            value = spec.min + n * (spec.max - spec.min);
        }
        if (gesture)
            editParameter(index, true);
        setHostParameter(index, value);
        if (gesture)
            editParameter(index, false);

        if (isSlot(index))
            syncChainValue(index, n);
        repaint();
    }

    // Keeps the chain's copy of a slot parameter (for presets and undo) in step with the host's value
    void syncChainValue(uint32_t index, float value)
    {
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        for (BlockState& block : fChain.blocks)
        {
            if (block.slot != slot)
                continue;
            if (param < 0)
                block.bypass = value >= 0.5f;
            else
                block.params[param] = value;
        }
    }

    void setHostParameter(uint32_t index, float value)
    {
        fParams[index] = value;
        setParameterValue(index, value);
    }

    void syncBypassToChain(uint32_t index)
    {
        for (BlockState& block : fChain.blocks)
        {
            if (slotBypassParam(block.slot) == index)
                block.bypass = fParams[index] >= 0.5f;
        }
    }

    std::string valueText(uint32_t index) const
    {
        char buf[32];
        if (isSlot(index))
        {
            const ParamSpec* spec = slotSpec(index);
            if (!spec)
                return "";
            const float v = spec->toValue(fParams[index]);
            if (spec->scale == ParamScale::Toggle)
                return v >= 0.5f ? "On" : "Off";
            if (spec->scale == ParamScale::Choice && spec->choices)
                return spec->choices[static_cast<int>(v - spec->min)];
            const uint32_t offset = index - kSlotParamBase;
            const int slot = static_cast<int>(offset / kSlotParamCount);
            const int param = static_cast<int>(offset % kSlotParamCount) - 1;
            const BlockState* block = blockInSlot(slot);
            if (block && block->typeId == kDelayBlockDescriptor.typeId && param == kDelayParamTime
                && fParams[slotParam(slot, kDelayParamSync)] >= 0.5f)
                return delayNoteName(delayNoteIndex(fParams[index]));
            if (spec->unit[0] == ':')
            {
                std::snprintf(buf, sizeof(buf), "%.1f%s", v, spec->unit);
                return buf;
            }
            if (std::strcmp(spec->unit, "ms") == 0 && v > 0.0f && v < 10.0f)
            {
                std::snprintf(buf, sizeof(buf), "%.2f ms", v);
                return buf;
            }
            if (std::strcmp(spec->unit, "Hz") == 0 && v >= 1000.0f)
                std::snprintf(buf, sizeof(buf), "%.1f kHz", v / 1000.0f);
            else
                std::snprintf(buf, sizeof(buf), "%.1f %s", v, spec->unit);
            return buf;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        std::snprintf(buf, sizeof(buf), "%+.1f %s", fParams[index], spec.unit);
        return buf;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Undo/redo, presets, MIDI learn, menus

    // Call before changing fChain, so the change can be undone
    void beginEdit() { fUndo.push(fChain); }

    // Replaces the chain (undo, redo, presets): the host gets every block's values back
    void applyChain(ChainState chain)
    {
        fChain = std::move(chain);
        for (const BlockState& block : fChain.blocks)
        {
            setHostParameter(slotBypassParam(block.slot), block.bypass ? 1.0f : 0.0f);
            for (int p = 0; p < kParamsPerSlot; ++p)
                setHostParameter(slotParam(block.slot, p), block.params[p]);
        }
        if (fSelected >= static_cast<int>(fChain.blocks.size()))
            fSelected = static_cast<int>(fChain.blocks.size()) - 1;
        if (fSelected < 0 && !fChain.blocks.empty())
            fSelected = 0;
        sendChain();
    }

    void undo()
    {
        if (std::optional<ChainState> state = fUndo.undo(fChain))
            applyChain(std::move(*state));
    }

    void redo()
    {
        if (std::optional<ChainState> state = fUndo.redo(fChain))
            applyChain(std::move(*state));
    }

    void removeBlock(int index)
    {
        beginEdit();
        const int slot = fChain.blocks[index].slot;
        fChain.blocks.erase(fChain.blocks.begin() + index);
        // MIDI mappings follow the block, so they go with it
        fChain.clearMidiMappings(slotBypassParam(slot));
        for (int p = 0; p < kParamsPerSlot; ++p)
            fChain.clearMidiMappings(slotParam(slot, p));
        if (fSelected >= static_cast<int>(fChain.blocks.size()))
            fSelected = static_cast<int>(fChain.blocks.size()) - 1;
        sendChain();
    }

    void showToast(std::string message)
    {
        fToast = std::move(message);
        fToastAt = std::chrono::steady_clock::now();
        repaint();
    }

    std::string presetName() const
    {
        return fChain.preset.empty() ? "Untitled" : fileStem(fChain.preset);
    }

    bool presetModified() const { return fChain != fSavedChain; }

    Preset currentPreset() const
    {
        Preset preset;
        preset.chain = fChain;
        for (uint32_t index : presetGlobalParams())
            preset.globals[globalParamSpec(index).symbol] = fParams[index];
        return preset;
    }

    void savePreset(const std::string& path)
    {
        std::string error;
        if (!savePresetFile(path, currentPreset(), fSettings.embedFiles, &error))
        {
            showToast("Couldn't save the preset: " + error);
            return;
        }
        fChain.preset = path;
        fSavedChain = fChain;
        updateSettings([&path](Settings& settings) { settings.addRecentPreset(path); });
        sendChain();
        showToast("Saved " + fileStem(path));
    }

    void openPreset(const std::string& path)
    {
        std::string error;
        const std::optional<LoadedPreset> loaded = loadPresetFile(path, [this](const std::string& hash) {
            for (const auto* library : { &fNamLibrary, &fIrLibrary })
            {
                for (const LibraryEntry& entry : *library)
                {
                    if (entry.hash == hash)
                        return entry.path;
                }
            }
            return std::string();
        }, &error);
        if (!loaded || !loaded->preset.chain.validate(BlockRegistry::builtin(), &error))
        {
            showToast("Couldn't open the preset: " + error);
            return;
        }

        beginEdit();
        for (uint32_t index : presetGlobalParams())
        {
            const auto it = loaded->preset.globals.find(globalParamSpec(index).symbol);
            if (it != loaded->preset.globals.end())
                setHostParameter(index, it->second);
        }
        fSelected = loaded->preset.chain.blocks.empty() ? -1 : 0;
        applyChain(loaded->preset.chain);
        fSavedChain = fChain;
        updateSettings([&path](Settings& settings) { settings.addRecentPreset(path); });

        const auto missing = std::count(loaded->sources.begin(), loaded->sources.end(), FileSource::Missing);
        if (missing > 0)
            showToast(std::to_string(missing) + (missing == 1 ? " file is" : " files are")
                      + " missing. Select the grayed-out block to locate it.");
        else
            showToast("Opened " + presetName());
    }

    void newPreset()
    {
        beginEdit();
        for (uint32_t index : presetGlobalParams())
            setHostParameter(index, globalParamSpec(index).def);
        ChainState empty;
        applyChain(empty);
        fSavedChain = fChain;
        fSelected = -1;
    }

    // A library file with the same content as a block's (missing) file
    const LibraryEntry* libraryMatch(const BlockState& block) const
    {
        const std::string hash = block.config.value("hash", std::string());
        if (hash.empty())
            return nullptr;
        const std::vector<LibraryEntry>& library =
            block.typeId == kIrBlockDescriptor.typeId ? fIrLibrary : fNamLibrary;
        for (const LibraryEntry& entry : library)
        {
            if (entry.hash == hash)
                return &entry;
        }
        return nullptr;
    }

    void locateFile(int index, const std::string& path)
    {
        if (index < 0 || index >= static_cast<int>(fChain.blocks.size()))
            return;
        beginEdit();
        nlohmann::json& config = fChain.blocks[index].config;
        config["path"] = path;
        if (const std::optional<std::string> hash = hashLibraryFile(path))
            config["hash"] = *hash;
        fSidecars.erase(path);
        sendChain();
    }

    // MIDI learn: the next CC the DSP reports is mapped to the parameter
    void startMidiLearn(uint32_t param)
    {
        fLearnParam = static_cast<int>(param);
        fLearnCcCount = fParams[kParamMidiCcCount];
        setHostParameter(kParamMidiLearn, 1.0f);
        repaint();
    }

    void finishMidiLearn()
    {
        const int value = static_cast<int>(fParams[kParamMidiLastCc]);
        beginEdit();
        fChain.setMidiMapping({ value / 128, value % 128, static_cast<uint32_t>(fLearnParam) });
        cancelMidiLearn();
        sendChain();
    }

    // The DSP already applied a mapped CC, but not every format lets it tell the host (LV2 mostly doesn't), so the
    // UI sets the host's value too. That also keeps the knob on screen in step.
    void forwardMidiCc()
    {
        const int cc = static_cast<int>(fParams[kParamMidiLastCc]);
        for (const MidiMapping& mapping : fChain.midi)
        {
            if (mapping.channel * 128 + mapping.cc == cc)
            {
                const float value = midiCcToValue(mapping.param, static_cast<int>(fParams[kParamMidiLastValue]));
                setHostParameter(mapping.param, value);
                if (isSlot(mapping.param))
                    syncChainValue(mapping.param, value);
                repaint();
                return;
            }
        }
    }

    void cancelMidiLearn()
    {
        fLearnParam = -1;
        setHostParameter(kParamMidiLearn, 0.0f);
        repaint();
    }

    std::string paramName(uint32_t index) const
    {
        if (!isSlot(index))
            return globalParamSpec(index).name;
        const uint32_t offset = index - kSlotParamBase;
        const int slot = static_cast<int>(offset / kSlotParamCount);
        const int param = static_cast<int>(offset % kSlotParamCount) - 1;
        const BlockState* block = blockInSlot(slot);
        const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
        std::string name = type ? type->displayName : "Slot";
        if (block && !block->config.value("path", std::string()).empty())
            name = fileStem(block->config.value("path", std::string()));
        return name + " " + (param < 0 ? "Bypass" : (type && param < type->numParams ? type->params[param].name : "?"));
    }

    static std::string ccText(const MidiMapping& mapping)
    {
        return "CC " + std::to_string(mapping.cc) + " (ch " + std::to_string(mapping.channel + 1) + ")";
    }

    void openMenu(float x, float y, std::vector<MenuEntry> entries)
    {
        fMenu = std::move(entries);
        fMenuX = x;
        fMenuY = y;
        openDialog(Dialog::Menu);
    }

    bool onRightClick(float x, float y)
    {
        const HitArea* hit = hitAt(x, y);
        if (hit == nullptr || fDialog != Dialog::None)
            return false;

        if (hit->kind == Hit::Knob || hit->kind == Hit::Toggle || hit->kind == Hit::Choice)
        {
            const auto param = static_cast<uint32_t>(hit->index);
            if (!isMidiMappable(param))
                return false;
            fMenuParam = param;
            std::vector<MenuEntry> entries { { paramName(param), MenuAction::None } };
            entries.push_back({ "MIDI Learn", MenuAction::MidiLearn });
            if (const MidiMapping* mapping = fChain.midiMappingFor(param))
                entries.push_back({ "Clear MIDI " + ccText(*mapping), MenuAction::MidiClear });
            if (hit->kind == Hit::Knob)
                entries.push_back({ "Reset to default", MenuAction::ResetParam });
            openMenu(x, y, std::move(entries));
            return true;
        }
        if (hit->kind == Hit::Tile)
        {
            fMenuTile = hit->index;
            fSelected = hit->index;
            const bool bypassed = fParams[slotBypassParam(fChain.blocks[hit->index].slot)] >= 0.5f;
            fMenuParam = slotBypassParam(fChain.blocks[hit->index].slot);
            std::vector<MenuEntry> entries {
                { bypassed ? "Enable" : "Bypass", MenuAction::ToggleBypass },
                { "MIDI Learn bypass", MenuAction::MidiLearn },
            };
            if (const MidiMapping* mapping = fChain.midiMappingFor(fMenuParam))
                entries.push_back({ "Clear MIDI " + ccText(*mapping), MenuAction::MidiClear });
            entries.push_back({ "Remove", MenuAction::RemoveBlock });
            openMenu(x, y, std::move(entries));
            return true;
        }
        return false;
    }

    void openPresetMenu()
    {
        fMenuParam = kNoParam;
        std::vector<MenuEntry> entries {
            { "New", MenuAction::PresetNew },
            { "Open…", MenuAction::PresetOpen },
            { "Save", MenuAction::PresetSave },
            { "Save As…", MenuAction::PresetSaveAs },
            { std::string("Embed files when saving: ") + (fSettings.embedFiles ? "On" : "Off"), MenuAction::PresetEmbed },
        };
        if (!fSettings.recentPresets.empty())
        {
            entries.push_back({ "Recent", MenuAction::None });
            for (size_t i = 0; i < fSettings.recentPresets.size(); ++i)
                entries.push_back({ fileStem(fSettings.recentPresets[i]), MenuAction::PresetRecent, static_cast<int>(i) });
        }
        openMenu(140.0f, kTopBarHeight - 6.0f, std::move(entries));
    }

    void runMenu(int index)
    {
        if (index < 0 || index >= static_cast<int>(fMenu.size()))
            return;
        const MenuEntry entry = fMenu[index];
        closeDialog();
        switch (entry.action)
        {
        case MenuAction::None:
            break;
        case MenuAction::MidiLearn:
            startMidiLearn(fMenuParam);
            break;
        case MenuAction::MidiClear:
            beginEdit();
            fChain.clearMidiMappings(fMenuParam);
            sendChain();
            break;
        case MenuAction::ResetParam:
            if (isSlot(fMenuParam))
                beginEdit();
            setNormalized(fMenuParam, defaultNormalized(fMenuParam), true);
            break;
        case MenuAction::ToggleBypass:
            beginEdit();
            setNormalized(fMenuParam, normalized(fMenuParam) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(fMenuParam);
            break;
        case MenuAction::RemoveBlock:
            if (fMenuTile >= 0 && fMenuTile < static_cast<int>(fChain.blocks.size()))
                removeBlock(fMenuTile);
            break;
        case MenuAction::PresetNew:
            newPreset();
            break;
        case MenuAction::PresetOpen:
            browse(BrowseTarget::OpenPreset, "Open a NAMsayin preset (.namsayin)", presetDir());
            break;
        case MenuAction::PresetSave:
            if (!fChain.preset.empty())
            {
                savePreset(fChain.preset);
                break;
            }
            [[fallthrough]];
        case MenuAction::PresetSaveAs: {
            fBrowseTarget = BrowseTarget::SavePreset;
            FileBrowserOptions options;
            options.saving = true;
            options.title = "Save the preset";
            const std::string name = presetName() + Preset::kExtension;
            options.defaultName = name.c_str();
            const std::string dir = presetDir();
            if (!dir.empty())
                options.startDir = dir.c_str();
            openFileBrowser(options);
            break;
        }
        case MenuAction::PresetEmbed:
            updateSettings([](Settings& settings) { settings.embedFiles = !settings.embedFiles; });
            break;
        case MenuAction::PresetRecent:
            if (entry.arg < static_cast<int>(fSettings.recentPresets.size()))
                openPreset(fSettings.recentPresets[entry.arg]);
            break;
        }
    }

    // Where preset browsers start: the current preset's folder, else the last one used
    std::string presetDir() const
    {
        const std::string path = !fChain.preset.empty() ? fChain.preset
                               : !fSettings.recentPresets.empty() ? fSettings.recentPresets.front()
                               : std::string();
        return path.empty() ? std::string() : pathToUtf8(pathFromUtf8(path).parent_path());
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Settings, library, dialogs

    void applySettings()
    {
        theme::accent = theme::accents[std::clamp(fSettings.accent, 0, theme::accentCount - 1)];
        if (fFocusedField != kFieldNamDir)
            fFields[kFieldNamDir] = fSettings.namDir;
        if (fFocusedField != kFieldIrDir)
            fFields[kFieldIrDir] = fSettings.irDir;
    }

    // Changes the settings file: reloads it first, so changes made meanwhile by other instances aren't lost
    template <typename Change>
    void updateSettings(Change change)
    {
        Settings settings = Settings::load();
        change(settings);
        const bool foldersChanged = settings.namDir != fSettings.namDir || settings.irDir != fSettings.irDir;
        fSettings = settings;
        if (!fSettings.save())
            d_stderr2("NAMsayin: can't save settings to %s", pathToUtf8(Settings::defaultPath()).c_str());
        fSettingsTime = fileTime(Settings::defaultPath());
        applySettings();
        if (foldersChanged)
            rescan();
        repaint();
    }

    void rescan()
    {
        fScanner.start(fSettings.namDir, fSettings.irDir);
        fScanning = true;
    }

    void openDialog(Dialog dialog)
    {
        fDialog = dialog;
        fListScroll = 0.0f;
        repaint();
    }

    void closeDialog()
    {
        blurField(true);
        fDialog = Dialog::None;
        repaint();
    }

    void browse(BrowseTarget target, const char* title, const std::string& startDir)
    {
        fBrowseTarget = target;
        FileBrowserOptions options;
        options.title = title;
        if (!startDir.empty())
            options.startDir = startDir.c_str();
        openFileBrowser(options);
    }

    // Ends editing a text field; folder fields are saved to the settings (commit) or restored
    void blurField(bool commit)
    {
        const int field = fFocusedField;
        if (field < 0)
            return;
        fFocusedField = -1;
        if (!commit)
            fFields[field] = fFieldBeforeEdit;
        else
            commitField(static_cast<Field>(field));
        repaint();
    }

    void commitField(Field field)
    {
        if (field == kFieldNamDir && fFields[field] != fSettings.namDir)
            updateSettings([text = fFields[field]](Settings& settings) { settings.namDir = text; });
        else if (field == kFieldIrDir && fFields[field] != fSettings.irDir)
            updateSettings([text = fFields[field]](Settings& settings) { settings.irDir = text; });
    }

    void pasteInto(std::string& text)
    {
        size_t size = 0;
        const void* data = getClipboard(size);
        if (data == nullptr)
            return;
        for (size_t i = 0; i < size; ++i)
        {
            const char c = static_cast<const char*>(data)[i];
            if (c != '\n' && c != '\r' && c != '\0')
                text += c;
        }
    }

    // An image file (8-bit PNG or JPEG, also inside a zip), loaded once; nullptr if it can't be loaded
    NanoImage* image(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        auto it = fImages.find(path);
        if (it == fImages.end())
        {
            it = fImages.emplace(path, std::make_unique<NanoImage>()).first;
            if (const std::optional<std::string> bytes = readLibraryFile(path))
            {
                *it->second = createImageFromMemory(reinterpret_cast<const uchar*>(bytes->data()),
                                                    static_cast<uint>(bytes->size()), IMAGE_GENERATE_MIPMAPS);
            }
            // DPF's image decoder is an older stb_image: 8-bit PNG and JPEG work, 16-bit PNG doesn't
            if (!it->second->isValid())
                d_stderr2("NAMsayin: can't load image %s (use an 8-bit PNG or a JPEG)", path.c_str());
        }
        return it->second->isValid() ? it->second.get() : nullptr;
    }

    // The sidecar image of a block's file, if it has one
    NanoImage* blockImage(const BlockState& block)
    {
        const std::string path = block.config.value("path", std::string());
        if (path.empty())
            return nullptr;
        auto it = fSidecars.find(path);
        if (it == fSidecars.end())
            it = fSidecars.emplace(path, findSidecarImage(path)).first;
        return image(it->second);
    }

    // Draws an image scaled to fit the rect, centred, keeping its aspect ratio
    void drawImageFit(NanoImage& img, float x, float y, float w, float h, float radius)
    {
        const Size<uint> size = img.getSize();
        if (size.getWidth() == 0 || size.getHeight() == 0)
            return;
        const float scale = std::min(w / size.getWidth(), h / size.getHeight());
        const float dw = size.getWidth() * scale, dh = size.getHeight() * scale;
        const float dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
        beginPath();
        roundedRect(dx, dy, dw, dh, radius);
        fillPaint(imagePattern(dx, dy, dw, dh, 0.0f, img, 1.0f));
        fill();
    }

    // Whether a block's file exists (for a file inside a zip: whether the zip does)
    static bool fileExists(const std::string& path)
    {
        std::error_code ec;
        const std::optional<ArchivePath> archive = splitArchivePath(path);
        return std::filesystem::exists(pathFromUtf8(archive ? archive->archive : path), ec);
    }

    // Adds a block at the end of the chain, starting from its defaults (parameter values live in the host)
    void addBlock(const BlockDescriptor* type, nlohmann::json config)
    {
        const int slot = fChain.freeSlot();
        if (slot < 0)
            return;
        beginEdit();

        BlockState block;
        block.typeId = type->typeId;
        block.slot = slot;
        block.config = std::move(config);

        setHostParameter(slotBypassParam(slot), 0.0f);
        for (int p = 0; p < type->numParams; ++p)
        {
            const ParamSpec& spec = type->params[p];
            block.params[p] = spec.toNormalized(spec.def);
        }
        // defaults from Settings
        if (type == &kNamBlockDescriptor)
        {
            block.config["a2Size"] = fSettings.a2Lite ? 0.0 : 1.0;
            block.params[kNamParamNormalize] = fSettings.normalize ? 1.0f : 0.0f;
        }
        else if (type == &kIrBlockDescriptor)
        {
            block.params[kIrParamNormalize] = fSettings.normalize ? 1.0f : 0.0f;
        }
        for (int p = 0; p < type->numParams; ++p)
            setHostParameter(slotParam(slot, p), block.params[p]);

        fChain.blocks.push_back(std::move(block));
        fSelected = static_cast<int>(fChain.blocks.size()) - 1;
        sendChain();
    }

    void sendChain()
    {
        const std::string json = fChain.toJson();
#ifndef NAMSAYIN_TEST_NO_STATE_MESSAGE // test builds only: behave like a host that drops state messages
        setState("chain", json.c_str());
#endif

        // Second route for hosts that drop the state message (see state/ChainExchange.hpp)
        if (fInstanceId != 0 && writeChainExchange(fInstanceId, json))
        {
            const uint32_t revision = (static_cast<uint32_t>(fParams[kParamChainRevision]) % kMaxExchangeValue) + 1;
            setHostParameter(kParamChainRevision, static_cast<float>(revision));
        }

        chainChanged();
        repaint();
    }

    void loadExchangeChain()
    {
        const std::optional<std::string> json = readChainExchange(fInstanceId);
        if (!json)
            return;
        if (std::optional<ChainState> chain = ChainState::fromJson(*json))
        {
            fChain = std::move(*chain);
            fSavedChain = fChain;
            fUndo.clear();
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void chainChanged()
    {
        fChainChangedAt = std::chrono::steady_clock::now();
        fStatusPending = true;
    }

    bool statusSettled() const { return std::chrono::steady_clock::now() - fChainChangedAt > kStatusGrace; }

    // Whether the audio engine reports a block running in this slot
    bool slotRunning(int slot) const
    {
        const uint32_t low = static_cast<uint32_t>(fParams[kParamRunningSlotsLow]);
        const uint32_t high = static_cast<uint32_t>(fParams[kParamRunningSlotsHigh]);
        return ((low | (high << 16)) >> slot) & 1u;
    }

    bool onRelease(float x)
    {
        if (fDragParam >= 0)
        {
            editParameter(static_cast<uint32_t>(fDragParam), false);
            fDragParam = -1;
            return true;
        }
        if (fPressTile >= 0)
        {
            if (fDraggingTile)
            {
                const int count = static_cast<int>(fChain.blocks.size());
                const float pos = (x + fScrollX - 16.0f + kTileGap / 2.0f) / (kTileSize + kTileGap);
                const int target = std::clamp(static_cast<int>(std::floor(pos)), 0, count - 1);
                if (target != fPressTile)
                {
                    beginEdit();
                    BlockState moved = std::move(fChain.blocks[fPressTile]);
                    fChain.blocks.erase(fChain.blocks.begin() + fPressTile);
                    fChain.blocks.insert(fChain.blocks.begin() + target, std::move(moved));
                    fSelected = target;
                    sendChain();
                }
            }
            fPressTile = -1;
            fDraggingTile = false;
            repaint();
            return true;
        }
        return false;
    }

    const HitArea* hitAt(float x, float y) const
    {
        // last drawn is on top
        for (auto it = fHits.rbegin(); it != fHits.rend(); ++it)
        {
            if (it->rect.contains(x, y))
                return &*it;
        }
        return nullptr;
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Drawing helpers

    void fillRect(float x, float y, float w, float h, const Color& color, float radius = 0.0f)
    {
        beginPath();
        if (radius > 0.0f)
            roundedRect(x, y, w, h, radius);
        else
            rect(x, y, w, h);
        fillColor(color);
        fill();
    }

    void label(float x, float y, const char* str, const Color& color, float size, int align)
    {
        if (str == nullptr || str[0] == '\0')
            return;
        fontSize(size);
        fillColor(color);
        textAlign(align);
        text(x, y, str, nullptr);
    }

    void drawButton(const Rect& r, const char* str, bool on, const Color& onColor, Hit kind, int index)
    {
        fillRect(r.x, r.y, r.w, r.h, on ? onColor : theme::tile, 6.0f);
        label(r.x + r.w / 2, r.y + r.h / 2, str, on ? Color(255, 255, 255) : theme::text, 13.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ r, kind, index });
    }

    void drawKnob(float cx, float cy, uint32_t index, const char* name)
    {
        constexpr float start = 0.75f * static_cast<float>(M_PI);
        constexpr float sweep = 1.5f * static_cast<float>(M_PI);
        const float n = std::clamp(normalized(index), 0.0f, 1.0f);
        const float angle = start + n * sweep;

        beginPath();
        circle(cx, cy, kKnobRadius);
        fillColor(theme::tile);
        fill();

        beginPath();
        arc(cx, cy, kKnobRadius + 4.0f, start, start + sweep, CW);
        strokeColor(theme::border);
        strokeWidth(3.0f);
        stroke();

        beginPath();
        arc(cx, cy, kKnobRadius + 4.0f, start, angle, CW);
        strokeColor(theme::accent);
        strokeWidth(3.0f);
        stroke();

        beginPath();
        moveTo(cx + std::cos(angle) * 6.0f, cy + std::sin(angle) * 6.0f);
        lineTo(cx + std::cos(angle) * (kKnobRadius - 4.0f), cy + std::sin(angle) * (kKnobRadius - 4.0f));
        strokeColor(theme::text);
        strokeWidth(2.5f);
        stroke();

        if (fLearnParam == static_cast<int>(index))
        {
            beginPath();
            circle(cx, cy, kKnobRadius + 9.0f);
            strokeColor(theme::accent);
            strokeWidth(1.5f);
            stroke();
        }

        label(cx, cy + kKnobRadius + 16.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(cx, cy + kKnobRadius + 31.0f, valueText(index).c_str(), theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);

        fHits.push_back({ { cx - kKnobRadius - 6, cy - kKnobRadius - 6, 2 * kKnobRadius + 12, 2 * kKnobRadius + 40 },
                          Hit::Knob, static_cast<int>(index) });
    }

    // A choice parameter: its name, and a button showing the current choice that steps to the next one
    void drawChoice(float cx, float cy, uint32_t index, const char* name)
    {
        label(cx, cy - 26.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawButton({ cx - 42.0f, cy - 14.0f, 84.0f, 28.0f }, valueText(index).c_str(), false, theme::accent,
                   Hit::Choice, static_cast<int>(index));
    }

    // Gain-reduction style meter for a block (fills downward from the top), in dB
    void drawBlockMeter(float cx, float cy, float db, float maxDb, const char* name)
    {
        constexpr float w = 10.0f, h = 64.0f;
        const float x = cx - w / 2, y = cy - h / 2 - 6.0f;
        fillRect(x, y, w, h, theme::inset, 2.0f);
        const float level = std::clamp(db / maxDb, 0.0f, 1.0f);
        if (level > 0.0f)
            fillRect(x, y, w, h * level, theme::warn, 2.0f);
        label(cx, cy + kKnobRadius + 16.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        char text[24];
        if (db >= 0.05f)
            std::snprintf(text, sizeof(text), "-%.1f dB", db);
        else
            std::snprintf(text, sizeof(text), "0.0 dB");
        label(cx, cy + kKnobRadius + 31.0f, text, theme::text, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // Placeholder art until real images exist: a pedal, an amp head or a speaker cab, fitted into the rect
    void drawBlockArt(const BlockDescriptor* type, float x, float y, float w, float h, bool detailed)
    {
        if (type == nullptr)
            return;
        const float cx = x + w / 2, cy = y + h / 2;

        if (type->category == Category::LocalIr)
        {
            // cab: a 2x2 of speakers
            const float size = std::min(w, h);
            const float bx = cx - size / 2, by = cy - size / 2;
            fillRect(bx, by, size, size, theme::cab.withAlpha(0.35f), size * 0.06f);
            fillRect(bx + size * 0.05f, by + size * 0.05f, size * 0.9f, size * 0.9f, theme::inset, size * 0.04f);
            for (int r = 0; r < 2; ++r)
            {
                for (int c = 0; c < 2; ++c)
                {
                    const float sx = bx + size * (0.28f + 0.44f * c), sy = by + size * (0.28f + 0.44f * r);
                    for (const float k : { 0.19f, 0.11f, 0.04f })
                    {
                        beginPath();
                        circle(sx, sy, size * k);
                        strokeColor(theme::cab.withAlpha(0.9f));
                        strokeWidth(std::max(1.0f, size * 0.012f));
                        stroke();
                    }
                }
            }
            return;
        }

        if (type->category == Category::LocalNam)
        {
            // amp head: a control strip over a grille, landscape
            float aw = w, ah = w / 1.7f;
            if (ah > h)
            {
                ah = h;
                aw = h * 1.7f;
            }
            const float ax = cx - aw / 2, ay = cy - ah / 2;
            fillRect(ax, ay, aw, ah, theme::amp.withAlpha(0.45f), aw * 0.04f);
            fillRect(ax + aw * 0.05f, ay + ah * 0.08f, aw * 0.9f, ah * 0.22f, theme::inset, aw * 0.015f);
            if (detailed)
            {
                for (int k = 0; k < 6; ++k)
                {
                    beginPath();
                    circle(ax + aw * (0.2f + 0.12f * k), ay + ah * 0.19f, ah * 0.06f);
                    fillColor(theme::text.withAlpha(0.6f));
                    fill();
                }
            }
            const float gx = ax + aw * 0.05f, gy = ay + ah * 0.38f, gw = aw * 0.9f, gh = ah * 0.52f;
            fillRect(gx, gy, gw, gh, theme::inset, aw * 0.015f);
            const int lines = detailed ? 9 : 5;
            for (int row = 0; row < lines; ++row)
            {
                const float ly = gy + gh * (row + 0.5f) / lines;
                beginPath();
                moveTo(gx + gw * 0.04f, ly);
                lineTo(gx + gw * 0.96f, ly);
                strokeColor(theme::amp.withAlpha(0.6f));
                strokeWidth(std::max(1.0f, ah * 0.012f));
                stroke();
            }
            return;
        }

        // pedal: portrait enclosure with knobs, a name, an LED and a footswitch
        Color color = theme::pedalOther;
        if (type->category == Category::Delay)
            color = theme::pedalDelay;
        else if (type->category == Category::Reverb)
            color = theme::pedalReverb;
        else if (type->category == Category::Dynamics)
            color = theme::pedalDynamics;

        float pw = h * 0.62f, ph = h;
        if (pw > w)
        {
            pw = w;
            ph = w / 0.62f;
        }
        const float px = cx - pw / 2, py = cy - ph / 2;
        fillRect(px, py, pw, ph, color.withAlpha(0.5f), pw * 0.08f);
        beginPath();
        roundedRect(px, py, pw, ph, pw * 0.08f);
        strokeColor(color);
        strokeWidth(std::max(1.0f, pw * 0.015f));
        stroke();

        const int knobs = detailed ? 3 : 2;
        for (int k = 0; k < knobs; ++k)
        {
            const float kx = px + pw * (k + 1) / (knobs + 1);
            beginPath();
            circle(kx, py + ph * 0.17f, pw * 0.085f);
            fillColor(theme::inset);
            fill();
        }
        if (detailed)
        {
            label(cx, py + ph * 0.45f, type->displayName, theme::text, std::max(10.0f, pw * 0.13f),
                  ALIGN_CENTER | ALIGN_MIDDLE);
        }
        beginPath();
        circle(cx, py + ph * 0.62f, pw * 0.035f);
        fillColor(theme::bad.withAlpha(0.9f));
        fill();
        beginPath();
        circle(cx, py + ph * 0.8f, pw * 0.12f);
        fillColor(theme::inset);
        fill();
        beginPath();
        circle(cx, py + ph * 0.8f, pw * 0.08f);
        fillColor(theme::textDim);
        fill();
    }

    void drawMeter(float x, float y, float h, uint32_t left, uint32_t right, bool latched, Hit clipHit,
                   const char* name)
    {
        constexpr float barWidth = 8.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float bx = x + ch * (barWidth + 3.0f);
            fillRect(bx, y, barWidth, h, theme::inset, 2.0f);
            const float db = fParams[ch == 0 ? left : right];
            const float level = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f); // -60..+6 dB
            const Color color = db > -0.5f ? theme::bad : (db > -12.0f ? theme::warn : theme::good);
            fillRect(bx, y + h * (1.0f - level), barWidth, h * level, color, 2.0f);
        }
        // clip light, click to reset
        const Rect clip { x, y - 14.0f, 2 * barWidth + 3.0f, 8.0f };
        fillRect(clip.x, clip.y, clip.w, clip.h, latched ? theme::bad : theme::tile, 2.0f);
        fHits.push_back({ { clip.x - 4, clip.y - 4, clip.w + 8, clip.h + 8 }, clipHit, 0 });
        label(x + barWidth + 1.5f, y + h + 12.0f, name, theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Sections

    void drawTopBar()
    {
        fillRect(0, 0, fWidth, kTopBarHeight, theme::bar);
        label(16.0f, kTopBarHeight / 2, "NAMsayin", theme::text, 20.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        // preset menu with the preset's name (• when changed since it was saved or opened)
        const std::string preset = presetName() + (presetModified() ? " •" : "") + "  ▾";
        drawButton({ 140.0f, 10.0f, 220.0f, 28.0f }, preset.c_str(), fDialog == Dialog::Menu && fMenuParam == kNoParam,
                   theme::accent, Hit::PresetMenu, 0);
        drawButton({ 368.0f, 10.0f, 56.0f, 28.0f }, "Undo", false, theme::accent, Hit::Undo, 0);
        drawButton({ 430.0f, 10.0f, 56.0f, 28.0f }, "Redo", false, theme::accent, Hit::Redo, 0);
        if (!fUndo.canUndo())
            fillRect(368.0f, 10.0f, 56.0f, 28.0f, theme::bar.withAlpha(0.6f), 6.0f);
        if (!fUndo.canRedo())
            fillRect(430.0f, 10.0f, 56.0f, 28.0f, theme::bar.withAlpha(0.6f), 6.0f);

        if (fLearnParam >= 0)
        {
            const std::string text = "MIDI learn: move a controller for " + paramName(fLearnParam) + "  (Esc cancels)";
            label(500.0f, kTopBarHeight / 2, text.c_str(), theme::accent, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        drawButton({ fWidth - 96.0f, 10.0f, 80.0f, 28.0f }, "Bypass", fParams[kParamBypass] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamBypass);
        drawButton({ fWidth - 192.0f, 10.0f, 84.0f, 28.0f }, "Settings", fDialog == Dialog::Settings, theme::accent,
                   Hit::OpenSettings, 0);
    }

    void drawChain()
    {
        const int count = static_cast<int>(fChain.blocks.size());
        const float y = kChainTop;

        scissor(0, kTopBarHeight, fWidth, kTileSize + 48.0f);
        for (int i = 0; i <= count; ++i)
        {
            float x = 16.0f + i * (kTileSize + kTileGap) - fScrollX;
            if (i < count && fDraggingTile && i == fPressTile)
                continue; // drawn last, under the mouse

            if (i == count)
            {
                drawAddTile(x, y);
                break;
            }
            drawTile(i, x, y);

            // signal-flow arrow to the next tile
            beginPath();
            moveTo(x + kTileSize + 3.0f, y + kTileSize / 2);
            lineTo(x + kTileSize + kTileGap - 3.0f, y + kTileSize / 2);
            strokeColor(theme::border);
            strokeWidth(2.0f);
            stroke();
        }
        if (fDraggingTile && fPressTile >= 0 && fPressTile < count)
            drawTile(fPressTile, fMouseX - kTileSize / 2, y - 6.0f);
        resetScissor();

        if (count == 0)
            label(16.0f + kTileSize + 32.0f, y + kTileSize / 2, "Click + to add a block", theme::textDim,
                  14.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        bool anyRunning = false;
        for (const BlockState& block : fChain.blocks)
            anyRunning |= slotRunning(block.slot);
        if (count > 0 && !anyRunning && statusSettled())
            label(16.0f, y + kTileSize + 14.0f,
                  "The audio engine hasn't started this chain. The host may not pass the plugin's UI messages "
                  "to it (some LV2 hosts don't); try the VST3 version.",
                  theme::bad, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawAddTile(float x, float y)
    {
        beginPath();
        roundedRect(x, y, kTileSize, kTileSize, 10.0f);
        strokeColor(theme::border);
        strokeWidth(2.0f);
        stroke();
        label(x + kTileSize / 2, y + kTileSize / 2 - 2, "+", theme::textDim, 44.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        if (fChain.blocks.size() < static_cast<size_t>(kMaxBlocks))
            fHits.push_back({ { x, y, kTileSize, kTileSize }, Hit::AddBlock, 0 });
    }

    void drawTile(int i, float x, float y)
    {
        const BlockState& block = fChain.blocks[i];
        const std::string path = block.config.value("path", std::string());
        const bool missing = !path.empty() && !fileExists(path);
        const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
        const bool notRunning = !missing && statusSettled() && !slotRunning(block.slot);
        const bool hover = Rect { x, y, kTileSize, kTileSize }.contains(fMouseX, fMouseY);

        fillRect(x, y, kTileSize, kTileSize, hover ? theme::tileHover : theme::tile, 10.0f);
        if (i == fSelected)
        {
            beginPath();
            roundedRect(x, y, kTileSize, kTileSize, 10.0f);
            strokeColor(theme::accent);
            strokeWidth(2.0f);
            stroke();
        }

        // thumbnail (placeholder art until real images)
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        globalAlpha(bypassed || missing || notRunning ? 0.35f : 1.0f);
        if (NanoImage* img = blockImage(block))
            drawImageFit(*img, x + 16, y + 24, kTileSize - 32, 58, 4.0f);
        else
            drawBlockArt(type, x + 16, y + 24, kTileSize - 32, 58, false);
        std::string name = missing ? "Missing file"
                         : notRunning ? "Not running"
                         : path.empty() ? (type ? type->displayName : block.typeId)
                         : fileStem(path);
        if (name.size() > 16)
            name = name.substr(0, 15) + "…";
        label(x + kTileSize / 2, y + 94, name.c_str(), missing || notRunning ? theme::bad : theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        globalAlpha(1.0f);

        char slotText[16];
        std::snprintf(slotText, sizeof(slotText), "S%02d", block.slot + 1);
        label(x + kTileSize / 2, y + 110, slotText, theme::textDim, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        fHits.push_back({ { x, y, kTileSize, kTileSize }, Hit::Tile, i });

        // bypass (top left) and remove (top right)
        const Rect power { x + 6, y + 4, 18, 18 };
        beginPath();
        circle(power.x + 9, power.y + 9, 6.0f);
        fillColor(bypassed ? theme::border : theme::good);
        fill();
        fHits.push_back({ power, Hit::TileBypass, i });

        const Rect remove { x + kTileSize - 24, y + 4, 18, 18 };
        label(remove.x + 9, remove.y + 9, "×", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ remove, Hit::TileRemove, i });
    }

    void drawSelectedPanel()
    {
        const float top = kChainTop + kTileSize + 28.0f;
        const float left = 16.0f;
        const float width = fWidth - 32.0f;
        const float height = fHeight - kBottomBarHeight - top - 16.0f;
        if (height < 60.0f)
            return;
        fillRect(left, top, width, height, theme::panel, 10.0f);

        if (fSelected < 0 || fSelected >= static_cast<int>(fChain.blocks.size()))
        {
            label(fWidth / 2, top + height / 2, "Select a block to edit it", theme::textDim, 14.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
            return;
        }

        const BlockState& block = fChain.blocks[fSelected];
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        if (!type)
            return;

        // The controls: one cell each, plus block-specific extras
        enum class Cell { Param, A2Size, Meter };
        struct Control {
            Cell cell;
            int param;
        };
        std::vector<Control> controls;
        for (int p = 0; p < type->numParams; ++p)
            controls.push_back({ Cell::Param, p });
        if (type == &kNamBlockDescriptor)
            controls.push_back({ Cell::A2Size, 0 });
        if (type == &kCompressorBlockDescriptor)
            controls.push_back({ Cell::Meter, 0 });
        const int count = static_cast<int>(controls.size());

        // Layout: the block image on the left, the controls right-justified in rows. Use fewer columns (more
        // rows) when one row would leave the image small and there's height for it; in a small window, narrow
        // the cells instead.
        constexpr float kHeaderHeight = 52.0f;
        const float imageMax = height - 2 * kPanelMargin;
        const float rowsSpace = height - kHeaderHeight - kPanelMargin;
        const int rowsAvailable = std::max(1, static_cast<int>(rowsSpace / kCellHeight));
        const int minColumns = (count + rowsAvailable - 1) / rowsAvailable;
        const float freeWidth = width - 3 * kPanelMargin;
        int columns = std::max(count, 1);
        while (columns > minColumns && freeWidth - columns * kCellWidth < std::min(imageMax, 240.0f))
            --columns;
        const int rows = (count + columns - 1) / columns;
        float cellWidth = kCellWidth;
        const float imageWanted = std::min(imageMax, 140.0f);
        if (freeWidth - columns * cellWidth < imageWanted)
            cellWidth = std::max(76.0f, (freeWidth - imageWanted) / columns);
        const float imageSize = std::clamp(freeWidth - columns * cellWidth, 0.0f, imageMax);

        // Image area (placeholder art for now; real pedal/amp/cab images go here)
        const float imageX = left + kPanelMargin, imageY = top + kPanelMargin;
        if (imageSize >= 60.0f)
        {
            fillRect(imageX, imageY, imageSize, imageSize, theme::inset, 8.0f);
            const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
            globalAlpha(bypassed ? 0.35f : 1.0f);
            if (NanoImage* img = blockImage(block))
                drawImageFit(*img, imageX + 8.0f, imageY + 8.0f, imageSize - 16.0f, imageSize - 16.0f, 6.0f);
            else
                drawBlockArt(type, imageX + imageSize * 0.1f, imageY + imageSize * 0.1f, imageSize * 0.8f,
                             imageSize * 0.8f, true);
            globalAlpha(1.0f);
        }

        // Header: name and file, above the controls
        const float textX = imageSize >= 60.0f ? imageX + imageSize + kPanelMargin : left + kPanelMargin;
        const std::string path = block.config.value("path", std::string());
        label(textX, top + 26.0f, path.empty() ? type->displayName : fileStem(path).c_str(), theme::text, 16.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        label(textX, top + 46.0f, path.empty() ? categoryName(type->category) : path.c_str(), theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);

        if (!path.empty() && !fileExists(path))
        {
            label(textX, top + 70.0f, "File not found. The block passes audio through until it's located.", theme::bad,
                  12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            float bx = textX;
            if (const LibraryEntry* match = libraryMatch(block))
            {
                const std::string text = "Use " + match->name + " from the library";
                drawButton({ bx, top + 86.0f, 260.0f, 28.0f }, text.c_str(), true, theme::accent, Hit::UseLibraryMatch,
                           fSelected);
                bx += 268.0f;
            }
            drawButton({ bx, top + 86.0f, 96.0f, 28.0f }, "Locate…", false, theme::accent, Hit::Locate, fSelected);
        }

        const float gridRight = left + width - kPanelMargin;
        const float gridTop = top + kHeaderHeight + std::max(0.0f, (rowsSpace - rows * kCellHeight) / 2);
        for (int i = 0; i < count; ++i)
        {
            const int row = i / columns;
            const int col = i % columns;
            // right-justified: the last row's cells sit against the right edge too
            const int inRow = row == rows - 1 ? count - row * columns : columns;
            const float cx = gridRight - (inRow - col - 0.5f) * cellWidth;
            const float cy = gridTop + row * kCellHeight + 8.0f + kKnobRadius + 6.0f;
            const Control& control = controls[i];

            if (control.cell == Cell::A2Size)
            {
                const bool lite = block.config.value("a2Size", 1.0) < 0.5;
                drawButton({ cx - 40.0f, cy - 14.0f, 80.0f, 28.0f }, lite ? "A2: Lite" : "A2: Full", lite,
                           theme::accent, Hit::A2Size, fSelected);
                continue;
            }
            if (control.cell == Cell::Meter)
            {
                drawBlockMeter(cx, cy, fParams[slotMeterParam(block.slot)], 24.0f, "Reduction");
                continue;
            }

            const ParamSpec& spec = type->params[control.param];
            const uint32_t index = slotParam(block.slot, control.param);
            if (spec.scale == ParamScale::Toggle)
                drawButton({ cx - 40.0f, cy - 14.0f, 80.0f, 28.0f }, spec.name, fParams[index] >= 0.5f, theme::accent,
                           Hit::Toggle, static_cast<int>(index));
            else if (spec.scale == ParamScale::Choice)
                drawChoice(cx, cy, index, spec.name);
            else
                drawKnob(cx, cy, index, spec.name);
        }
    }

    void drawAddDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(680.0f, fWidth - 64.0f);
        const float h = std::min(440.0f, fHeight - 64.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        fillRect(x, y, w, h, theme::dialog, 12.0f);
        beginPath();
        roundedRect(x, y, w, h, 12.0f);
        strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, "Add Block", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        // categories on the left
        const float listX = x + 12.0f, listTop = y + 52.0f, listW = 190.0f;
        const float rowH = std::min(26.0f, (h - 64.0f) / kCategoryCount);
        for (int c = 0; c < kCategoryCount; ++c)
        {
            const auto category = static_cast<Category>(c);
            const Rect r { listX, listTop + c * rowH, listW, rowH - 2.0f };
            const bool selected = category == fDialogCategory;
            const bool available = isLibraryCategory(category) || !addItems(category).empty();
            if (selected)
                fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(0.25f), 5.0f);
            else if (r.contains(fMouseX, fMouseY))
                fillRect(r.x, r.y, r.w, r.h, theme::tileHover, 5.0f);
            label(r.x + 10.0f, r.y + r.h / 2, categoryName(category), available ? theme::text : theme::textDim, 13.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);
            fHits.push_back({ r, Hit::DialogCategory, c });
        }

        // the category's blocks on the right
        const float itemsX = listX + listW + 16.0f, itemsW = x + w - 16.0f - itemsX;
        if (isLibraryCategory(fDialogCategory))
        {
            drawLibraryList(itemsX, listTop, itemsW, y + h - 12.0f - listTop);
            return;
        }
        const std::vector<AddItem> items = addItems(fDialogCategory);
        if (items.empty())
        {
            const bool tone3000 = fDialogCategory == Category::Tone3000AmpHead
                               || fDialogCategory == Category::Tone3000AmpCab
                               || fDialogCategory == Category::Tone3000Cab;
            label(itemsX + 8.0f, listTop + 16.0f, tone3000 ? "Tone3000 integration comes in a later version."
                                                           : "No blocks in this category yet.",
                  theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            return;
        }
        const float itemH = 64.0f;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const Rect r { itemsX, listTop + i * (itemH + 8.0f), itemsW, itemH };
            fillRect(r.x, r.y, r.w, r.h, r.contains(fMouseX, fMouseY) ? theme::tileHover : theme::tile, 8.0f);
            drawBlockArt(items[i].type, r.x + 10.0f, r.y + 8.0f, 60.0f, r.h - 16.0f, false);
            label(r.x + 84.0f, r.y + r.h / 2, items[i].label.c_str(), theme::text, 14.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            fHits.push_back({ r, Hit::DialogItem, static_cast<int>(i) });
        }
    }

    // A single-line text field. Click to edit; Enter or clicking elsewhere ends editing.
    void drawTextField(const Rect& r, Field field, const char* placeholder)
    {
        const bool focused = fFocusedField == field;
        fillRect(r.x, r.y, r.w, r.h, theme::inset, 6.0f);
        beginPath();
        roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 6.0f);
        strokeColor(focused ? theme::accent : theme::border);
        strokeWidth(1.0f);
        stroke();

        save();
        scissor(r.x + 6.0f, r.y, r.w - 12.0f, r.h);
        const std::string& text = fFields[field];
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        Rectangle<float> bounds;
        const float textWidth = text.empty() ? 0.0f : textBounds(0, 0, text.c_str(), nullptr, bounds);
        // keep the end (and the cursor) in view while typing
        const float tx = r.x + 10.0f - std::max(0.0f, textWidth - (r.w - 24.0f));
        if (text.empty() && !focused)
            label(r.x + 10.0f, r.y + r.h / 2, placeholder, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        else
            label(tx, r.y + r.h / 2, text.c_str(), theme::text, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        if (focused)
            fillRect(tx + textWidth + 1.0f, r.y + 7.0f, 1.5f, r.h - 14.0f, theme::accent);
        restore();

        fHits.push_back({ r, Hit::TextField, field });
    }

    // The library list for Local NAM / Local IR: search, favorites filter, browse, and the scanned files
    void drawLibraryList(float x, float y, float w, float h)
    {
        const bool ir = fDialogCategory == Category::LocalIr;
        const std::vector<LibraryEntry>& library = ir ? fIrLibrary : fNamLibrary;
        const std::string& folder = ir ? fSettings.irDir : fSettings.namDir;

        // header: search, favorites, browse
        const float headerH = 30.0f;
        drawTextField({ x, y, w - 200.0f, headerH }, kFieldSearch, ir ? "Search IRs" : "Search models");
        drawButton({ x + w - 192.0f, y, 92.0f, headerH }, "★ Favorites", fFavoritesOnly, theme::accent,
                   Hit::LibraryFavorites, 0);
        drawButton({ x + w - 92.0f, y, 92.0f, headerH }, "Browse…", false, theme::accent, Hit::LibraryBrowse, 0);

        const float listY = y + headerH + 10.0f;
        const float listH = h - headerH - 10.0f;

        fVisibleEntries.clear();
        for (const LibraryEntry& entry : library)
        {
            if (fFavoritesOnly && fSettings.favorites.count(entry.hash) == 0)
                continue;
            if (!matchesSearch(entry, fFields[kFieldSearch]))
                continue;
            fVisibleEntries.push_back(&entry);
        }

        auto message = [&](const char* text) {
            label(x + 8.0f, listY + 18.0f, text, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        };
        if (folder.empty())
        {
            message(ir ? "Set your IR folder in Settings to list your cab IRs here."
                       : "Set your NAM folder in Settings to list your models here.");
            drawButton({ x + 8.0f, listY + 40.0f, 120.0f, 28.0f }, "Open Settings", false, theme::accent,
                       Hit::OpenSettings, 0);
            return;
        }
        if (fVisibleEntries.empty())
        {
            if (fScanning)
                message("Scanning…");
            else if (library.empty())
                message(ir ? "No .wav files found in your IR folder." : "No .nam files found in your NAM folder.");
            else
                message(fFavoritesOnly ? "No favorites match." : "No matches.");
            return;
        }

        constexpr float rowH = 52.0f;
        const float contentH = fVisibleEntries.size() * rowH;
        fListScroll = std::clamp(fListScroll, 0.0f, std::max(0.0f, contentH - listH));

        save();
        scissor(x, listY, w, listH);
        const int first = static_cast<int>(fListScroll / rowH);
        for (int i = first; i < static_cast<int>(fVisibleEntries.size()); ++i)
        {
            const float ry = listY + i * rowH - fListScroll;
            if (ry > listY + listH)
                break;
            const LibraryEntry& entry = *fVisibleEntries[i];
            const Rect r { x, ry, w, rowH - 4.0f };
            const bool hover = r.contains(fMouseX, fMouseY) && fMouseY >= listY && fMouseY < listY + listH;
            fillRect(r.x, r.y, r.w, r.h, hover ? theme::tileHover : theme::tile, 6.0f);

            if (NanoImage* img = image(entry.image))
                drawImageFit(*img, r.x + 6.0f, r.y + 4.0f, 52.0f, r.h - 8.0f, 3.0f);
            else
                drawBlockArt(ir ? &kIrBlockDescriptor : &kNamBlockDescriptor, r.x + 6.0f, r.y + 6.0f, 52.0f,
                             r.h - 12.0f, false);

            label(r.x + 68.0f, r.y + 17.0f, entry.name.c_str(), theme::text, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            std::string detail = entry.folder;
            if (!entry.info.empty())
                detail += (detail.empty() ? "" : "  ·  ") + entry.info;
            label(r.x + 68.0f, r.y + 34.0f, detail.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

            const bool favorite = fSettings.favorites.count(entry.hash) != 0;
            const Rect star { r.x + r.w - 40.0f, r.y, 40.0f, r.h };
            label(star.x + 20.0f, star.y + r.h / 2, favorite ? "★" : "☆", favorite ? theme::accent : theme::textDim,
                  18.0f, ALIGN_CENTER | ALIGN_MIDDLE);

            // only rows inside the list area are clickable
            const float top = std::max(r.y, listY), bottom = std::min(r.y + r.h, listY + listH);
            if (bottom > top)
            {
                fHits.push_back({ { r.x, top, r.w - 40.0f, bottom - top }, Hit::LibraryItem, i });
                fHits.push_back({ { star.x, top, star.w, bottom - top }, Hit::LibraryStar, i });
            }
        }
        restore();

        char count[64];
        std::snprintf(count, sizeof(count), "%zu of %zu", fVisibleEntries.size(), library.size());
        label(x + w, y + h + 2.0f, count, theme::textDim, 10.0f, ALIGN_RIGHT | ALIGN_BOTTOM);
    }

    void drawMenu()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        constexpr float itemH = 28.0f, menuW = 260.0f;
        const float h = fMenu.size() * itemH + 8.0f;
        const float x = std::min(fMenuX, fWidth - menuW - 8.0f);
        const float y = std::min(fMenuY, fHeight - h - 8.0f);
        fillRect(x, y, menuW, h, theme::dialog, 8.0f);
        beginPath();
        roundedRect(x, y, menuW, h, 8.0f);
        strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        fHits.push_back({ { x, y, menuW, h }, Hit::DialogPanel, 0 });

        for (size_t i = 0; i < fMenu.size(); ++i)
        {
            const Rect r { x + 4.0f, y + 4.0f + i * itemH, menuW - 8.0f, itemH };
            const bool heading = fMenu[i].action == MenuAction::None;
            if (!heading && r.contains(fMouseX, fMouseY))
                fillRect(r.x, r.y, r.w, r.h, theme::accent.withAlpha(0.3f), 5.0f);
            label(r.x + 10.0f, r.y + r.h / 2, fMenu[i].label.c_str(), heading ? theme::textDim : theme::text,
                  heading ? 11.0f : 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            if (!heading)
                fHits.push_back({ r, Hit::MenuItem, static_cast<int>(i) });
        }
    }

    void drawToast()
    {
        if (fToast.empty())
            return;
        if (std::chrono::steady_clock::now() - fToastAt > std::chrono::seconds(5))
        {
            fToast.clear();
            return;
        }
        fontSize(13.0f);
        Rectangle<float> bounds;
        const float w = textBounds(0, 0, fToast.c_str(), nullptr, bounds) + 32.0f;
        const float x = (fWidth - w) / 2, y = fHeight - kBottomBarHeight - 52.0f;
        fillRect(x, y, w, 32.0f, theme::dialog, 8.0f);
        beginPath();
        roundedRect(x, y, w, 32.0f, 8.0f);
        strokeColor(theme::accent.withAlpha(0.6f));
        strokeWidth(1.0f);
        stroke();
        label(fWidth / 2, y + 16.0f, fToast.c_str(), theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawSettingsDialog()
    {
        fHits.push_back({ { 0, 0, fWidth, fHeight }, Hit::DialogBackdrop, 0 });
        fillRect(0, 0, fWidth, fHeight, theme::overlay);

        const float w = std::min(640.0f, fWidth - 64.0f);
        const float h = std::min(420.0f, fHeight - 48.0f);
        const float x = (fWidth - w) / 2, y = (fHeight - h) / 2;
        fillRect(x, y, w, h, theme::dialog, 12.0f);
        beginPath();
        roundedRect(x, y, w, h, 12.0f);
        strokeColor(theme::border);
        strokeWidth(1.0f);
        stroke();
        fHits.push_back({ { x, y, w, h }, Hit::DialogPanel, 0 });

        label(x + 20.0f, y + 26.0f, "Settings", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const Rect close { x + w - 40.0f, y + 10.0f, 30.0f, 30.0f };
        label(close.x + 15.0f, close.y + 15.0f, "×", theme::textDim, 22.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ close, Hit::DialogClose, 0 });

        const float labelX = x + 24.0f;
        const float controlX = x + 200.0f;
        const float controlW = x + w - 24.0f - controlX;
        float row = y + 64.0f;
        constexpr float rowH = 44.0f;
        auto rowLabel = [&](const char* text) {
            label(labelX, row + 14.0f, text, theme::textDim, 13.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        };

        rowLabel("NAM folder");
        drawTextField({ controlX, row, controlW - 96.0f, 28.0f }, kFieldNamDir, "Not set");
        drawButton({ controlX + controlW - 88.0f, row, 88.0f, 28.0f }, "Choose…", false, theme::accent,
                   Hit::SettingsChoose, kFieldNamDir);
        row += rowH;

        rowLabel("IR folder");
        drawTextField({ controlX, row, controlW - 96.0f, 28.0f }, kFieldIrDir, "Not set");
        drawButton({ controlX + controlW - 88.0f, row, 88.0f, 28.0f }, "Choose…", false, theme::accent,
                   Hit::SettingsChoose, kFieldIrDir);
        row += rowH;

        rowLabel("Library");
        drawButton({ controlX, row, 88.0f, 28.0f }, "Rescan", false, theme::accent, Hit::SettingsRescan, 0);
        char status[96];
        if (fScanning)
            std::snprintf(status, sizeof(status), "Scanning…");
        else
            std::snprintf(status, sizeof(status), "%zu NAM model%s, %zu IR%s, %zu favorite%s", fNamLibrary.size(),
                          fNamLibrary.size() == 1 ? "" : "s", fIrLibrary.size(), fIrLibrary.size() == 1 ? "" : "s",
                          fSettings.favorites.size(), fSettings.favorites.size() == 1 ? "" : "s");
        label(controlX + 100.0f, row + 14.0f, status, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Default A2 size");
        drawButton({ controlX, row, 72.0f, 28.0f }, "Full", !fSettings.a2Lite, theme::accent, Hit::SettingsA2, 0);
        drawButton({ controlX + 78.0f, row, 72.0f, 28.0f }, "Lite", fSettings.a2Lite, theme::accent,
                   Hit::SettingsA2, 1);
        label(controlX + 166.0f, row + 14.0f, "for new NAM blocks; each block can override it", theme::textDim,
              11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Normalize new blocks");
        drawButton({ controlX, row, 72.0f, 28.0f }, fSettings.normalize ? "On" : "Off", fSettings.normalize,
                   theme::accent, Hit::SettingsNormalize, 0);
        label(controlX + 88.0f, row + 14.0f, "loudness normalization for new NAM and IR blocks", theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Multi-core splits");
        drawButton({ controlX, row, 72.0f, 28.0f }, fSettings.parallel ? "On" : "Off", fSettings.parallel,
                   theme::accent, Hit::SettingsParallel, 0);
        label(controlX + 88.0f, row + 14.0f, "run the second branch of a split on another core", theme::textDim,
              11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        row += rowH;

        rowLabel("Accent colour");
        for (int i = 0; i < theme::accentCount; ++i)
        {
            const float cx = controlX + 14.0f + i * 34.0f, cy = row + 14.0f;
            beginPath();
            circle(cx, cy, 11.0f);
            fillColor(theme::accents[i]);
            fill();
            if (i == fSettings.accent)
            {
                beginPath();
                circle(cx, cy, 14.5f);
                strokeColor(theme::text);
                strokeWidth(1.5f);
                stroke();
            }
            fHits.push_back({ { cx - 15.0f, cy - 15.0f, 30.0f, 30.0f }, Hit::SettingsAccent, i });
        }

        label(x + 24.0f, y + h - 18.0f, pathToUtf8(Settings::defaultPath()).c_str(), theme::textDim, 10.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawBottomBar()
    {
        const float top = fHeight - kBottomBarHeight;
        fillRect(0, top, fWidth, kBottomBarHeight, theme::bar);

        const float cy = top + 46.0f;
        const bool stereo = fParams[kParamInputMode] >= 0.5f;
        drawButton({ 24.0f, cy - 14.0f, 80.0f, 28.0f }, stereo ? "Stereo" : "Mono", stereo, theme::accent,
                   Hit::Toggle, kParamInputMode);

        drawMeter(128.0f, top + 22.0f, 80.0f, kParamInPeakL, kParamInPeakR, fInClipLatched, Hit::InClip, "IN");

        const float knobsStart = 200.0f;
        const float spacing = std::clamp((fWidth - knobsStart - 260.0f) / 5.0f, 80.0f, 140.0f);
        const uint32_t knobs[] = { kParamInput, kParamBass, kParamMiddle, kParamTreble, kParamOutput };
        for (int i = 0; i < 5; ++i)
            drawKnob(knobsStart + spacing * (i + 0.5f), cy, knobs[i], globalParamSpec(knobs[i]).name);

        const float right = knobsStart + spacing * 5.0f + 24.0f;
        drawMeter(right, top + 22.0f, 80.0f, kParamOutPeakL, kParamOutPeakR, fOutClipLatched, Hit::OutClip, "OUT");

        const bool limiter = fParams[kParamLimiter] >= 0.5f;
        drawButton({ right + 40.0f, cy - 14.0f, 80.0f, 28.0f }, "Limiter", limiter, theme::accent, Hit::Toggle,
                   kParamLimiter);
        if (limiter && fParams[kParamLimiterReduction] > 0.5f)
        {
            char gr[24];
            std::snprintf(gr, sizeof(gr), "-%.1f dB", fParams[kParamLimiterReduction]);
            label(right + 80.0f, cy + 26.0f, gr, theme::warn, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------

    float fParams[kParamCount] {};
    ChainState fChain;
    int fSelected = -1;

    std::vector<HitArea> fHits;
    float fWidth = DISTRHO_UI_DEFAULT_WIDTH;
    float fHeight = DISTRHO_UI_DEFAULT_HEIGHT;
    float fMouseX = -1.0f, fMouseY = -1.0f;
    float fScrollX = 0.0f;

    int fDragParam = -1;
    float fDragStartY = 0.0f;
    float fDragStartValue = 0.0f;
    uint fLastClickTime = 0;

    int fPressTile = -1;
    float fPressX = 0.0f;
    bool fDraggingTile = false;

    static constexpr uint32_t kNoParam = 0xffffffffu;

    Dialog fDialog = Dialog::None;
    std::vector<MenuEntry> fMenu;
    float fMenuX = 0.0f, fMenuY = 0.0f;
    uint32_t fMenuParam = kNoParam; // the parameter a context menu is for
    int fMenuTile = -1;

    UndoStack fUndo;
    ChainState fSavedChain; // the chain as last saved or opened, for the modified mark
    int fLocateIndex = -1;
    int fLearnParam = -1;
    float fLearnCcCount = 0.0f;
    std::string fToast;
    std::chrono::steady_clock::time_point fToastAt;
    Category fDialogCategory = Category::LocalNam;
    float fListScroll = 0.0f;
    bool fFavoritesOnly = false;
    std::vector<const LibraryEntry*> fVisibleEntries; // library rows drawn last, for hit indices
    BrowseTarget fBrowseTarget = BrowseTarget::AddBlock;

    std::string fFields[kFieldCount];
    int fFocusedField = -1;
    std::string fFieldBeforeEdit;

    Settings fSettings;
    int64_t fSettingsTime = 0;
    std::chrono::steady_clock::time_point fSettingsCheckedAt = std::chrono::steady_clock::now();

    LibraryScanner fScanner;
    std::vector<LibraryEntry> fNamLibrary, fIrLibrary;
    bool fScanning = false;
    bool fScanned = false;

    std::map<std::string, std::unique_ptr<NanoImage>> fImages; // by image path
    std::map<std::string, std::string> fSidecars;             // block file path -> its image path

    bool fInClipLatched = false;
    bool fOutClipLatched = false;

    uint32_t fInstanceId = 0;
    bool fGotChainState = false;

    std::chrono::steady_clock::time_point fChainChangedAt = std::chrono::steady_clock::now();
    bool fStatusPending = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinUI)
};

UI* createUI()
{
    return new NAMsayinUI();
}

END_NAMESPACE_DISTRHO
