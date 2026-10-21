// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "state/UndoStack.hpp"

using namespace namsayin;

namespace {

ChainState chainWith(int blocks)
{
    ChainState chain;
    for (int i = 0; i < blocks; ++i)
        chain.blocks.push_back({ "test.gain", i });
    return chain;
}

} // namespace

TEST_CASE("undo and redo walk through chain snapshots")
{
    UndoStack stack;
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.undo(chainWith(0)));

    // three edits: 0 -> 1 -> 2 -> 3 blocks
    stack.push(chainWith(0));
    stack.push(chainWith(1));
    stack.push(chainWith(2));
    ChainState current = chainWith(3);

    current = *stack.undo(current);
    CHECK(current.blocks.size() == 2);
    current = *stack.undo(current);
    CHECK(current.blocks.size() == 1);
    CHECK(stack.canRedo());
    current = *stack.redo(current);
    CHECK(current.blocks.size() == 2);

    // a new edit drops the redo history
    stack.push(current);
    current = chainWith(5);
    CHECK_FALSE(stack.canRedo());
    current = *stack.undo(current);
    CHECK(current.blocks.size() == 2);
    current = *stack.undo(current);
    current = *stack.undo(current);
    CHECK(current.blocks.empty());
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("undo ignores repeated snapshots and keeps a bounded history")
{
    UndoStack stack;
    stack.push(chainWith(1));
    stack.push(chainWith(1));
    ChainState current = *stack.undo(chainWith(2));
    CHECK_FALSE(stack.canUndo());

    for (size_t i = 0; i < UndoStack::kLimit + 50; ++i)
        stack.push(chainWith(static_cast<int>(i % 3)));
    size_t steps = 0;
    while (std::optional<ChainState> previous = stack.undo(current))
    {
        current = *previous;
        ++steps;
    }
    CHECK(steps == UndoStack::kLimit);
}
