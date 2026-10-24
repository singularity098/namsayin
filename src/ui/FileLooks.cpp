// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "FileLooks.hpp"

namespace namsayin {

namespace {

// Random numbers from the key: FNV-1a for the seed, then splitmix64. Both are fixed here (not std::hash), so a file
// looks the same on every machine and in every version.
class Dice {
public:
    explicit Dice(std::string_view key)
    {
        for (const char c : key)
        {
            fState ^= static_cast<unsigned char>(c);
            fState *= 0x100000001b3ull;
        }
    }

    uint64_t next()
    {
        uint64_t z = (fState += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    int below(int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }
    float unit() { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); } // 0..1
    float between(float lo, float hi) { return lo + (hi - lo) * unit(); }

private:
    uint64_t fState = 0xcbf29ce484222325ull;
};

} // namespace

AmpLook ampLook(std::string_view key)
{
    AmpLook look;
    if (key.empty())
        return look;
    Dice dice(key);
    look.knobs = 3 + dice.below(5);
    look.grille = static_cast<AmpLook::Grille>(dice.below(AmpLook::kGrilles));
    look.badge = static_cast<Badge>(dice.below(kBadges));
    look.body = static_cast<AmpLook::Body>(dice.below(AmpLook::kBodies));
    look.hue = dice.between(-28.0f, 28.0f);
    for (float& turn : look.turns)
        turn = dice.between(0.2f, 0.8f);
    return look;
}

CabLook cabLook(std::string_view key)
{
    CabLook look;
    if (key.empty())
        return look;
    Dice dice(key);
    static constexpr int layouts[][2] = { { 1, 1 }, { 1, 2 }, { 2, 1 }, { 2, 2 }, { 1, 3 } };
    const int layout = dice.below(5);
    look.rows = layouts[layout][0];
    look.columns = layouts[layout][1];
    look.cone = static_cast<CabLook::Cone>(dice.below(CabLook::kCones));
    look.cloth = static_cast<CabLook::Cloth>(dice.below(CabLook::kCloths));
    look.badge = static_cast<Badge>(dice.below(kBadges));
    look.corners = dice.below(2) == 1;
    look.hue = dice.between(-60.0f, 60.0f);
    return look;
}

} // namespace namsayin
