// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "engine/BlockRegistry.hpp"

#include "engine/blocks/ChorusBlock.hpp"
#include "engine/blocks/CompressorBlock.hpp"
#include "engine/blocks/DelayBlock.hpp"
#include "engine/blocks/DoublerBlock.hpp"
#include "engine/blocks/DriveBlocks.hpp"
#include "engine/blocks/EqBlocks.hpp"
#include "engine/blocks/FlangerBlock.hpp"
#include "engine/blocks/GateBlock.hpp"
#include "engine/blocks/IrBlock.hpp"
#include "engine/blocks/NamBlock.hpp"
#include "engine/blocks/PhaserBlock.hpp"
#include "engine/blocks/ReverbBlock.hpp"
#include "engine/blocks/TremoloBlock.hpp"
#include "engine/blocks/WahBlocks.hpp"

namespace namsayin {

const char* categoryName(Category category)
{
    switch (category)
    {
    case Category::Tone3000AmpHead: return "Tone3000 Amp Head";
    case Category::Tone3000AmpCab:  return "Tone3000 Amp + Cab";
    case Category::Tone3000Cab:     return "Tone3000 Cab";
    case Category::LocalNam:        return "Local NAM";
    case Category::LocalIr:         return "Local IR";
    case Category::Distortion:      return "Distortion";
    case Category::Dynamics:        return "Dynamics";
    case Category::Eq:              return "EQ";
    case Category::Modulation:      return "Modulation";
    case Category::Reverb:          return "Reverb";
    case Category::Delay:           return "Delay";
    case Category::PitchSynth:      return "Pitch/Synth";
    case Category::Filter:          return "Filter";
    case Category::Wah:             return "Wah";
    }
    return "";
}

BlockRegistry::BlockRegistry(std::vector<const BlockDescriptor*> types)
    : fTypes(std::move(types))
{
}

const BlockRegistry& BlockRegistry::builtin()
{
    // One line per block type, in Add Block dialog order within each category
    static const BlockRegistry registry({
        &kNamBlockDescriptor,
        &kIrBlockDescriptor,
        &kBoostBlockDescriptor,
        &kOverdriveBlockDescriptor,
        &kDistortionBlockDescriptor,
        &kFuzzBlockDescriptor,
        &kCompressorBlockDescriptor,
        &kGateBlockDescriptor,
        &kGraphicEqBlockDescriptor,
        &kParametricEqBlockDescriptor,
        &kChorusBlockDescriptor,
        &kFlangerBlockDescriptor,
        &kPhaserBlockDescriptor,
        &kTremoloBlockDescriptor,
        &kDoublerBlockDescriptor,
        &kReverbBlockDescriptor,
        &kDelayBlockDescriptor,
        &kWahBlockDescriptor,
        &kAutoWahBlockDescriptor,
    });
    return registry;
}

const BlockDescriptor* BlockRegistry::find(std::string_view typeId) const
{
    for (const BlockDescriptor* type : fTypes)
    {
        if (typeId == type->typeId)
            return type;
    }
    return nullptr;
}

} // namespace namsayin
