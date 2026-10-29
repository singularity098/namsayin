// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "FileLooks.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace xythos {

namespace {

// Random numbers from the key: FNV-1a for the seed, then splitmix64. Both are fixed here (not std::hash), so a file
// looks the same in every build and every version.
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
    dice.next(); // was the body (head or combo); still drawn, so the other choices stay as they were
    // reds mostly, oranges now and then
    const float u = dice.unit();
    look.hue = AmpLook::kMinHue + (AmpLook::kMaxHue - AmpLook::kMinHue) * u * u;
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
    look.cone = static_cast<CabLook::Cone>(dice.below(CabLook::kCones));
    look.cloth = static_cast<CabLook::Cloth>(dice.below(CabLook::kCloths));
    look.badge = static_cast<Badge>(dice.below(kBadges));
    look.corners = dice.below(2) == 1;
    look.hue = dice.between(-60.0f, 60.0f);
    return look;
}

std::string_view cabImageSize(std::string_view id)
{
    const size_t dash = id.rfind('-');
    return dash == std::string_view::npos ? std::string_view() : id.substr(0, dash);
}

std::vector<std::string> cabImagesOfSize(const std::vector<std::string>& ids, std::string_view size)
{
    std::vector<std::string> result;
    for (const std::string& id : ids)
        if (cabImageSize(id) == size)
            result.push_back(id);
    const auto number = [](const std::string& id) { return std::atoi(id.c_str() + id.rfind('-') + 1); };
    std::stable_sort(result.begin(), result.end(),
                     [&number](const std::string& a, const std::string& b) { return number(a) < number(b); });
    return result;
}

std::string_view cabSizeInName(std::string_view name)
{
    const auto digit = [&name](size_t i) { return i < name.size() && std::isdigit(static_cast<unsigned char>(name[i])); };
    for (size_t i = 0; i + 4 <= name.size(); ++i)
    {
        // "<count>x<inches>", not part of a longer number
        if ((i > 0 && digit(i - 1)) || digit(i + 4) || (name[i + 1] != 'x' && name[i + 1] != 'X'))
            continue;
        for (const char* size : kCabSizes)
            if (name[i] == size[0] && name[i + 2] == size[2] && name[i + 3] == size[3])
                return size;
    }
    return {};
}

std::string defaultCabImage(std::string_view key, std::string_view fileName, const std::vector<std::string>& ids)
{
    if (ids.empty())
        return {};
    if (key.empty())
    {
        const std::vector<std::string> fours = cabImagesOfSize(ids, "4x12");
        return fours.empty() ? ids.front() : fours.front();
    }
    // most IRs are of a 4x12, so the rest only when the name says so (owner, 2026-10-29; was any size)
    std::vector<std::string> pool;
    if (const std::string_view size = cabSizeInName(fileName); !size.empty())
        pool = cabImagesOfSize(ids, size);
    if (pool.empty())
        pool = cabImagesOfSize(ids, "4x12");
    if (pool.empty())
        pool = ids;
    Dice dice(key);
    return pool[static_cast<size_t>(dice.below(static_cast<int>(pool.size())))];
}

} // namespace xythos
