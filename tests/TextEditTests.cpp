// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include <doctest/doctest.h>

#include "ui/TextEdit.hpp"

using namespace xythos;

namespace {

TextEdit editing(const char* text)
{
    TextEdit edit;
    edit.setText(text);
    return edit;
}

} // namespace

TEST_CASE("TextEdit types at the cursor and replaces the selection")
{
    TextEdit edit = editing("hello");
    CHECK(edit.cursor == 5);
    edit.moveLeft(false, false);
    edit.insert("X");
    CHECK(edit.text == "hellXo");
    edit.selectAll();
    edit.insert("new\nline");
    CHECK(edit.text == "newline"); // control characters are dropped
    CHECK(edit.cursor == 7);
    CHECK(!edit.hasSelection());
}

TEST_CASE("TextEdit Ctrl+Backspace and Ctrl+Delete remove whole words")
{
    TextEdit edit = editing("clean amp  model");
    edit.backspace(true);
    CHECK(edit.text == "clean amp  ");
    edit.backspace(true); // skips the spaces, then the word
    CHECK(edit.text == "clean ");
    edit.backspace(false);
    CHECK(edit.text == "clean");

    edit = editing("/home/me/NAM models");
    edit.backspace(true);
    CHECK(edit.text == "/home/me/NAM ");
    edit.backspace(true);
    CHECK(edit.text == "/home/me/");
    edit.backspace(true); // a run of punctuation goes as one
    CHECK(edit.text == "/home/me");

    edit = editing("one two three");
    edit.moveHome(false);
    edit.del(true);
    CHECK(edit.text == " two three");
    edit.del(true);
    CHECK(edit.text == " three");
}

TEST_CASE("TextEdit moves by word and extends the selection with Shift")
{
    TextEdit edit = editing("one two three");
    edit.moveLeft(true, false);
    CHECK(edit.cursor == 8);
    edit.moveLeft(true, true);
    CHECK(edit.selectedText() == "two ");
    edit.moveRight(false, false); // collapses to the selection's end
    CHECK(edit.cursor == 8);
    CHECK(!edit.hasSelection());
    edit.moveHome(true);
    CHECK(edit.selectedText() == "one two ");
    edit.moveEnd(false);
    CHECK(edit.cursor == edit.text.size());
}

TEST_CASE("TextEdit double click selects the word under the pointer")
{
    TextEdit edit = editing("Plexi crunch.nam");
    edit.selectWordAt(8); // inside "crunch"
    CHECK(edit.selectedText() == "crunch");
    edit.selectWordAt(0);
    CHECK(edit.selectedText() == "Plexi");
    edit.selectWordAt(5); // just after "Plexi"
    CHECK(edit.selectedText() == "Plexi");
    edit.selectWordAt(edit.text.size()); // past the end: the last word
    CHECK(edit.selectedText() == "nam");

    TextEdit empty = editing("");
    empty.selectWordAt(0);
    CHECK(!empty.hasSelection());
}

TEST_CASE("TextEdit keeps whole UTF-8 characters")
{
    TextEdit edit = editing("caf\xc3\xa9 b\xc3\xa4r");
    edit.backspace(false);
    CHECK(edit.text == "caf\xc3\xa9 b\xc3\xa4");
    edit.backspace(false);
    CHECK(edit.text == "caf\xc3\xa9 b");
    edit.backspace(true);
    edit.backspace(true);
    CHECK(edit.text.empty());

    edit = editing("\xc3\xa9t\xc3\xa9");
    edit.moveLeft(false, false);
    CHECK(edit.cursor == 3);
    edit.setCursor(1, false); // inside a character: moves to its start
    CHECK(edit.cursor == 0);
    edit.selectWordAt(1);
    CHECK(edit.selectedText() == edit.text);
}
