#include "corrector.h"

#include <fstream>
#include <string_view>

namespace autocorrect {

namespace {

// Calls fn(key, value) for each "key<TAB>value" line.
template <typename Fn>
size_t readTsv(const std::string &path, Fn fn) {
    std::ifstream in(path);
    size_t n = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || tab + 1 == line.size()) continue;
        fn(line.substr(0, tab), line.substr(tab + 1));
        ++n;
    }
    return n;
}

bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }

} // namespace

std::string toLowerAscii(std::string s) {
    for (auto &c : s)
        if (isUpper(c)) c = static_cast<char>(c + 32);
    return s;
}

std::string matchCase(const std::string &typed, std::string fix) {
    if (typed.empty() || fix.empty()) return fix;
    size_t letters = 0, upper = 0;
    for (char c : typed) {
        if (isUpper(c) || isLower(c)) ++letters;
        if (isUpper(c)) ++upper;
    }
    if (letters > 1 && upper == letters) {
        for (auto &c : fix)
            if (isLower(c)) c = static_cast<char>(c - 32);
    } else if (isUpper(typed[0]) && isLower(fix[0])) {
        fix[0] = static_cast<char>(fix[0] - 32);
    }
    return fix;
}

size_t Corrector::loadTypoMap(const std::string &path) {
    return readTsv(path, [this](std::string k, std::string v) {
        typos_[toLowerAscii(std::move(k))] = std::move(v);
    });
}

size_t Corrector::loadExpansions(const std::string &path) {
    expansions_.clear();
    return readTsv(path, [this](std::string k, std::string v) {
        expansions_[toLowerAscii(std::move(k))] = std::move(v);
    });
}

size_t Corrector::loadNever(const std::string &path) {
    std::ifstream in(path);
    size_t n = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        never_.insert(toLowerAscii(line));
        ++n;
    }
    return n;
}

void Corrector::addNever(const std::string &word) { never_.insert(toLowerAscii(word)); }

bool Corrector::isNever(const std::string &word) const {
    return never_.count(toLowerAscii(word)) > 0;
}

std::optional<Replacement> Corrector::lookup(const std::string &word) const {
    if (word.empty()) return std::nullopt;
    const auto key = toLowerAscii(word);
    if (auto it = expansions_.find(key); it != expansions_.end())
        return Replacement{it->second, true};
    if (never_.count(key)) return std::nullopt;
    if (auto it = typos_.find(key); it != typos_.end())
        return Replacement{matchCase(word, it->second), false};
    return std::nullopt;
}

bool isDeniedProgram(const std::string &program,
                     const std::unordered_set<std::string> &extra) {
    if (program.empty()) return false;
    // Terminal list from smartcomplete (MIT, see third_party/smartcomplete),
    // plus the Wayland app_ids for vic's terminals.
    static constexpr std::string_view builtin[] = {
        "kitty", "alacritty", "foot", "footclient", "wezterm", "wezterm-gui",
        "org.wezfurlong.wezterm", "com.mitchellh.ghostty", "ghostty",
        "xterm", "urxvt", "rxvt", "st", "st-256color",
        "gnome-terminal", "gnome-terminal-server", "org.gnome.terminal",
        "org.gnome.ptyxis", "ptyxis", "org.kde.konsole", "konsole",
        "xfce4-terminal", "lxterminal", "mate-terminal", "deepin-terminal",
        "terminator", "tilix", "com.gexperts.tilix", "hyper", "terminology",
        "blackbox", "cool-retro-term", "termite",
    };
    const auto prog = toLowerAscii(program);
    auto matches = [&prog](std::string_view pat) {
        if (pat.empty()) return false;
        if (prog == pat) return true;
        return pat.size() >= 5 && prog.compare(0, pat.size(), pat) == 0;
    };
    for (auto pat : builtin)
        if (matches(pat)) return true;
    for (const auto &pat : extra)
        if (matches(toLowerAscii(pat))) return true;
    return false;
}

} // namespace autocorrect
