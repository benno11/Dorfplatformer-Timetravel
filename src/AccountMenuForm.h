#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

namespace AccountMenu {
using Login = std::function<bool(const std::string&, const std::string&, std::string&)>;
enum class Field { None, Email, Password };

struct TextEntry {
    std::string value;
    size_t cursor = 0;
    bool selected = false;
    static size_t previous(const std::string& text, size_t pos) {
        pos = std::min(pos, text.size());
        if (pos) --pos;
        while (pos && ((unsigned char)text[pos] & 0xc0) == 0x80) --pos;
        return pos;
    }
    static size_t next(const std::string& text, size_t pos) {
        if (pos >= text.size()) return text.size();
        ++pos;
        while (pos < text.size() && ((unsigned char)text[pos] & 0xc0) == 0x80) ++pos;
        return pos;
    }
    void clear() { value.clear(); cursor = 0; selected = false; }
    void insert(std::string text, size_t limit) {
        text.erase(std::remove_if(text.begin(), text.end(), [](char c) { return c == '\r' || c == '\n' || c == '\0'; }), text.end());
        if (text.empty()) return;
        if (selected) clear();
        cursor = std::min(cursor, value.size());
        size_t count = std::min(text.size(), limit - std::min(limit, value.size()));
        while (count < text.size() && count > 0 && ((unsigned char)text[count] & 0xc0) == 0x80) --count;
        value.insert(cursor, text, 0, count);
        cursor += count;
    }
    void backspace() {
        if (selected) { clear(); return; }
        const size_t start = previous(value, cursor);
        value.erase(start, cursor - start);
        cursor = start;
    }
    void eraseForward() {
        if (selected) { clear(); return; }
        value.erase(cursor, next(value, cursor) - cursor);
    }
    void move(int direction) { cursor = direction < 0 ? previous(value, cursor) : next(value, cursor); selected = false; }
};

struct Form {
    TextEntry email, password;
    Field editing = Field::None;
    TextEntry& active() { return editing == Field::Password ? password : email; }
    void insert(const std::string& text) {
        if (editing != Field::None) active().insert(text, editing == Field::Password ? 256 : 254);
    }
    std::string display(Field field) const {
        const auto& entry = field == Field::Password ? password : email;
        std::string shown;
        size_t caret = 0;
        for (size_t i = 0; i < entry.value.size();) {
            const auto end = TextEntry::next(entry.value, i);
            if (i < entry.cursor) caret = shown.size() + (field == Field::Password ? 1 : end - i);
            shown += field == Field::Password ? "*" : entry.value.substr(i, end - i);
            i = end;
        }
        if (editing == field) shown.insert(caret, "|");
        if (shown.empty()) return "<empty>";
        // Keep long credentials inside small menu buttons; follow the insertion point.
        size_t start = caret > 12 ? caret - 12 : 0;
        while (start && ((unsigned char)shown[start] & 0xc0) == 0x80) --start;
        size_t end = std::min(shown.size(), start + 16);
        while (end < shown.size() && ((unsigned char)shown[end] & 0xc0) == 0x80) --end;
        return (start ? "..." : "") + shown.substr(start, end - start) + (shown.size() > end ? "..." : "");
    }
    bool submit(const Login& login, std::string& error) {
        error.clear();
        auto address = email.value;
        const auto first = address.find_first_not_of(" \t\r\n");
        address = first == std::string::npos ? "" : address.substr(first, address.find_last_not_of(" \t\r\n") - first + 1);
        if (address.empty() || password.value.empty()) { error = "Email and password are required."; return false; }
        if (!login) { error = "Sign-in is unavailable in this build."; return false; }
        if (!login(address, password.value, error)) {
            if (error.empty()) error = "Sign-in failed. Please try again.";
            return false;
        }
        password.clear();
        editing = Field::None;
        return true;
    }
};
} // namespace AccountMenu
