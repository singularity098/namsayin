// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace namsayin {

// Each NAM model and cab IR gets a look of its own in the placeholder art, picked from its file's path, so two amps
// or two cabs side by side tell apart at a glance and a file looks the same wherever it appears (chain, library,
// presets). Plain data, no DPF, so the tests can check it.
//
// An empty key gives the generic look (the Add Block list, which has no file).

// The logos, shared by amps and cabs
enum Badge { BadgeNone, BadgeCircle, BadgeDiamond, BadgeBolt, BadgeStar, BadgePlate, BadgeTriangle, kBadges };

struct AmpLook {
    enum Grille { Slats, Bars, Crosshatch, Perforated, Waves, Zigzag, kGrilles };
    enum Body { Head, Combo, kBodies };

    int knobs = 6;           // along the top panel, 3..7
    Grille grille = Slats;   // the pattern of the grille
    Badge badge = BadgeNone; // the logo on the grille
    Body body = Head;        // a landscape head, or a taller combo
    float hue = 0.0f;        // shift of the amp colour, in degrees
    std::array<float, 7> turns { 0.55f, 0.4f, 0.65f, 0.5f, 0.35f, 0.6f, 0.45f }; // knob positions, 0..1
};

struct CabLook {
    enum Cone { Plain, Rim, Spokes, BigCap, kCones };
    enum Cloth { Bare, Diagonal, Vertical, kCloths };

    Cone cone = Plain;       // how each speaker is drawn
    Cloth cloth = Bare;      // lines of grille cloth behind the speakers
    Badge badge = BadgeNone; // the logo above the speakers
    bool corners = false;    // metal corner protectors
    float hue = 0.0f;        // shift of the cab colour, in degrees
};

AmpLook ampLook(std::string_view key);
CabLook cabLook(std::string_view key);

} // namespace namsayin
