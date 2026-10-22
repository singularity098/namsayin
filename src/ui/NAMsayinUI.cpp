// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"

#include "OpenUrl.hpp"
#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/blocks/CompressorBlock.hpp"
#include "engine/blocks/DelayBlock.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/LooperBlock.hpp"
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
#include <utility>
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
const Color pedalDrive(0xc8, 0x44, 0x3c);
const Color pedalModulation(0x8e, 0x5c, 0xd0);
const Color pedalEq(0x9a, 0xa0, 0xa8);
const Color pedalPitch(0xd0, 0xb0, 0x40);
const Color pedalFilter(0x5c, 0xa0, 0x3a);
const Color pedalOther(0x70, 0x70, 0x7a);
} // namespace theme

constexpr float kTopBarHeight = 48.0f;
constexpr float kBottomBarHeight = 132.0f;
constexpr float kTileSize = 120.0f;
constexpr float kSplitTileSize = 100.0f; // tiles are smaller with a split, to fit two rows
constexpr float kTileGap = 16.0f;
constexpr float kRowGap = 14.0f;
constexpr float kForkWidth = 28.0f;
constexpr float kMergeWidth = 64.0f;
constexpr float kChainTop = kTopBarHeight + 24.0f;
constexpr float kKnobRadius = 22.0f;
// Block panel: one cell per control, right-justified next to the block image
constexpr float kCellWidth = 96.0f;
constexpr float kCellHeight = 116.0f;
constexpr float kPanelMargin = 16.0f;
constexpr uint kDoubleClickMs = 350;
constexpr auto kTunerHold = std::chrono::milliseconds(1000); // the tuner keeps showing the last note this long
constexpr float kTunerInTuneCents = 2.0f;

// How long the audio engine gets to start a changed chain before the UI reports blocks that aren't running
constexpr auto kStatusGrace = std::chrono::milliseconds(2500);

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum class Hit {
    Knob,         // index = host parameter
    Toggle,       // index = host parameter (0/1 parameters)
    Momentary,    // index = host parameter: 1 while the mouse is held, then 0
    Tile,         // index = position in the chain
    TileBypass,
    TileRemove,
    AddBlock,     // index = Section
    Split,        // add a split
    Merge,        // select the merge
    MergeRemove,  // remove the split
    SetValue,     // index = host parameter, set to `value` (real units)
    Step,         // index = host parameter, add `value` (real units), clamped
    Slider,       // index = host parameter; horizontal drag over the rect
    MetroPanel,   // open/close the metronome panel
    MetroAccent,  // index = beat: cycle accent / normal / soft / mute
    MetroGrouping, // next beat grouping
    MetroResetAccents,
    Tap,          // tap tempo
    Fretbored,    // open the Fretbored web app
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
    float value = 0.0f;
};

// Where a dragged block can land: position `position` of `section`; `x` is where the block's tile would start
struct DropPoint {
    Section section;
    int position;
    float x;
    int row;
};

// Positions of everything in the chain view, worked out before drawing and kept for drag and drop.
// With a split, branch A and everything outside the split are on row 0, branch B on row 1.
struct ChainLayout {
    float tile = kTileSize;
    float rowY[2] {};
    float height = kTileSize;
    std::vector<Rect> tiles;          // per block index
    Rect add[4];                      // "+" per Section; w == 0 if there's none
    Rect split;                       // "Split" button (no split yet)
    Rect merge;                       // merge tile (split)
    float forkX = 0.0f;               // where the chain splits
    std::vector<Rect> wires[2];       // items on each row in order, joined by signal-flow lines
    std::vector<DropPoint> drops;
};

constexpr const char* kFretboredUrl = "https://singularity098.github.io/fretbored/";
constexpr double kTapTimeout = 2.0; // seconds between taps before tap tempo starts over
constexpr int kNoteValues[4] = { 2, 4, 8, 16 };

// Beat groupings, as in Fretbored. A grouping is stored as a bit mask of the beats that start a group.
std::vector<int> groupsFromMask(int beats, uint32_t mask)
{
    std::vector<int> groups;
    for (int i = 0; i < beats; ++i)
    {
        if (i == 0 || (mask >> i) & 1u)
            groups.push_back(0);
        ++groups.back();
    }
    return groups;
}

uint32_t maskFromGroups(const std::vector<int>& groups)
{
    uint32_t mask = 0;
    int beat = 0;
    for (const int g : groups)
    {
        mask |= 1u << beat;
        beat += g;
    }
    return mask;
}

// Fretbored's default: groups of 3, ending in 2+2 or 2 (7/8 is 2+2+3, 5/8 is 3+2); none below 5 beats or for
// quarter and half notes
std::vector<int> defaultGroups(int beats, int noteValue)
{
    if (noteValue < 8 || beats < 5)
        return {};
    if (beats % 3 == 0)
        return std::vector<int>(beats / 3, 3);
    if (beats == 5)
        return { 3, 2 };
    if (beats == 7)
        return { 2, 2, 3 };
    std::vector<int> groups;
    int left = beats;
    while (left > 4)
    {
        groups.push_back(3);
        left -= 3;
    }
    if (left == 4)
    {
        groups.push_back(2);
        groups.push_back(2);
    }
    else if (left > 0)
    {
        groups.push_back(left);
    }
    return groups;
}

// Every way to split `beats` into groups of 2 and 3, for the Grouping button to cycle through
void groupings(int beats, std::vector<int>& current, std::vector<std::vector<int>>& out)
{
    if (beats == 0)
    {
        out.push_back(current);
        return;
    }
    for (const int g : { 2, 3 })
    {
        if (g <= beats)
        {
            current.push_back(g);
            groupings(beats - g, current, out);
            current.pop_back();
        }
    }
}

std::string groupingText(const std::vector<int>& groups)
{
    std::string text;
    for (const int g : groups)
        text += (text.empty() ? "" : "+") + std::to_string(g);
    return text.empty() ? "None" : text;
}

const char* tempoMarking(double bpm)
{
    static const std::pair<double, const char*> marks[] = { { 40, "Grave" }, { 60, "Largo" }, { 66, "Larghetto" },
        { 76, "Adagio" }, { 108, "Andante" }, { 120, "Moderato" }, { 156, "Allegro" }, { 176, "Vivace" },
        { 200, "Presto" } };
    for (const auto& [limit, name] : marks)
    {
        if (bpm < limit)
            return name;
    }
    return "Prestissimo";
}

