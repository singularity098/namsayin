// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"

#include "engine/BlockRegistry.hpp"
#include "engine/Params.hpp"
#include "engine/blocks/NamBlock.hpp"
#include "state/ChainState.hpp"

#include <algorithm>
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

// Theme tokens (dark)
namespace theme {
const Color background(0x16, 0x17, 0x1c);
const Color bar(0x1f, 0x21, 0x28);
const Color panel(0x23, 0x25, 0x2d);
const Color tile(0x2c, 0x2f, 0x39);
const Color tileHover(0x35, 0x39, 0x45);
const Color border(0x3a, 0x3e, 0x4a);
const Color text(0xe8, 0xea, 0xf0);
const Color textDim(0x8a, 0x8f, 0x9c);
const Color accent(0xf5, 0xa6, 0x23);
const Color good(0x4c, 0xc9, 0x7a);
const Color warn(0xf0, 0xc0, 0x40);
const Color bad(0xe5, 0x48, 0x4d);
} // namespace theme

constexpr float kTopBarHeight = 48.0f;
constexpr float kBottomBarHeight = 132.0f;
constexpr float kTileSize = 120.0f;
constexpr float kTileGap = 16.0f;
constexpr float kChainTop = kTopBarHeight + 24.0f;
constexpr float kKnobRadius = 22.0f;
constexpr uint kDoubleClickMs = 350;

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
    InClip,
    OutClip,
};

struct HitArea {
    Rect rect;
    Hit kind;
    int index;
};

std::string fileStem(const std::string& path)
{
    return std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())).stem().string();
}

bool hasNamExtension(const char* filename)
{
    const size_t len = std::strlen(filename);
    if (len < 4)
        return false;
    std::string ext(filename + len - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".nam";
}

} // namespace

class NAMsayinUI : public UI
{
public:
    NAMsayinUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();

        for (uint32_t i = 0; i < kGlobalParamCount; ++i)
            fParams[i] = globalParamSpec(i).def;

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
            fChain = std::move(*chain);
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = fChain.blocks.empty() ? -1 : 0;
            repaint();
        }
    }

    void uiFileBrowserSelected(const char* const filename) override
    {
        if (filename == nullptr || !hasNamExtension(filename))
            return;
        const int slot = fChain.freeSlot();
        if (slot < 0)
            return;

        BlockState block;
        block.typeId = kNamBlockDescriptor.typeId;
        block.slot = slot;
        block.config = { { "path", filename }, { "a2Size", 1.0 } };

        // A new block starts from its defaults; parameter values live in the host
        setHostParameter(slotBypassParam(slot), 0.0f);
        for (int p = 0; p < kNamBlockDescriptor.numParams; ++p)
        {
            const ParamSpec& spec = kNamBlockDescriptor.params[p];
            block.params[p] = spec.toNormalized(spec.def);
            setHostParameter(slotParam(slot, p), block.params[p]);
        }

        fChain.blocks.push_back(std::move(block));
        fSelected = static_cast<int>(fChain.blocks.size()) - 1;
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

        fillRect(0, 0, fWidth, fHeight, theme::background);
        drawTopBar();
        drawChain();
        drawSelectedPanel();
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
                return onRelease(x);
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
            fPressTile = hit->index;
            fPressX = x;
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
            fChain.blocks.erase(fChain.blocks.begin() + hit->index);
            if (fSelected >= static_cast<int>(fChain.blocks.size()))
                fSelected = static_cast<int>(fChain.blocks.size()) - 1;
            sendChain();
            return true;

        case Hit::AddBlock: {
            FileBrowserOptions options;
            options.title = "Add a NAM model (.nam)";
            openFileBrowser(options);
            return true;
        }

        case Hit::A2Size: {
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

    bool onScroll(const ScrollEvent& ev) override
    {
        const float y = static_cast<float>(ev.pos.getY() / getScaleFactor());
        if (y < kChainTop || y > kChainTop + kTileSize)
            return false;
        const float delta = static_cast<float>(ev.delta.getY() != 0.0 ? ev.delta.getY() : ev.delta.getX());
        fScrollX = std::max(0.0f, fScrollX - delta * 40.0f);
        repaint();
        return true;
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
            std::snprintf(buf, sizeof(buf), "%.1f %s", v, spec->unit);
            return buf;
        }
        const GlobalParamSpec& spec = globalParamSpec(index);
        std::snprintf(buf, sizeof(buf), "%+.1f %s", fParams[index], spec.unit);
        return buf;
    }

    void sendChain()
    {
        setState("chain", fChain.toJson().c_str());
        repaint();
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
        fontSize(size);
        fillColor(color);
        textAlign(align);
        text(x, y, str, nullptr);
    }

    void drawButton(const Rect& r, const char* str, bool on, const Color& onColor, Hit kind, int index)
    {
        fillRect(r.x, r.y, r.w, r.h, on ? onColor : theme::tile, 6.0f);
        label(r.x + r.w / 2, r.y + r.h / 2, str, on ? theme::background : theme::text, 13.0f,
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
            fillRect(bx, y, barWidth, h, theme::tile, 2.0f);
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
        drawButton({ fWidth - 96.0f, 10.0f, 80.0f, 28.0f }, "Bypass", fParams[kParamBypass] >= 0.5f, theme::warn,
                   Hit::Toggle, kParamBypass);
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
            label(16.0f + kTileSize + 32.0f, y + kTileSize / 2, "Click + to add a NAM model", theme::textDim,
                  14.0f, ALIGN_LEFT | ALIGN_MIDDLE);
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
        const bool missing = !path.empty() && !std::filesystem::exists(
            std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())));
        const bool bypassed = fParams[slotBypassParam(block.slot)] >= 0.5f;
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

        // generic amp image: a speaker grille (placeholder until real images)
        globalAlpha(bypassed || missing ? 0.35f : 1.0f);
        fillRect(x + 18, y + 24, kTileSize - 36, 52, theme::panel, 6.0f);
        for (int row = 0; row < 5; ++row)
        {
            beginPath();
            moveTo(x + 24, y + 32 + row * 9.0f);
            lineTo(x + kTileSize - 24, y + 32 + row * 9.0f);
            strokeColor(theme::border);
            strokeWidth(2.0f);
            stroke();
        }
        std::string name = missing ? "Missing file" : fileStem(path);
        if (name.size() > 16)
            name = name.substr(0, 15) + "…";
        label(x + kTileSize / 2, y + 94, name.c_str(), missing ? theme::bad : theme::text, 12.0f,
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
        const float height = fHeight - kBottomBarHeight - top - 16.0f;
        if (height < 60.0f)
            return;
        fillRect(16.0f, top, fWidth - 32.0f, height, theme::panel, 10.0f);

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

    bool fInClipLatched = false;
    bool fOutClipLatched = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinUI)
};

UI* createUI()
{
    return new NAMsayinUI();
}

END_NAMESPACE_DISTRHO
