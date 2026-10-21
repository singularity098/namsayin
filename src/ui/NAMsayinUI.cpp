// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"

#include "OpenUrl.hpp"
#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/NamBlock.hpp"
#include "state/ChainExchange.hpp"
#include "state/ChainState.hpp"

#include <algorithm>
#include <chrono>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

using namespace namsayin;

namespace {

// Theme tokens. Very dark: the window fades from near-black at the top to pure black at the bottom, and every
// surface is a faint translucent white over it.
namespace theme {
const Color backgroundTop(0x0b, 0x0b, 0x0d);
const Color backgroundBottom(0x00, 0x00, 0x00);
const Color bar(255, 255, 255, 0.025f);
const Color panel(255, 255, 255, 0.035f);
const Color tile(255, 255, 255, 0.05f);
const Color tileHover(255, 255, 255, 0.08f);
const Color inset(0, 0, 0, 0.45f);
const Color border(255, 255, 255, 0.10f);
const Color text(0xe6, 0xe6, 0xea);
const Color textDim(0x78, 0x78, 0x82);
const Color good(0x4c, 0xc9, 0x7a);
const Color warn(0xf0, 0xc0, 0x40);
const Color bad(0xe5, 0x48, 0x4d);
// Highlight colour. Will be user-configurable (Settings); purple for now.
Color accent(0x9b, 0x5c, 0xff);
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
    InClip,
    OutClip,
};

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
    return std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())).stem().string();
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
        if (index == kParamInstanceId && value >= 1.0f)
        {
            fInstanceId = static_cast<uint32_t>(value);
            // The host may not have sent us the chain (Reaper's LV2 doesn't): read the DSP's copy
            if (!fGotChainState)
                loadExchangeChain();
        }
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
            chainChanged();
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void uiFileBrowserSelected(const char* const filename) override
    {
        const BlockDescriptor* type = filename != nullptr ? blockTypeForFile(filename) : nullptr;
        if (type == nullptr)
            return;
        const int slot = fChain.freeSlot();
        if (slot < 0)
            return;

        BlockState block;
        block.typeId = type->typeId;
        block.slot = slot;
        block.config = { { "path", filename } };
        if (type == &kNamBlockDescriptor)
            block.config["a2Size"] = 1.0;

        // A new block starts from its defaults; parameter values live in the host
        setHostParameter(slotBypassParam(slot), 0.0f);
        for (int p = 0; p < type->numParams; ++p)
        {
            const ParamSpec& spec = type->params[p];
            block.params[p] = spec.toNormalized(spec.def);
            setHostParameter(slotParam(slot, p), block.params[p]);
        }

        fChain.insert(fAddSection, kMaxBlocks, std::move(block));
        fSelected = indexOfSlot(slot);
        fMergeSelected = false;
        sendChain();
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

        beginPath();
        rect(0, 0, fWidth, fHeight);
        fillPaint(linearGradient(0, 0, 0, fHeight, theme::backgroundTop, theme::backgroundBottom));
        fill();
        drawDither(scale);
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
        if (ev.button != kMouseButtonLeft)
            return false;

        const bool doubleClick = ev.time - fLastClickTime < kDoubleClickMs;
        fLastClickTime = ev.time;

        const HitArea* hit = hitAt(x, y);
        if (hit == nullptr)
            return false;

        switch (hit->kind)
        {
        case Hit::Knob:
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
            const uint32_t index = slotBypassParam(fChain.blocks[hit->index].slot);
            setNormalized(index, normalized(index) >= 0.5f ? 0.0f : 1.0f, true);
            syncBypassToChain(index);
            return true;
        }

        case Hit::TileRemove:
            fChain.remove(hit->index);
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = static_cast<int>(fChain.blocks.size()) - 1;
            sendChain();
            return true;

        case Hit::Split: {
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
            fChain.removeSplit();
            fMergeSelected = false;
            sendChain();
            return true;

        case Hit::AddBlock: {
            fAddSection = static_cast<Section>(hit->index);
            FileBrowserOptions options;
            options.title = "Add a NAM model (.nam) or cab IR (.wav)";
            openFileBrowser(options);
            return true;
        }

        case Hit::A2Size: {
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

    bool onScroll(const ScrollEvent& ev) override
    {
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
    bool onKeyboard(const KeyboardEvent& ev) override
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
        // the metronome's pulse and sweep
        if (fParams[kParamMetronome] >= 0.5f || fMetroPanel)
            repaint();

        // the tuner animates (needle easing, strobe)
        const auto now = std::chrono::steady_clock::now();
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

        // keep the chain's copy (for presets and undo later) in step
        if (isSlot(index))
        {
            const uint32_t offset = index - kSlotParamBase;
            const int slot = static_cast<int>(offset / kSlotParamCount);
            const int param = static_cast<int>(offset % kSlotParamCount) - 1;
            for (BlockState& block : fChain.blocks)
            {
                if (block.slot == slot && param >= 0)
                    block.params[param] = n;
            }
        }
        repaint();
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

    bool onRelease(float x, float y)
    {
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

    // A dark gradient only spans a handful of 8-bit levels, so it shows as bands. A faint noise texture (0-3 levels
    // of white per pixel, one texel per physical pixel) breaks the bands up into a smooth fade.
    // NanoVG has no built-in dithering, so we draw it ourselves.
    void drawDither(float scale)
    {
        constexpr uint kSize = 128;
        if (!fDither.isValid())
        {
            std::vector<uchar> pixels(kSize * kSize * 4);
            uint32_t seed = 0x12345678u;
            for (uint i = 0; i < kSize * kSize; ++i)
            {
                seed = seed * 1664525u + 1013904223u; // LCG: plenty for noise
                pixels[i * 4 + 0] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
                pixels[i * 4 + 3] = static_cast<uchar>((seed >> 24) % 4);
            }
            fDither = createImageFromRGBA(kSize, kSize, pixels.data(),
                                          static_cast<ImageFlags>(IMAGE_REPEAT_X | IMAGE_REPEAT_Y));
        }
        // Fade the noise out towards the bottom, which stays pure black
        constexpr int kStrips = 32;
        const float tile = kSize / scale;
        const float stripHeight = fHeight / kStrips;
        for (int i = 0; i < kStrips; ++i)
        {
            const float alpha = 1.0f - (i + 0.5f) / kStrips;
            beginPath();
            rect(0, i * stripHeight, fWidth, stripHeight + 0.5f);
            fillPaint(imagePattern(0, 0, tile, tile, 0.0f, fDither, alpha));
            fill();
        }
    }

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

        label(cx, cy + kKnobRadius + 16.0f, name, theme::textDim, 12.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(cx, cy + kKnobRadius + 31.0f, valueText(index).c_str(), theme::text, 12.0f,
              ALIGN_CENTER | ALIGN_MIDDLE);

        fHits.push_back({ { cx - kKnobRadius - 6, cy - kKnobRadius - 6, 2 * kKnobRadius + 12, 2 * kKnobRadius + 40 },
                          Hit::Knob, static_cast<int>(index) });
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
        drawButton({ fWidth - 96.0f, 10.0f, 80.0f, 28.0f }, "Bypass", fParams[kParamBypass] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamBypass);
        drawButton({ fWidth - 184.0f, 10.0f, 80.0f, 28.0f }, "Tuner", fParams[kParamTuner] >= 0.5f, theme::accent,
                   Hit::Toggle, kParamTuner);

        // Metronome: on/off, flashing on every beat, and ▾ for its panel
        const bool running = fParams[kParamMetronome] >= 0.5f;
        const Rect metro { fWidth - 304.0f, 10.0f, 88.0f, 28.0f };
        drawButton(metro, "Metronome", running, theme::accent, Hit::Toggle, kParamMetronome);
        const float pulse = beatPulse();
        if (running && pulse > 0.0f)
            fillRect(metro.x, metro.y, metro.w, metro.h, Color(255, 255, 255, 0.35f * pulse), 6.0f);
        drawButton({ metro.x + metro.w + 2.0f, 10.0f, 26.0f, 28.0f }, "▾", fMetroPanel, theme::accent, Hit::MetroPanel,
                   0);

        drawButton({ fWidth - 408.0f, 10.0f, 92.0f, 28.0f }, "Fretbored ↗", false, theme::accent, Hit::Fretbored, 0);
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
            label(l.split.x + l.split.w + 24.0f, l.rowY[0] + kTileSize / 2, "Click + to add a NAM model or a cab IR",
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
        const bool missing = !path.empty() && !std::filesystem::exists(
            std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())));
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

        // generic images (placeholders until real images): an amp grille for NAM, a speaker cone for IRs
        globalAlpha(bypassed || missing || notRunning ? 0.35f : 1.0f);
        fillRect(18, 24, kTileSize - 36, 52, theme::inset, 6.0f);
        if (block.typeId == kIrBlockDescriptor.typeId)
        {
            for (const float r : { 22.0f, 13.0f, 5.0f })
            {
                beginPath();
                circle(kTileSize / 2, 50, r);
                strokeColor(theme::border);
                strokeWidth(2.0f);
                stroke();
            }
        }
        else
        {
            for (int row = 0; row < 5; ++row)
            {
                beginPath();
                moveTo(24, 32 + row * 9.0f);
                lineTo(kTileSize - 24, 32 + row * 9.0f);
                strokeColor(theme::border);
                strokeWidth(2.0f);
                stroke();
            }
        }
        std::string name = missing ? "Missing file" : (notRunning ? "Not running" : fileStem(path));
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
        const float height = fHeight - kBottomBarHeight - top - 16.0f;
        if (height < 60.0f)
            return;
        fillRect(16.0f, top, fWidth - 32.0f, height, theme::panel, 10.0f);

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

        const std::string path = block.config.value("path", std::string());
        label(32.0f, top + 22.0f, path.empty() ? type->displayName : fileStem(path).c_str(), theme::text, 16.0f,
              ALIGN_LEFT | ALIGN_MIDDLE);
        label(32.0f, top + 42.0f, path.c_str(), theme::textDim, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);

        float x = 32.0f + kKnobRadius + 8.0f;
        const float cy = top + 42.0f + 24.0f + kKnobRadius + 14.0f;
        for (int p = 0; p < type->numParams; ++p)
        {
            const uint32_t index = slotParam(block.slot, p);
            if (type->params[p].scale == ParamScale::Toggle)
            {
                drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, type->params[p].name, fParams[index] >= 0.5f,
                           theme::accent, Hit::Toggle, static_cast<int>(index));
            }
            else
            {
                drawKnob(x, cy, index, type->params[p].name);
            }
            x += 100.0f;
        }

        if (block.typeId == kNamBlockDescriptor.typeId)
        {
            const bool lite = block.config.value("a2Size", 1.0) < 0.5;
            drawButton({ x - 40.0f, cy - 14.0f, 80.0f, 28.0f }, lite ? "A2: Lite" : "A2: Full", lite, theme::accent,
                       Hit::A2Size, fSelected);
        }
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
    NanoImage fDither;
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
    float fDragStartY = 0.0f;
    float fDragStartValue = 0.0f;
    uint fLastClickTime = 0;

    int fPressTile = -1;
    float fPressX = 0.0f, fPressY = 0.0f;
    bool fDraggingTile = false;

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
