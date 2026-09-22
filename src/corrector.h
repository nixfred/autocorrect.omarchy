// Pure decision logic: no fcitx5 here, so it can be unit tested.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace autocorrect {

struct Replacement {
    std::string text;       // what to type instead of the word
    bool isExpansion;       // an abbreviation (mha -> address), not a typo
};

class Corrector {
public:
    // "typo<TAB>fix" lines; '#' comments and blanks are ignored. Returns pairs loaded.
    size_t loadTypoMap(const std::string &path);
    // "abbrev<TAB>expansion" lines. Replaces any expansions already loaded.
    size_t loadExpansions(const std::string &path);
    // One word per line: never touch these (learned from Backspace undo).
    size_t loadNever(const std::string &path);

    void addNever(const std::string &word);
    bool isNever(const std::string &word) const;

    // What to replace `word` with, if anything. Expansions win over typos.
    // A typo fix follows the typed case: "Teh" -> "The", "TEH" -> "THE".
    std::optional<Replacement> lookup(const std::string &word) const;

    size_t typoCount() const { return typos_.size(); }
    size_t expansionCount() const { return expansions_.size(); }

private:
    std::unordered_map<std::string, std::string> typos_;
    std::unordered_map<std::string, std::string> expansions_;
    std::unordered_set<std::string> never_;
};

std::string toLowerAscii(std::string s);
std::string matchCase(const std::string &typed, std::string fix);

// A program where we must never act (terminals). Exact match, case-insensitive,
// or a prefix match for patterns of 5+ letters ("kitty" catches "kittyfloat"
// but "st" does not catch "steam"; that greedy prefix was a smartcomplete bug).
bool isDeniedProgram(const std::string &program,
                     const std::unordered_set<std::string> &extra = {});

} // namespace autocorrect
