// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "TextEdit.hpp"

#include <algorithm>

namespace namsayin {

namespace {

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xc0) == 0x80; }

enum class CharClass { Space, Word, Punctuation };

// Letters, digits and anything non-ASCII are word characters; so a path or a file name breaks at / . - and spaces
CharClass classOf(char c)
{
    const auto u = static_cast<unsigned char>(c);
    if (u == ' ' || u == '\t')
        return CharClass::Space;
    if (u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_')
        return CharClass::Word;
    return CharClass::Punctuation;
}

} // namespace

void TextEdit::setText(std::string value)
{
    text = std::move(value);
    cursor = anchor = text.size();
}

void TextEdit::clamp()
{
    auto fix = [this](size_t pos) {
        pos = std::min(pos, text.size());
        while (pos > 0 && pos < text.size() && isContinuation(text[pos]))
            --pos;
        return pos;
    };
    cursor = fix(cursor);
    anchor = fix(anchor);
}

size_t TextEdit::charLeft(size_t pos) const
{
    if (pos == 0)
        return 0;
    --pos;
    while (pos > 0 && isContinuation(text[pos]))
        --pos;
    return pos;
}

size_t TextEdit::charRight(size_t pos) const
{
    if (pos >= text.size())
        return text.size();
    ++pos;
    while (pos < text.size() && isContinuation(text[pos]))
        ++pos;
    return pos;
}

size_t TextEdit::wordLeft(size_t pos) const
{
    pos = std::min(pos, text.size());
    while (pos > 0 && classOf(text[pos - 1]) == CharClass::Space)
        --pos;
    if (pos == 0)
        return 0;
    const CharClass cls = classOf(text[pos - 1]);
    while (pos > 0 && classOf(text[pos - 1]) == cls)
        --pos;
    return pos;
}

size_t TextEdit::wordRight(size_t pos) const
{
    pos = std::min(pos, text.size());
    while (pos < text.size() && classOf(text[pos]) == CharClass::Space)
        ++pos;
    if (pos == text.size())
        return pos;
    const CharClass cls = classOf(text[pos]);
    while (pos < text.size() && classOf(text[pos]) == cls)
        ++pos;
    return pos;
}

void TextEdit::selectAll()
{
    anchor = 0;
    cursor = text.size();
}

void TextEdit::selectWordAt(size_t pos)
{
    if (text.empty())
    {
        cursor = anchor = 0;
        return;
    }
    pos = std::min(pos, text.size());
    // past the end, or between a word and what follows it: take the character before
    if (pos == text.size() || (pos > 0 && classOf(text[pos]) == CharClass::Space
                               && classOf(text[pos - 1]) != CharClass::Space))
        pos = charLeft(pos);
    const CharClass cls = classOf(text[pos]);
    size_t start = pos, end = pos;
    while (start > 0 && classOf(text[start - 1]) == cls)
        --start;
    while (end < text.size() && classOf(text[end]) == cls)
        ++end;
    anchor = start;
    cursor = end;
}

void TextEdit::setCursor(size_t pos, bool extend)
{
    cursor = pos;
    if (!extend)
        anchor = pos;
    clamp();
}

void TextEdit::deleteSelection()
{
    const size_t start = selectionStart();
    text.erase(start, selectionEnd() - start);
    cursor = anchor = start;
}

void TextEdit::insert(const std::string& str)
{
    std::string clean;
    clean.reserve(str.size());
    for (const char c : str)
    {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 32 && u != 127)
            clean += c;
    }
    deleteSelection();
    text.insert(cursor, clean);
    cursor = anchor = cursor + clean.size();
}

void TextEdit::moveLeft(bool word, bool extend)
{
    if (hasSelection() && !extend)
        cursor = word ? wordLeft(selectionStart()) : selectionStart();
    else
        cursor = word ? wordLeft(cursor) : charLeft(cursor);
    if (!extend)
        anchor = cursor;
}

void TextEdit::moveRight(bool word, bool extend)
{
    if (hasSelection() && !extend)
        cursor = word ? wordRight(selectionEnd()) : selectionEnd();
    else
        cursor = word ? wordRight(cursor) : charRight(cursor);
    if (!extend)
        anchor = cursor;
}

void TextEdit::moveHome(bool extend) { setCursor(0, extend); }

void TextEdit::moveEnd(bool extend) { setCursor(text.size(), extend); }

void TextEdit::backspace(bool word)
{
    if (!hasSelection())
        anchor = word ? wordLeft(cursor) : charLeft(cursor);
    deleteSelection();
}

void TextEdit::del(bool word)
{
    if (!hasSelection())
        anchor = word ? wordRight(cursor) : charRight(cursor);
    deleteSelection();
}

} // namespace namsayin