const char* const kNoteNames[12] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };

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
        if (index == kParamTunerFrequency && value > 0.0f)
            tunerReading(value);
        if (index == kParamMetroBeatCount)
            fBeatAt = std::chrono::steady_clock::now();
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
        if (fMetroPanel)
        {
            drawMetronome();
        }
        else if (fParams[kParamTuner] >= 0.5f)
        {
            drawTuner();
        }
        else
        {
            drawChain();
            drawSelectedPanel();
        }
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
                return onRelease(x, y);
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

        case Hit::Momentary:
            // a footswitch press: no undo step, and back to 0 on release
            if (fMomentaryReleasePending)
                releaseMomentary();
            fMomentaryParam = hit->index;
            fMomentaryPressedAt = std::chrono::steady_clock::now();
            fMomentaryReleasePending = false;
            setNormalized(static_cast<uint32_t>(hit->index), 1.0f, true);
            return true;

        case Hit::Toggle:
            if (isSlot(hit->index))
                beginEdit();
            setNormalized(hit->index, normalized(hit->index) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(hit->index);
            return true;

        case Hit::Tile:
            fSelected = hit->index;
            fMergeSelected = false;
            fPressTile = hit->index;
            fPressX = x;
            fPressY = y;
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

        case Hit::Split: {
            beginEdit();
            // around the selected block if there is one (it goes into branch A), otherwise at the end
            const int count = static_cast<int>(fChain.blocks.size());
            if (fSelected >= 0 && fSelected < count)
                fChain.addSplit(fSelected, fSelected + 1);
            else
                fChain.addSplit(count, count);
            for (uint32_t index = kParamMergeBalance; index <= kParamMergeInvertB; ++index)
                setHostParameter(index, globalParamSpec(index).def);
            fMergeSelected = true;
            sendChain();
            return true;
        }

        case Hit::Merge:
            fMergeSelected = true;
            repaint();
            return true;

        case Hit::MergeRemove:
            beginEdit();
            fChain.removeSplit();
            fMergeSelected = false;
            sendChain();
            return true;

        case Hit::AddBlock:
            fAddSection = static_cast<Section>(hit->index);
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
            sendChain(); // the DSP reads the setting when it builds the chain
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

        case Hit::SetValue:
            setValue(static_cast<uint32_t>(hit->index), hit->value);
            return true;

        case Hit::Slider:
            fDragParam = hit->index;
            fSliderRect = hit->rect;
            editParameter(static_cast<uint32_t>(hit->index), true);
            setNormalized(static_cast<uint32_t>(hit->index), std::clamp((x - hit->rect.x) / hit->rect.w, 0.0f, 1.0f),
                          false);
            return true;

        case Hit::MetroPanel:
            fMetroPanel = !fMetroPanel;
            repaint();
            return true;

        case Hit::MetroAccent: {
            const uint32_t index = kParamMetroAccent1 + static_cast<uint32_t>(hit->index);
            setValue(index, static_cast<float>((static_cast<int>(fParams[index]) + 3) % 4)); // 3 > 2 > 1 > 0 > 3
            return true;
        }

        case Hit::MetroGrouping:
            nextGrouping();
            return true;

        case Hit::MetroResetAccents:
            applyDefaultAccents();
            return true;

        case Hit::Tap:
            tap();
            return true;

        case Hit::Fretbored:
            openUrl(kFretboredUrl);
            return true;

        case Hit::Step:
            setValue(static_cast<uint32_t>(hit->index), fParams[hit->index] + hit->value);
            return true;

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

        if (fDragParam >= 0 && fSliderRect.w > 0.0f)
        {
            setNormalized(static_cast<uint32_t>(fDragParam), std::clamp((x - fSliderRect.x) / fSliderRect.w, 0.0f, 1.0f),
                          false);
            return true;
        }
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

        if (fPressTile >= 0 && (std::fabs(x - fPressX) > 6.0f || std::fabs(y - fPressY) > 6.0f))
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
        return fDialog == Dialog::None && metronomeKey(ev);
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
        if (y < kChainTop || y > kChainTop + fLayout.height)
            return false;
        const float delta = static_cast<float>(ev.delta.getY() != 0.0 ? ev.delta.getY() : ev.delta.getX());
        fScrollX = std::max(0.0f, fScrollX - delta * 40.0f);
        repaint();
        return true;
    }

    // Metronome shortcuts, while its panel is open (and so only while this window has the keyboard focus):
    // Space start/stop, Up/Down tempo (Shift: 5 BPM), T tap
    bool metronomeKey(const KeyboardEvent& ev)
    {
        if (!ev.press || !fMetroPanel)
            return false;
        const float step = (ev.mod & kModifierShift) ? 5.0f : 1.0f;
        switch (ev.key)
        {
        case kKeySpace:
            setValue(kParamMetronome, fParams[kParamMetronome] >= 0.5f ? 0.0f : 1.0f);
            return true;
        case kKeyUp:
        case kKeyRight:
            setValue(kParamMetroTempo, std::round(fParams[kParamMetroTempo]) + step);
            return true;
        case kKeyDown:
        case kKeyLeft:
            setValue(kParamMetroTempo, std::round(fParams[kParamMetroTempo]) - step);
            return true;
        case 't':
        case 'T':
            tap();
            return true;
        }
        return false;
    }

    void uiIdle() override
    {
        if (fMomentaryReleasePending && std::chrono::steady_clock::now() - fMomentaryPressedAt >= kMomentaryMinHold)
            releaseMomentary();
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

        // the metronome's pulse and sweep
        if (fParams[kParamMetronome] >= 0.5f || fMetroPanel)
            repaint();

        // the tuner animates (needle easing, strobe)
        const float dt = std::min(0.1f, std::chrono::duration<float>(now - fLastIdle).count());
        fLastIdle = now;
        if (fParams[kParamTuner] >= 0.5f)
        {
            const float target = tunerActive() ? fTunerCents : 0.0f;
            fNeedleCents += (target - fNeedleCents) * std::min(1.0f, dt * 12.0f);
            for (int row = 0; row < 3; ++row)
                fStrobePhase[row] += target * 5.0f * static_cast<float>(1 << row) * dt;
            repaint();
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

    // Sets a global parameter in its real units, as one gesture
    void setValue(uint32_t index, float value)
    {
        const GlobalParamSpec& spec = globalParamSpec(index);
        if (spec.flags & (kParamFlagInteger | kParamFlagToggle))
            value = std::round(value);
        setNormalized(index, (std::clamp(value, spec.min, spec.max) - spec.min) / (spec.max - spec.min), true);
        if (index == kParamMetroBeats || index == kParamMetroNoteValue)
            applyDefaultAccents();
    }

    int metroBeats() const { return std::clamp(static_cast<int>(fParams[kParamMetroBeats]), 1, 16); }
    int metroNoteValue() const { return kNoteValues[std::clamp(static_cast<int>(fParams[kParamMetroNoteValue]), 0, 3)]; }

    // Accents follow the grouping: the first beat accented, the first of every other group normal, the rest soft.
    // Without a grouping: the first beat accented, the rest normal.
    void applyGrouping(const std::vector<int>& groups)
    {
        const int beats = metroBeats();
        std::vector<int> levels(16, 2);
        levels[0] = 3;
        int sum = 0;
        for (const int g : groups)
            sum += g;
        if (!groups.empty() && sum == beats)
        {
            int beat = 0;
            for (size_t gi = 0; gi < groups.size(); ++gi)
            {
                for (int k = 0; k < groups[gi]; ++k)
                    levels[beat + k] = k == 0 ? (gi == 0 ? 3 : 2) : 1;
                beat += groups[gi];
            }
        }
        setValue(kParamMetroGrouping, static_cast<float>(groups.empty() || sum != beats ? 0 : maskFromGroups(groups)));
        for (int i = 0; i < 16; ++i)
            setValue(kParamMetroAccent1 + static_cast<uint32_t>(i), static_cast<float>(levels[i]));
    }

    void applyDefaultAccents() { applyGrouping(defaultGroups(metroBeats(), metroNoteValue())); }

    std::vector<int> currentGroups() const
    {
        const auto mask = static_cast<uint32_t>(fParams[kParamMetroGrouping]);
        return mask == 0 ? std::vector<int> {} : groupsFromMask(metroBeats(), mask);
    }

    // Cycles through no grouping and every split of the bar into 2s and 3s
    void nextGrouping()
    {
        std::vector<std::vector<int>> all { {} };
        std::vector<int> current;
        groupings(metroBeats(), current, all);
        const std::vector<int> groups = currentGroups();
        size_t i = 0;
        while (i < all.size() && all[i] != groups)
            ++i;
        applyGrouping(all[(i + 1) % all.size()]);
    }

    void tap()
    {
        const auto now = std::chrono::steady_clock::now();
        if (!fTaps.empty() && std::chrono::duration<double>(now - fTaps.back()).count() > kTapTimeout)
            fTaps.clear();
        fTaps.push_back(now);
        if (fTaps.size() > 8)
            fTaps.erase(fTaps.begin());
        fTapAt = now;
        if (fTaps.size() >= 2)
        {
            const double average =
                std::chrono::duration<double>(fTaps.back() - fTaps.front()).count() / (fTaps.size() - 1);
            setValue(kParamMetroTempo, static_cast<float>(std::round(60.0 / average)));
        }
        repaint();
    }

    // A new frequency from the tuner. Readings are median-filtered; a clear change of note starts over.
    void tunerReading(float frequency)
    {
        if (fTunerHistory.size() > 0)
        {
            std::vector<float> sorted = fTunerHistory;
            std::sort(sorted.begin(), sorted.end());
            if (std::fabs(1200.0f * std::log2(frequency / sorted[sorted.size() / 2])) > 60.0f)
                fTunerHistory.clear();
        }
        fTunerHistory.push_back(frequency);
        if (fTunerHistory.size() > 5)
            fTunerHistory.erase(fTunerHistory.begin());
        std::vector<float> sorted = fTunerHistory;
        std::sort(sorted.begin(), sorted.end());
        fTunerFrequency = sorted[sorted.size() / 2];

        const float midi = 69.0f + 12.0f * std::log2(fTunerFrequency / fParams[kParamTunerReference]);
        const int note = static_cast<int>(std::lround(midi));
        const float cents = (midi - note) * 100.0f;
        if (note != fTunerNote || !tunerActive())
            fTunerCents = cents;
        else
            fTunerCents += (cents - fTunerCents) * 0.35f;
        fTunerNote = note;
        fTunerSeenAt = std::chrono::steady_clock::now();
    }

    bool tunerActive() const
    {
        return fTunerNote >= 0 && std::chrono::steady_clock::now() - fTunerSeenAt < kTunerHold;
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
            const BlockDescriptor* type = block ? BlockRegistry::builtin().find(block->typeId) : nullptr;
            if (type && type->syncedParam == param && fParams[slotParam(slot, type->syncParam)] >= 0.5f)
                return type->syncedIsRate ? rateNoteName(rateNoteIndex(fParams[index])) : noteName(timeNoteIndex(fParams[index]));
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
        const float v = fParams[index];
        if (index == kParamMergeBalance)
        {
            // how far the other branch is turned down
            if (std::fabs(v) < 0.5f)
                return "A = B";
            std::snprintf(buf, sizeof(buf), v < 0 ? "B -%.0f%%" : "A -%.0f%%", std::fabs(v));
            return buf;
        }
        if (index == kParamMergePanA || index == kParamMergePanB)
        {
            if (std::fabs(v) < 0.5f)
                return "Center";
            std::snprintf(buf, sizeof(buf), v < 0 ? "L %.0f" : "R %.0f", std::fabs(v));
            return buf;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        std::snprintf(buf, sizeof(buf), "%+.1f %s", v, spec.unit);
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
        fChain.remove(index);
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
            // values a preset doesn't have (older presets have no merge settings) go back to their defaults
            const GlobalParamSpec& spec = globalParamSpec(index);
            const auto it = loaded->preset.globals.find(spec.symbol);
            setHostParameter(index, it != loaded->preset.globals.end() ? it->second : spec.def);
        }
        fSelected = loaded->preset.chain.blocks.empty() ? -1 : 0;
        fMergeSelected = false;
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
        fMergeSelected = false;
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

        if (hit->kind == Hit::Knob || hit->kind == Hit::Toggle || hit->kind == Hit::Momentary
            || hit->kind == Hit::Choice)
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

        fChain.insert(fAddSection, kMaxBlocks, std::move(block));
        fSelected = indexOfSlot(slot);
        fMergeSelected = false;
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

    void releaseMomentary()
    {
        setNormalized(static_cast<uint32_t>(fMomentaryParam), 0.0f, true);
        fMomentaryParam = -1;
        fMomentaryReleasePending = false;
    }

    bool onRelease(float x, float y)
    {
        if (fMomentaryParam >= 0)
        {
            // a quick click is held a moment longer (uiIdle releases it), so the DSP sees the press even if the
            // release would land in the same audio buffer
            if (std::chrono::steady_clock::now() - fMomentaryPressedAt < kMomentaryMinHold)
            {
                fMomentaryReleasePending = true;
                return true;
            }
            releaseMomentary();
            return true;
        }
        if (fDragParam >= 0)
        {
            editParameter(static_cast<uint32_t>(fDragParam), false);
            fDragParam = -1;
            fSliderRect = Rect {};
            return true;
        }
        if (fPressTile >= 0)
        {
            if (fDraggingTile)
            {
                if (const DropPoint* drop = dropPointAt(x, y))
                {
                    const int slot = fChain.blocks[fPressTile].slot;
                    const ChainState before = fChain;
                    fChain.move(fPressTile, drop->section, drop->position);
                    fSelected = indexOfSlot(slot);
                    if (!(fChain == before))
                    {
                        fUndo.push(before);
                        sendChain();
                    }
                }
            }
            fPressTile = -1;
            fDraggingTile = false;
            repaint();
            return true;
        }
        return false;
    }

    int indexOfSlot(int slot) const
    {
        for (int i = 0; i < static_cast<int>(fChain.blocks.size()); ++i)
        {
            if (fChain.blocks[i].slot == slot)
                return i;
        }
        return -1;
    }

    // The drop point nearest to a tile dragged with the mouse at (x, y)
    const DropPoint* dropPointAt(float x, float y) const
    {
        const int row = fChain.split && y >= fLayout.rowY[1] - kRowGap / 2 ? 1 : 0;
        const float left = x - fLayout.tile / 2;
        const DropPoint* best = nullptr;
        for (const DropPoint& drop : fLayout.drops)
        {
            if (drop.row == row && (!best || std::fabs(drop.x - left) < std::fabs(best->x - left)))
                best = &drop;
        }
        return best;
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

    // The looper's state, position in the loop and length
    void drawLooperStatus(float cx, float cy, float value)
    {
        const LooperStatus status = LooperStatus::decode(value);
        static constexpr const char* names[] = { "Empty", "Recording", "Playing", "Overdub", "Stopped" };
        const Color colors[] = { theme::textDim, theme::bad, theme::good, theme::warn, theme::textDim };
        const int state = static_cast<int>(status.state);
        label(cx, cy - 18.0f, names[state], colors[state], 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        const float w = 84.0f;
        fillRect(cx - w / 2, cy - 4.0f, w, 8.0f, theme::inset, 3.0f);
        if (status.state == LooperState::Playing || status.state == LooperState::Overdubbing)
            fillRect(cx - w / 2, cy - 4.0f, w * (status.progress + 1) / 100.0f, 8.0f, colors[state], 3.0f);
        char text[24];
        std::snprintf(text, sizeof(text), "%.1f s", status.lengthTenths / 10.0);
        label(cx, cy + 18.0f, text, theme::text, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
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
        switch (type->category)
        {
        case Category::Delay:      color = theme::pedalDelay; break;
        case Category::Reverb:     color = theme::pedalReverb; break;
        case Category::Dynamics:   color = theme::pedalDynamics; break;
        case Category::Distortion: color = theme::pedalDrive; break;
        case Category::Modulation: color = theme::pedalModulation; break;
        case Category::Eq:         color = theme::pedalEq; break;
        case Category::PitchSynth: color = theme::pedalPitch; break;
        case Category::Filter:
        case Category::Wah:        color = theme::pedalFilter; break;
        default:                   break;
        }

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
        drawButton({ fWidth - 188.0f, 10.0f, 84.0f, 28.0f }, "Settings", fDialog == Dialog::Settings, theme::accent,
                   Hit::OpenSettings, 0);
        drawButton({ fWidth - 276.0f, 10.0f, 80.0f, 28.0f }, "Tuner", fParams[kParamTuner] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamTuner);

        // Metronome: on/off, flashing on every beat, and ▾ for its panel
        const bool running = fParams[kParamMetronome] >= 0.5f;
        const Rect metro { fWidth - 400.0f, 10.0f, 88.0f, 28.0f };
        drawButton(metro, "Metronome", running, theme::accent, Hit::Toggle, kParamMetronome);
        const float pulse = beatPulse();
        if (running && pulse > 0.0f)
            fillRect(metro.x, metro.y, metro.w, metro.h, Color(255, 255, 255, 0.35f * pulse), 6.0f);
        drawButton({ metro.x + metro.w + 2.0f, 10.0f, 26.0f, 28.0f }, "▾", fMetroPanel, theme::accent, Hit::MetroPanel,
                   0);

        // only when there's room next to the preset and undo controls
        if (fLearnParam < 0 && fWidth >= 1010.0f)
            drawButton({ fWidth - 504.0f, 10.0f, 92.0f, 28.0f }, "Fretbored ↗", false, theme::accent, Hit::Fretbored,
                       0);
    }

    // 1 right after a beat, fading to 0 over 150 ms
    float beatPulse() const
    {
        if (fParams[kParamMetroBeat] < 0.0f || fParams[kParamMetroSilent] >= 0.5f)
            return 0.0f;
        const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - fBeatAt).count();
        return std::clamp(1.0f - since / 0.15f, 0.0f, 1.0f);
    }

    void drawSlider(const Rect& r, uint32_t index, bool enabled = true)
    {
        const float n = std::clamp(normalized(index), 0.0f, 1.0f);
        globalAlpha(enabled ? 1.0f : 0.4f);
        fillRect(r.x, r.y + r.h / 2 - 2.0f, r.w, 4.0f, theme::inset, 2.0f);
        fillRect(r.x, r.y + r.h / 2 - 2.0f, r.w * n, 4.0f, theme::accent, 2.0f);
        beginPath();
        circle(r.x + r.w * n, r.y + r.h / 2, 7.0f);
        fillColor(theme::text);
        fill();
        globalAlpha(1.0f);
        // the hit rect maps x to the value, so it's exactly the track
        if (enabled)
            fHits.push_back({ r, Hit::Slider, static_cast<int>(index) });
    }

    // [−] value [+]
    void drawStepper(float x, float y, float w, const char* text, uint32_t index, float step)
    {
        drawValueButton({ x, y, 24.0f, 24.0f }, "−", false, Hit::Step, index, -step);
        label(x + w / 2, y + 12.0f, text, theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawValueButton({ x + w - 24.0f, y, 24.0f, 24.0f }, "+", false, Hit::Step, index, step);
    }

    void sectionTitle(float x, float y, const char* text)
    {
        label(x, y, text, theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawMetronome()
    {
        const Rect panel { 16.0f, kTopBarHeight + 16.0f, fWidth - 32.0f,
                           fHeight - kBottomBarHeight - kTopBarHeight - 32.0f };
        fillRect(panel.x, panel.y, panel.w, panel.h, theme::panel, 10.0f);
        label(panel.x + 16.0f, panel.y + 30.0f, "Metronome", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawValueButton({ panel.x + panel.w - 30.0f, panel.y + 6.0f, 24.0f, 24.0f }, "×", false, Hit::MetroPanel, 0, 0);

        const float gap = 20.0f;
        const float mainWidth = std::clamp(panel.w * 0.36f, 300.0f, 420.0f);
        const float colWidth = (panel.w - mainWidth - 4 * gap) / 2.0f;
        drawMetronomeMain({ panel.x + gap, panel.y + 52.0f, mainWidth, panel.h - 64.0f });
        drawMetronomeMeter({ panel.x + 2 * gap + mainWidth, panel.y + 16.0f, colWidth, panel.h - 28.0f });
        drawMetronomeTrainers({ panel.x + 3 * gap + mainWidth + colWidth, panel.y + 52.0f, colWidth,
                                panel.h - 64.0f });
    }

    void drawMetronomeMain(const Rect& r)
    {
        const bool running = fParams[kParamMetronome] >= 0.5f;
        const bool sync = fParams[kParamMetroSync] >= 0.5f;
        const bool trainer = fParams[kParamMetroTrainer] >= 0.5f;
        const bool live = running && fParams[kParamMetroBeat] >= 0.0f;
        const float cx = r.x + r.w / 2;

        // tempo: what's being played if the trainer or the DAW sets it
        float shown = fParams[kParamMetroTempo];
        if ((trainer || sync) && live)
            shown = fParams[kParamMetroLiveTempo];
        else if (trainer && !sync)
            shown = fParams[kParamMetroTrainerStart];
        char bpm[16];
        std::snprintf(bpm, sizeof(bpm), sync && !live ? "DAW" : "%.0f", shown);
        label(cx, r.y + 30.0f, bpm, theme::text, 56.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(cx, r.y + 64.0f, sync && !live ? "tempo" : (std::string("BPM · ") + tempoMarking(shown)).c_str(),
              theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        // beat dots: click to cycle accent / normal / soft / mute; the beat being heard lights up
        const int beats = metroBeats();
        const float dot = std::min(22.0f, (r.w - 8.0f) / beats - 4.0f);
        const float dotsWidth = beats * (dot + 4.0f) - 4.0f;
        const float dy = r.y + 96.0f;
        const int current = live ? static_cast<int>(fParams[kParamMetroBeat]) : -1;
        const float pulse = beatPulse();
        for (int i = 0; i < beats; ++i)
        {
            const float x = cx - dotsWidth / 2 + i * (dot + 4.0f);
            const int level = static_cast<int>(fParams[kParamMetroAccent1 + static_cast<uint32_t>(i)]);
            const float height = dot * (level == 3 ? 1.0f : level == 2 ? 0.72f : level == 1 ? 0.45f : 0.2f);
            const bool lit = i == current;
            fillRect(x, dy + dot - height, dot, height, lit ? theme::accent : theme::tile, 4.0f);
            if (lit && pulse > 0.0f)
                fillRect(x, dy + dot - height, dot, height, Color(255, 255, 255, 0.5f * pulse), 4.0f);
            fHits.push_back({ { x, dy - 4.0f, dot, dot + 8.0f }, Hit::MetroAccent, i });
        }
        label(cx, dy + dot + 12.0f, "Click a beat: accent · normal · soft · mute", theme::textDim, 10.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);

        // sweep: a ball swinging from side to side once per beat, eased like a pendulum
        const float sy = dy + dot + 32.0f;
        fillRect(r.x, sy - 1.0f, r.w, 2.0f, theme::border, 1.0f);
        if (live && fParams[kParamMetroLiveTempo] > 0.0f)
        {
            const float beatSeconds = 60.0f / fParams[kParamMetroLiveTempo];
            const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - fBeatAt).count();
            const float phase = std::clamp(since / beatSeconds, 0.0f, 1.0f);
            const float eased = (1.0f - std::cos(static_cast<float>(M_PI) * phase)) / 2.0f;
            const bool odd = static_cast<int>(fParams[kParamMetroBeatCount]) % 2;
            const float pos = odd ? eased : 1.0f - eased;
            beginPath();
            circle(r.x + 7.0f + pos * (r.w - 14.0f), sy, 7.0f);
            fillColor(fParams[kParamMetroSilent] >= 0.5f ? theme::border : theme::accent);
            fill();
        }

        // tempo controls (locked while the trainer or the DAW sets the tempo)
        const bool locked = trainer || sync;
        const float ty = sy + 22.0f;
        globalAlpha(locked ? 0.4f : 1.0f);
        if (!locked)
        {
            drawValueButton({ r.x, ty, 34.0f, 26.0f }, "−5", false, Hit::Step, kParamMetroTempo, -5.0f);
            drawValueButton({ r.x + 38.0f, ty, 30.0f, 26.0f }, "−1", false, Hit::Step, kParamMetroTempo, -1.0f);
            drawValueButton({ r.x + r.w - 68.0f, ty, 30.0f, 26.0f }, "+1", false, Hit::Step, kParamMetroTempo, 1.0f);
            drawValueButton({ r.x + r.w - 34.0f, ty, 34.0f, 26.0f }, "+5", false, Hit::Step, kParamMetroTempo, 5.0f);
        }
        globalAlpha(1.0f);
        drawSlider({ r.x + 76.0f, ty, r.w - 152.0f, 26.0f }, kParamMetroTempo, !locked);

        const float by = ty + 40.0f;
        const float tapFlash = std::clamp(1.0f - std::chrono::duration<float>(std::chrono::steady_clock::now() -
                                                                              fTapAt).count() / 0.15f, 0.0f, 1.0f);
        drawButton({ r.x, by, 80.0f, 32.0f }, "Tap", tapFlash > 0.0f, theme::accent, Hit::Tap, 0);
        drawButton({ r.x + 88.0f, by, r.w - 176.0f, 32.0f }, running ? "■  Stop" : "▶  Start", running, theme::accent,
                   Hit::Toggle, kParamMetronome);
        drawButton({ r.x + r.w - 80.0f, by, 80.0f, 32.0f }, "DAW sync", sync, theme::accent, Hit::Toggle,
                   kParamMetroSync);

        char status[96];
        if (!running)
            std::snprintf(status, sizeof(status), "Stopped");
        else if (!live)
            std::snprintf(status, sizeof(status), sync ? "Waiting for the DAW to play" : "Starting…");
        else
            std::snprintf(status, sizeof(status), "Bar %d · Beat %d%s", static_cast<int>(fParams[kParamMetroBar]) + 1,
                          current + 1, fParams[kParamMetroSilent] >= 0.5f ? " · (muted)" : "");
        label(cx, by + 50.0f, status, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        if (by + 74.0f < r.y + r.h)
            label(cx, by + 70.0f, "Space start/stop · ↑/↓ tempo (Shift ×5) · T tap", theme::textDim, 10.0f,
                  ALIGN_CENTER | ALIGN_MIDDLE);
    }

    void drawMetronomeMeter(const Rect& r)
    {
        float y = r.y + 14.0f;
        sectionTitle(r.x, y, "METER");
        y += 16.0f;
        char text[32];
        std::snprintf(text, sizeof(text), "%d beats", metroBeats());
        drawStepper(r.x, y, 100.0f, text, kParamMetroBeats, 1.0f);
        label(r.x + 107.0f, y + 12.0f, "/", theme::textDim, 14.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        for (int i = 0; i < 4; ++i)
        {
            std::snprintf(text, sizeof(text), "%d", kNoteValues[i]);
            drawValueButton({ r.x + 114.0f + i * 30.0f, y, 28.0f, 24.0f }, text,
                            static_cast<int>(fParams[kParamMetroNoteValue]) == i, Hit::SetValue, kParamMetroNoteValue,
                            static_cast<float>(i));
        }
        y += 32.0f;
        const std::string grouping = "Grouping: " + groupingText(currentGroups());
        drawButton({ r.x, y, std::min(150.0f, r.w / 2 - 4.0f), 24.0f }, grouping.c_str(), false, theme::accent,
                   Hit::MetroGrouping, 0);
        drawButton({ r.x + std::min(154.0f, r.w / 2), y, std::min(110.0f, r.w / 2 - 4.0f), 24.0f }, "Reset accents",
                   false, theme::accent, Hit::MetroResetAccents, 0);

        y += 42.0f;
        sectionTitle(r.x, y, "SUBDIVISION");
        y += 14.0f;
        static constexpr const char* subdivisions[] = { "Quarter", "Eighths", "Triplets", "16ths", "Quintuplets",
            "Sextuplets", "Swing", "Dotted", "8 + 16 16", "16 16 + 8", "16 8 16" };
        static constexpr const char* shortNames[] = { "1/4", "8ths", "Triplets", "16ths", "5-tuplets", "6-tuplets",
            "Swing", "Dotted", "8 16 16", "16 16 8", "16 8 16" };
        const float pill = (r.w - 3 * 4.0f) / 4.0f;
        for (int i = 0; i < 11; ++i)
        {
            drawValueButton({ r.x + (i % 4) * (pill + 4.0f), y + (i / 4) * 28.0f, pill, 24.0f },
                            pill >= 84.0f ? subdivisions[i] : shortNames[i],
                            static_cast<int>(fParams[kParamMetroSubdivision]) == i, Hit::SetValue,
                            kParamMetroSubdivision, static_cast<float>(i));
        }

        y += 3 * 28.0f + 18.0f;
        sectionTitle(r.x, y, "SOUND");
        y += 14.0f;
        static constexpr const char* sounds[] = { "Classic", "Digital", "Beep" };
        const float seg = (r.w - 2 * 4.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            drawValueButton({ r.x + i * (seg + 4.0f), y, seg, 24.0f }, sounds[i],
                            static_cast<int>(fParams[kParamMetroSound]) == i, Hit::SetValue, kParamMetroSound,
                            static_cast<float>(i));
        y += 34.0f;
        for (const uint32_t index : { kParamMetroVolume, kParamMetroSubVolume })
        {
            std::snprintf(text, sizeof(text), "%s %.0f%%", index == kParamMetroVolume ? "Volume" : "Subdivisions",
                          fParams[index]);
            label(r.x, y + 12.0f, text, theme::text, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            drawSlider({ r.x + 110.0f, y, r.w - 110.0f, 24.0f }, index);
            y += 28.0f;
        }
    }

    void drawMetronomeTrainers(const Rect& r)
    {
        float y = r.y - 22.0f;
        sectionTitle(r.x, y, "TEMPO TRAINER");
        y += 14.0f;
        static constexpr const char* modes[] = { "Off", "Step", "Gradual" };
        const int mode = static_cast<int>(fParams[kParamMetroTrainer]);
        const float seg = (r.w - 2 * 4.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            drawValueButton({ r.x + i * (seg + 4.0f), y, seg, 24.0f }, modes[i], mode == i, Hit::SetValue,
                            kParamMetroTrainer, static_cast<float>(i));
        y += 32.0f;

        char text[48];
        auto row = [&](const char* name, uint32_t index, const char* unit, float step) {
            label(r.x, y + 12.0f, name, theme::textDim, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            std::snprintf(text, sizeof(text), "%.0f %s", fParams[index], unit);
            drawStepper(r.x + r.w - 130.0f, y, 130.0f, text, index, step);
            y += 28.0f;
        };
        globalAlpha(mode == 0 ? 0.4f : 1.0f);
        row("Start", kParamMetroTrainerStart, "BPM", 1.0f);
        row("End", kParamMetroTrainerEnd, "BPM", 1.0f);
        if (mode == 2)
        {
            row("Over", kParamMetroTrainerBars, "bars", 1.0f);
        }
        else
        {
            row("Change by", kParamMetroTrainerStep, "BPM", 1.0f);
            row("Every", kParamMetroTrainerEvery, "bars", 1.0f);
        }
        globalAlpha(1.0f);
        if (mode != 0 && fParams[kParamMetroSync] >= 0.5f)
            label(r.x, y + 8.0f, "The trainer is off while the DAW sets the tempo", theme::warn, 11.0f,
                  ALIGN_LEFT | ALIGN_MIDDLE);

        y += 30.0f;
        sectionTitle(r.x, y, "GAP TRAINER");
        y += 14.0f;
        const bool gapOn = fParams[kParamMetroGap] >= 0.5f;
        drawButton({ r.x, y, 64.0f, 24.0f }, gapOn ? "On" : "Off", gapOn, theme::accent, Hit::Toggle, kParamMetroGap);
        label(r.x + 74.0f, y + 12.0f, "Mute the click for a few bars", theme::textDim, 11.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        y += 32.0f;
        globalAlpha(gapOn ? 1.0f : 0.4f);
        row("Play", kParamMetroGapPlay, "bars", 1.0f);
        row("Then mute", kParamMetroGapMute, "bars", 1.0f);
        globalAlpha(1.0f);
    }

    // Small button that sets a parameter to a value, or steps it
    void drawValueButton(const Rect& r, const char* str, bool on, Hit kind, uint32_t index, float value)
    {
        drawButton(r, str, on, theme::accent, kind, static_cast<int>(index));
        fHits.back().value = value;
    }

    void drawTuner()
    {
        const Rect panel { 16.0f, kTopBarHeight + 16.0f, fWidth - 32.0f,
                           fHeight - kBottomBarHeight - kTopBarHeight - 32.0f };
        fillRect(panel.x, panel.y, panel.w, panel.h, theme::panel, 10.0f);
        const bool active = tunerActive();
        const float cents = active ? fTunerCents : 0.0f;
        const bool inTune = active && std::fabs(cents) <= kTunerInTuneCents;
        const Color stateColor = !active ? theme::textDim : inTune ? theme::good
                                                          : std::fabs(cents) <= 10.0f ? theme::warn : theme::accent;

        // header: view, mute, reference pitch
        const float hy = panel.y + 16.0f;
        label(panel.x + 16.0f, hy + 14.0f, "Tuner", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const bool strobe = fParams[kParamTunerStrobe] >= 0.5f;
        drawValueButton({ panel.x + 96.0f, hy, 76.0f, 28.0f }, "Needle", !strobe, Hit::SetValue, kParamTunerStrobe, 0);
        drawValueButton({ panel.x + 176.0f, hy, 76.0f, 28.0f }, "Strobe", strobe, Hit::SetValue, kParamTunerStrobe, 1);
        drawButton({ panel.x + 272.0f, hy, 76.0f, 28.0f }, "Mute", fParams[kParamTunerMute] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamTunerMute);

        char ref[32];
        std::snprintf(ref, sizeof(ref), "A4 = %.0f Hz", fParams[kParamTunerReference]);
        const float rx = panel.x + panel.w - 16.0f;
        drawValueButton({ rx - 28.0f, hy, 28.0f, 28.0f }, "+", false, Hit::Step, kParamTunerReference, 1.0f);
        label(rx - 84.0f, hy + 14.0f, ref, theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        drawValueButton({ rx - 168.0f, hy, 28.0f, 28.0f }, "−", false, Hit::Step, kParamTunerReference, -1.0f);
        fHits.push_back({ { rx - 136.0f, hy, 104.0f, 28.0f }, Hit::Knob, static_cast<int>(kParamTunerReference) });

        // note, cents, frequency and guidance
        const float cx = panel.x + panel.w / 2;
        const float noteY = panel.y + 56.0f + (panel.h - 56.0f) * 0.28f;
        if (fTunerNote >= 0)
        {
            fontSize(84.0f);
            DGL_NAMESPACE::Rectangle<float> bounds;
            const char* name = kNoteNames[((fTunerNote % 12) + 12) % 12];
            textBounds(0, 0, name, nullptr, bounds);
            const float w = bounds.getWidth();
            label(cx - 10.0f, noteY, name, active ? stateColor : theme::textDim, 84.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            char octave[16];
            std::snprintf(octave, sizeof(octave), "%d", fTunerNote / 12 - 1);
            label(cx - 10.0f + w / 2 + 4.0f, noteY + 22.0f, octave, theme::textDim, 26.0f, ALIGN_LEFT | ALIGN_MIDDLE);

            char info[64];
            std::snprintf(info, sizeof(info), "%s%.1f ¢     %.2f Hz", cents >= 0 ? "+" : "−", std::fabs(cents),
                          fTunerFrequency);
            label(cx, noteY + 58.0f, info, theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        else
        {
            label(cx, noteY, "—", theme::textDim, 84.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        }
        const char* status = !active ? "Play a note" : inTune ? "In tune" : cents < 0 ? "Tune up ↑" : "Tune down ↓";
        label(cx, noteY + 82.0f, status, stateColor, 14.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        const float viewTop = noteY + 104.0f;
        const float viewHeight = panel.y + panel.h - 16.0f - viewTop;
        if (viewHeight < 40.0f)
            return;
        if (strobe)
            drawStrobe({ cx - 300.0f, viewTop, 600.0f, std::min(viewHeight, 120.0f) }, active, inTune);
        else
            drawNeedle(cx, viewTop, std::min(viewHeight, 140.0f), inTune);
    }

    // Needle gauge: ±50 cents over a 120° arc, green zone for "in tune"
    void drawNeedle(float cx, float top, float height, bool inTune)
    {
        const float radius = std::min(260.0f, height - 8.0f);
        const float cy = top + radius + 4.0f;
        constexpr float span = static_cast<float>(M_PI) / 3.0f; // each side
        const float up = -static_cast<float>(M_PI) / 2.0f;
        auto angleOf = [&](float c) { return up + std::clamp(c, -50.0f, 50.0f) / 50.0f * span; };

        beginPath();
        arc(cx, cy, radius, angleOf(-50), angleOf(50), CW);
        strokeColor(theme::border);
        strokeWidth(3.0f);
        stroke();
        beginPath();
        arc(cx, cy, radius, angleOf(-kTunerInTuneCents), angleOf(kTunerInTuneCents), CW);
        strokeColor(theme::good);
        strokeWidth(6.0f);
        stroke();

        for (int c = -50; c <= 50; c += 10)
        {
            const float a = angleOf(static_cast<float>(c));
            const float inner = radius - (c % 50 == 0 ? 16.0f : 9.0f);
            beginPath();
            moveTo(cx + std::cos(a) * inner, cy + std::sin(a) * inner);
            lineTo(cx + std::cos(a) * (radius - 3.0f), cy + std::sin(a) * (radius - 3.0f));
            strokeColor(theme::textDim);
            strokeWidth(1.5f);
            stroke();
            if (c % 50 == 0)
            {
                char text[8];
                std::snprintf(text, sizeof(text), c > 0 ? "+%d" : "%d", c);
                label(cx + std::cos(a) * (radius - 30.0f), cy + std::sin(a) * (radius - 30.0f), text,
                      theme::textDim, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
            }
        }

        const float a = angleOf(fNeedleCents);
        beginPath();
        moveTo(cx, cy);
        lineTo(cx + std::cos(a) * (radius + 6.0f), cy + std::sin(a) * (radius + 6.0f));
        strokeColor(inTune ? theme::good : theme::text);
        strokeWidth(2.5f);
        stroke();
        beginPath();
        circle(cx, cy, 5.0f);
        fillColor(theme::text);
        fill();
    }

    // Strobe: three rows of bands (the fundamental and two octaves, like a Peterson strobe). They drift at a
    // speed proportional to how far out of tune the note is, and stand still when it's in tune.
    void drawStrobe(const Rect& r, bool active, bool inTune)
    {
        fillRect(r.x, r.y, r.w, r.h, theme::inset, 8.0f);
        scissor(r.x, r.y, r.w, r.h);
        const float rowHeight = r.h / 3.0f;
        const Color color = !active ? theme::border : inTune ? theme::good : theme::accent;
        for (int row = 0; row < 3; ++row)
        {
            const float period = 56.0f / static_cast<float>(1 << row) + 12.0f;
            const float offset = std::fmod(std::fmod(fStrobePhase[row], period) + period, period);
            globalAlpha(0.95f - row * 0.18f);
            for (float x = r.x + offset - period; x < r.x + r.w; x += period)
                fillRect(x, r.y + row * rowHeight + 3.0f, period / 2, rowHeight - 6.0f, color);
        }
        globalAlpha(1.0f);
        resetScissor();
    }

    void layoutChain()
    {
        ChainLayout& l = fLayout;
        const bool split = fChain.split.has_value();
        l = ChainLayout {};
        l.tile = split ? kSplitTileSize : kTileSize;
        l.rowY[0] = kChainTop;
        l.rowY[1] = kChainTop + l.tile + kRowGap;
        l.height = split ? 2 * l.tile + kRowGap : l.tile;
        l.tiles.resize(fChain.blocks.size());

        const float step = l.tile + kTileGap;
        float x = 16.0f - fScrollX;

        // Lays out one section's tiles on a row from `x`, with a drop point before each and one at the end
        auto section = [&](Section s, int row, float x0) {
            const std::vector<int> indices = fChain.sectionIndices(s);
            float sx = x0;
            for (int k = 0; k < static_cast<int>(indices.size()); ++k)
            {
                l.drops.push_back({ s, k, sx, row });
                l.tiles[indices[k]] = { sx, l.rowY[row], l.tile, l.tile };
                l.wires[row].push_back(l.tiles[indices[k]]);
                sx += step;
            }
            l.drops.push_back({ s, static_cast<int>(indices.size()), sx, row });
            return sx;
        };
        auto addTile = [&](Section s, int row, float ax) {
            l.add[static_cast<int>(s)] = { ax, l.rowY[row], l.tile, l.tile };
            l.wires[row].push_back(l.add[static_cast<int>(s)]);
        };

        x = section(Section::Pre, 0, x);
        if (!split)
        {
            addTile(Section::Pre, 0, x);
            l.split = { x + step, l.rowY[0] + l.tile / 2 - 18.0f, 72.0f, 36.0f };
            return;
        }

        l.forkX = x + kForkWidth / 2 - kTileGap / 2;
        const Rect fork { l.forkX, l.rowY[0], 0.0f, l.tile };
        l.wires[0].push_back(fork);
        l.wires[1].push_back({ l.forkX, l.rowY[1], 0.0f, l.tile });
        x += kForkWidth;

        const float endA = section(Section::A, 0, x);
        const float endB = section(Section::B, 1, x);
        addTile(Section::A, 0, endA);
        addTile(Section::B, 1, endB);
        x = std::max(endA, endB) + step;

        l.merge = { x, l.rowY[0], kMergeWidth, l.height };
        l.wires[0].push_back({ x, l.rowY[0], kMergeWidth, l.tile });
        l.wires[1].push_back({ x, l.rowY[1], kMergeWidth, l.tile });
        x += kMergeWidth + kTileGap;

        x = section(Section::Post, 0, x);
        addTile(Section::Post, 0, x);
    }

    void drawWire(float x0, float x1, float y)
    {
        if (x1 - x0 < 4.0f)
            return;
        beginPath();
        moveTo(x0 + 3.0f, y);
        lineTo(x1 - 3.0f, y);
        strokeColor(theme::border);
        strokeWidth(2.0f);
        stroke();
    }

    void drawChain()
    {
        layoutChain();
        const ChainLayout& l = fLayout;
        const int count = static_cast<int>(fChain.blocks.size());

        scissor(0, kTopBarHeight, fWidth, l.height + 48.0f);

        // signal-flow lines between neighbours on each row, and the fork down to branch B
        for (int row = 0; row < 2; ++row)
        {
            for (size_t i = 1; i < l.wires[row].size(); ++i)
                drawWire(l.wires[row][i - 1].x + l.wires[row][i - 1].w, l.wires[row][i].x,
                         l.rowY[row] + l.tile / 2);
        }
        if (fChain.split)
        {
            beginPath();
            moveTo(l.forkX, l.rowY[0] + l.tile / 2);
            lineTo(l.forkX, l.rowY[1] + l.tile / 2);
            strokeColor(theme::border);
            strokeWidth(2.0f);
            stroke();
            beginPath();
            circle(l.forkX, l.rowY[0] + l.tile / 2, 4.0f);
            fillColor(theme::accent);
            fill();
            drawMergeTile();
        }

        for (int i = 0; i < count; ++i)
        {
            if (fDraggingTile && i == fPressTile)
                continue; // drawn last, under the mouse
            drawTile(i, l.tiles[i].x, l.tiles[i].y, l.tile);
        }
        for (int s = 0; s < 4; ++s)
        {
            if (l.add[s].w > 0.0f)
                drawAddTile(l.add[s], static_cast<Section>(s));
        }
        if (!fChain.split)
            drawSplitButton(l.split);

        if (fDraggingTile && fPressTile >= 0 && fPressTile < count)
        {
            if (const DropPoint* drop = dropPointAt(fMouseX, fMouseY))
            {
                const float dx = drop->x - kTileGap / 2;
                fillRect(dx - 1.5f, l.rowY[drop->row] - 4.0f, 3.0f, l.tile + 8.0f, theme::accent, 1.5f);
            }
            drawTile(fPressTile, fMouseX - l.tile / 2, fMouseY - l.tile / 2, l.tile);
        }
        resetScissor();

        if (count == 0 && !fChain.split)
            label(l.split.x + l.split.w + 24.0f, l.rowY[0] + kTileSize / 2, "Click + to add a block",
                  theme::textDim, 14.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        bool anyRunning = false;
        for (const BlockState& block : fChain.blocks)
            anyRunning |= slotRunning(block.slot);
        if (count > 0 && !anyRunning && statusSettled())
            label(16.0f, kChainTop + l.height + 14.0f,
                  "The audio engine hasn't started this chain. The host may not pass the plugin's UI messages "
                  "to it (some LV2 hosts don't); try the VST3 version.",
                  theme::bad, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawAddTile(const Rect& r, Section section)
    {
        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 10.0f);
        strokeColor(theme::border);
        strokeWidth(2.0f);
        stroke();
        label(r.x + r.w / 2, r.y + r.h / 2 - 2, "+", theme::textDim, r.w * 0.37f, ALIGN_CENTER | ALIGN_MIDDLE);
        if (fChain.blocks.size() < static_cast<size_t>(kMaxBlocks))
            fHits.push_back({ r, Hit::AddBlock, static_cast<int>(section) });
    }

    void drawSplitButton(const Rect& r)
    {
        const bool hover = r.contains(fMouseX, fMouseY);
        fillRect(r.x, r.y, r.w, r.h, hover ? theme::tileHover : theme::tile, 8.0f);
        // fork icon
        const float ix = r.x + 14.0f, iy = r.y + r.h / 2;
        beginPath();
        moveTo(ix - 4.0f, iy);
        lineTo(ix + 2.0f, iy);
        lineTo(ix + 8.0f, iy - 6.0f);
        moveTo(ix + 2.0f, iy);
        lineTo(ix + 8.0f, iy + 6.0f);
        strokeColor(theme::text);
        strokeWidth(1.5f);
        stroke();
        label(r.x + 44.0f, iy, "Split", theme::text, 13.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ r, Hit::Split, 0 });
    }

    void drawMergeTile()
    {
        const Rect& r = fLayout.merge;
        const bool hover = r.contains(fMouseX, fMouseY);
        fillRect(r.x, r.y, r.w, r.h, hover ? theme::tileHover : theme::tile, 10.0f);
        if (fMergeSelected)
        {
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 10.0f);
            strokeColor(theme::accent);
            strokeWidth(2.0f);
            stroke();
        }
        const float rowA = fLayout.rowY[0] + fLayout.tile / 2;
        const float rowB = fLayout.rowY[1] + fLayout.tile / 2;
        label(r.x + r.w / 2, rowA, "A", theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(r.x + r.w / 2, rowB, "B", theme::text, 15.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(r.x + r.w / 2, (rowA + rowB) / 2, "MERGE", theme::textDim, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ r, Hit::Merge, 0 });

        const Rect remove { r.x + r.w - 22, r.y + 4, 18, 18 };
        label(remove.x + 9, remove.y + 9, "×", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        fHits.push_back({ remove, Hit::MergeRemove, 0 });
    }

    // Draws block `i` as a `size` square at (x, y). The tile is designed at kTileSize and scaled.
    void drawTile(int i, float x, float y, float size)
    {
        const float k = size / kTileSize;
        auto hitRect = [&](float rx, float ry, float rw, float rh) {
            return Rect { x + rx * k, y + ry * k, rw * k, rh * k };
        };

        const BlockState& block = fChain.blocks[i];
        const std::string path = block.config.value("path", std::string());
        const bool missing = !path.empty() && !fileExists(path);
        const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
        const bool notRunning = !missing && statusSettled() && !slotRunning(block.slot);
        const bool hover = Rect { x, y, size, size }.contains(fMouseX, fMouseY);

        save();
        translate(x, y);
        this->scale(k, k);

        fillRect(0, 0, kTileSize, kTileSize, hover ? theme::tileHover : theme::tile, 10.0f);
        if (i == fSelected && !fMergeSelected)
        {
            beginPath();
            roundedRect(0, 0, kTileSize, kTileSize, 10.0f);
            strokeColor(theme::accent);
            strokeWidth(2.0f / k);
            stroke();
        }

        // thumbnail (placeholder art until real images)
        const BlockDescriptor* type = BlockRegistry::builtin().find(block.typeId);
        globalAlpha(bypassed || missing || notRunning ? 0.35f : 1.0f);
        if (NanoImage* img = blockImage(block))
            drawImageFit(*img, 16, 24, kTileSize - 32, 58, 4.0f);
        else
            drawBlockArt(type, 16, 24, kTileSize - 32, 58, false);
        std::string name = missing ? "Missing file"
                         : notRunning ? "Not running"
                         : path.empty() ? (type ? type->displayName : block.typeId)
                         : fileStem(path);
        if (name.size() > 16)
            name = name.substr(0, 15) + "…";
        label(kTileSize / 2, 94, name.c_str(), missing || notRunning ? theme::bad : theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);
        globalAlpha(1.0f);

        char slotText[16];
        std::snprintf(slotText, sizeof(slotText), "S%02d", block.slot + 1);
        label(kTileSize / 2, 110, slotText, theme::textDim, 10.0f, ALIGN_CENTER | ALIGN_MIDDLE);

        // bypass (top left) and remove (top right)
        beginPath();
        circle(15, 13, 6.0f);
        fillColor(bypassed ? theme::border : theme::good);
        fill();
        label(kTileSize - 15, 13, "×", theme::textDim, 18.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        restore();

        fHits.push_back({ { x, y, size, size }, Hit::Tile, i });
        fHits.push_back({ hitRect(6, 4, 18, 18), Hit::TileBypass, i });
        fHits.push_back({ hitRect(kTileSize - 24, 4, 18, 18), Hit::TileRemove, i });
    }

    void drawSelectedPanel()
    {
        const float top = kChainTop + fLayout.height + 28.0f;
        const float left = 16.0f;
        const float width = fWidth - 32.0f;
        const float height = fHeight - kBottomBarHeight - top - 16.0f;
        if (height < 60.0f)
            return;
        fillRect(left, top, width, height, theme::panel, 10.0f);

        if (fMergeSelected && fChain.split)
        {
            drawMergePanel(top);
            return;
        }

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
        if (type->meterName)
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
                if (type->meterStyle == MeterStyle::Looper)
                    drawLooperStatus(cx, cy, fParams[slotMeterParam(block.slot)]);
                else
                    drawBlockMeter(cx, cy, fParams[slotMeterParam(block.slot)], type->meterRange, type->meterName);
                continue;
            }

            const ParamSpec& spec = type->params[control.param];
            const uint32_t index = slotParam(block.slot, control.param);
            if (spec.scale == ParamScale::Toggle)
                drawButton({ cx - 40.0f, cy - 14.0f, 80.0f, 28.0f }, spec.name, fParams[index] >= 0.5f, theme::accent,
                           Hit::Toggle, static_cast<int>(index));
            else if (spec.scale == ParamScale::Momentary)
                drawButton({ cx - 40.0f, cy - 22.0f, 80.0f, 44.0f }, spec.name, fParams[index] >= 0.5f, theme::accent,
                           Hit::Momentary, static_cast<int>(index));
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
        // one column of tall rows while they fit; then two columns, and shorter rows if needed
        const float itemsH = y + h - 12.0f - listTop;
        const int count = static_cast<int>(items.size());
        const int columns = count * 72.0f > itemsH ? 2 : 1;
        const int rows = (count + columns - 1) / columns;
        const float itemH = std::clamp((itemsH + 8.0f) / rows - 8.0f, 30.0f, 64.0f);
        const float itemW = (itemsW - (columns - 1) * 8.0f) / columns;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const int col = static_cast<int>(i) / rows, row = static_cast<int>(i) % rows;
            const Rect r { itemsX + col * (itemW + 8.0f), listTop + row * (itemH + 8.0f), itemW, itemH };
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

    void drawMergePanel(float top)
    {
        label(32.0f, top + 22.0f, "Merge", theme::text, 16.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        const bool stereo = fParams[kParamInputMode] >= 0.5f;
        label(32.0f, top + 42.0f,
              stereo ? "Mixes branch A (top row) and branch B (bottom row) back together"
                     : "Mixes branch A (top row) and branch B (bottom row) back together. Pan needs Stereo mode.",
              theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        float x = 32.0f + kKnobRadius + 8.0f;
        const float cy = top + 42.0f + 24.0f + kKnobRadius + 14.0f;
        for (const uint32_t index : { kParamMergeBalance, kParamMergeLevelA, kParamMergeLevelB, kParamMergePanA,
                                      kParamMergePanB })
        {
            static constexpr const char* names[] = { "A / B", "Level A", "Level B", "Pan A", "Pan B" };
            globalAlpha(!stereo && (index == kParamMergePanA || index == kParamMergePanB) ? 0.4f : 1.0f);
            drawKnob(x, cy, index, names[index - kParamMergeBalance]);
            globalAlpha(1.0f);
            x += 100.0f;
        }
        drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, "Invert A", fParams[kParamMergeInvertA] >= 0.5f,
                   theme::accent, Hit::Toggle, kParamMergeInvertA);
        x += 100.0f;
        drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, "Invert B", fParams[kParamMergeInvertB] >= 0.5f,
                   theme::accent, Hit::Toggle, kParamMergeInvertB);
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
    bool fMergeSelected = false;
    Section fAddSection = Section::Pre; // where the block picked in the file browser goes
    ChainLayout fLayout;

    std::vector<HitArea> fHits;
    float fWidth = DISTRHO_UI_DEFAULT_WIDTH;
    float fHeight = DISTRHO_UI_DEFAULT_HEIGHT;
    float fMouseX = -1.0f, fMouseY = -1.0f;
    float fScrollX = 0.0f;

    int fDragParam = -1;
    int fMomentaryParam = -1; // a momentary button being held
    std::chrono::steady_clock::time_point fMomentaryPressedAt;
    bool fMomentaryReleasePending = false;
    static constexpr std::chrono::milliseconds kMomentaryMinHold { 80 };
    float fDragStartY = 0.0f;
    float fDragStartValue = 0.0f;
    uint fLastClickTime = 0;

    int fPressTile = -1;
    float fPressX = 0.0f, fPressY = 0.0f;
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

    // metronome
    bool fMetroPanel = false;
    Rect fSliderRect; // the slider being dragged (w == 0: a knob is)
    std::vector<std::chrono::steady_clock::time_point> fTaps;
    std::chrono::steady_clock::time_point fTapAt, fBeatAt;

    // tuner display
    std::vector<float> fTunerHistory;
    float fTunerFrequency = 0.0f;
    float fTunerCents = 0.0f;
    float fNeedleCents = 0.0f;
    float fStrobePhase[3] {};
    int fTunerNote = -1; // MIDI note
    std::chrono::steady_clock::time_point fTunerSeenAt;
    std::chrono::steady_clock::time_point fLastIdle = std::chrono::steady_clock::now();

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
