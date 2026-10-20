// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "DistrhoUI.hpp"

START_NAMESPACE_DISTRHO

// M0 skeleton: an empty dark window that resizes and scales for HiDPI.
// Everything is drawn in logical pixels; the scale factor maps them to the real window size.
class NAMsayinUI : public UI
{
public:
    NAMsayinUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        loadSharedResources();

        const double scaleFactor = getScaleFactor();
        setGeometryConstraints(NAMSAYIN_UI_MIN_WIDTH * scaleFactor, NAMSAYIN_UI_MIN_HEIGHT * scaleFactor, false);

        if (d_isNotEqual(scaleFactor, 1.0))
            setSize(DISTRHO_UI_DEFAULT_WIDTH * scaleFactor, DISTRHO_UI_DEFAULT_HEIGHT * scaleFactor);
    }

protected:
    void parameterChanged(uint32_t, float) override
    {
    }

    void onNanoDisplay() override
    {
        const float scale = static_cast<float>(getScaleFactor());
        const float width = getWidth() / scale;
        const float height = getHeight() / scale;

        save();
        this->scale(scale, scale);

        // background
        beginPath();
        rect(0, 0, width, height);
        fillColor(Color(0x16, 0x17, 0x1c));
        fill();

        // top bar
        beginPath();
        rect(0, 0, width, kTopBarHeight);
        fillColor(Color(0x1f, 0x21, 0x28));
        fill();

        // bottom bar
        beginPath();
        rect(0, height - kBottomBarHeight, width, kBottomBarHeight);
        fillColor(Color(0x1f, 0x21, 0x28));
        fill();

        fontSize(20.0f);
        fillColor(Color(0xe8, 0xea, 0xf0));
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(16.0f, kTopBarHeight / 2.0f, "NAMsayin", nullptr);

        fontSize(14.0f);
        fillColor(Color(0x8a, 0x8f, 0x9c));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(width / 2.0f, height / 2.0f, "Signal chain goes here", nullptr);

        restore();
    }

private:
    static constexpr float kTopBarHeight = 48.0f;
    static constexpr float kBottomBarHeight = 120.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NAMsayinUI)
};

UI* createUI()
{
    return new NAMsayinUI();
}

END_NAMESPACE_DISTRHO
