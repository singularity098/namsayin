// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstddef>
#include <string>

namespace xythos {

// Editing a single-line UTF-8 text field: a cursor and a selection, moving by character or word, deleting words
// (Ctrl+Backspace), selecting the word under a double click. Plain data, no DPF, so it can be tested.
//
// Positions are byte offsets that always sit on a character boundary. The selection runs between `anchor` and
// `cursor` (in either order); it is empty when they're equal.
struct TextEdit {
    std::string text;
    size_t cursor = 0;
    size_t anchor = 0;

    bool hasSelection() const { return cursor != anchor; }
    size_t selectionStart() const { return cursor < anchor ? cursor : anchor; }
    size_t selectionEnd() const { return cursor < anchor ? anchor : cursor; }
    std::string selectedText() const { return text.substr(selectionStart(), selectionEnd() - selectionStart()); }

    // Replaces the whole text, with the cursor at its end
    void setText(std::string value);
    // Keeps the cursor and anchor inside the text and on character boundaries (after the text changed directly)
    void clamp();

    void selectAll();
    void selectWordAt(size_t pos); // the word (or run of spaces or punctuation) around a position
    void setCursor(size_t pos, bool extend); // extend: keep the anchor (Shift+click)

    // Inserts at the cursor, replacing the selection. Line breaks and other control characters are dropped.
    void insert(const std::string& str);

    // word: by word (Ctrl), extend: grow the selection (Shift)
    void moveLeft(bool word, bool extend);
    void moveRight(bool word, bool extend);
    void moveHome(bool extend);
    void moveEnd(bool extend);
    void backspace(bool word); // deletes the selection, else the character or word before the cursor
    void del(bool word);       // deletes the selection, else the character or word after the cursor
    void deleteSelection();

    // The start of the word before / end of the word after a position, as Ctrl+Left/Right move
    size_t wordLeft(size_t pos) const;
    size_t wordRight(size_t pos) const;
    size_t charLeft(size_t pos) const;
    size_t charRight(size_t pos) const;
};

} // namespace xythos
