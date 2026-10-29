// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include "state/ChainState.hpp"

#include <deque>
#include <optional>

namespace xythos {

// Undo/redo as snapshots of the chain (blocks, parameter values, bypass, MIDI map). Snapshots are small, so
// copies are cheap. The UI pushes the state before each edit; one knob gesture is one edit.
class UndoStack {
public:
    static constexpr size_t kLimit = 200;

    // Records `before`, the state an edit is about to change. Clears the redo history.
    void push(const ChainState& before)
    {
        if (!fUndo.empty() && fUndo.back() == before)
            return; // an edit that changed nothing
        fUndo.push_back(before);
        if (fUndo.size() > kLimit)
            fUndo.pop_front();
        fRedo.clear();
    }

    // The state to go back to, given the current one (which becomes redoable)
    std::optional<ChainState> undo(const ChainState& current)
    {
        if (fUndo.empty())
            return std::nullopt;
        ChainState state = std::move(fUndo.back());
        fUndo.pop_back();
        fRedo.push_back(current);
        return state;
    }

    std::optional<ChainState> redo(const ChainState& current)
    {
        if (fRedo.empty())
            return std::nullopt;
        ChainState state = std::move(fRedo.back());
        fRedo.pop_back();
        fUndo.push_back(current);
        return state;
    }

    bool canUndo() const { return !fUndo.empty(); }
    bool canRedo() const { return !fRedo.empty(); }

    void clear()
    {
        fUndo.clear();
        fRedo.clear();
    }

private:
    std::deque<ChainState> fUndo, fRedo;
};

} // namespace xythos
