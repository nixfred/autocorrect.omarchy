// fcitx5 module: fix typos and expand abbreviations as you type, in every app.
//
// Letters go to the app untouched; we only watch them. At a word boundary
// (space or punctuation) we look the word up, and on a hit we swallow the
// boundary key, delete the word through surrounding text, and commit the fix
// plus the boundary. Backspace straight after puts back exactly what was typed
// and remembers the word so it's never corrected again.
//
// Safety, in order: skip password/sensitive/terminal/no-spellcheck fields,
// skip denied programs, skip non-keyboard engines (their letters are preedit,
// not in the app), and do nothing unless the app's surrounding text proves the
// word is really there. Every handler fails open: on any error we let the key
// through untouched. We never log what was typed.

#include "corrector.h"

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace autocorrect {

FCITX_DEFINE_LOG_CATEGORY(acLog, "autocorrect");
#define AC_INFO() FCITX_LOGC(::autocorrect::acLog, Info)

namespace {

constexpr int kFastGapMs = 15;     // keys closer than this look injected
constexpr int kFastRunToTaint = 3; // this many in a row = dictation/paste
constexpr size_t kMaxWord = 48;

std::string xdgDir(const char *env, const char *fallback) {
    if (const char *v = std::getenv(env); v && *v) return v;
    const char *home = std::getenv("HOME");
    return std::string(home ? home : "") + "/" + fallback;
}

bool fileExists(const std::string &path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

long mtimeOf(const std::string &path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 ? static_cast<long>(st.st_mtime) : -1;
}

bool isLetter(fcitx::KeySym s) {
    return (s >= FcitxKey_a && s <= FcitxKey_z) || (s >= FcitxKey_A && s <= FcitxKey_Z);
}

bool isWordChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '\'' || c == '_' || c >= 0x80;
}

std::optional<std::string> boundaryText(fcitx::KeySym s) {
    switch (s) {
    case FcitxKey_space: return " ";
    case FcitxKey_period: return ".";
    case FcitxKey_comma: return ",";
    case FcitxKey_exclam: return "!";
    case FcitxKey_question: return "?";
    case FcitxKey_semicolon: return ";";
    case FcitxKey_colon: return ":";
    default: return std::nullopt;
    }
}

const fcitx::KeyStates kCommandMods = fcitx::KeyStates(fcitx::KeyState::Ctrl) |
                                      fcitx::KeyState::Alt | fcitx::KeyState::Super |
                                      fcitx::KeyState::Super2 | fcitx::KeyState::Hyper;

const fcitx::CapabilityFlags kHandsOff =
    fcitx::CapabilityFlags(fcitx::CapabilityFlag::PasswordOrSensitive) |
    fcitx::CapabilityFlag::Terminal | fcitx::CapabilityFlag::NoSpellCheck;

// The text before the cursor, or nullopt if we can't trust it.
std::optional<std::string_view> textBeforeCursor(fcitx::InputContext *ic) {
    const auto &st = ic->surroundingText();
    if (!st.isValid() || st.anchor() != st.cursor()) return std::nullopt;
    const auto &text = st.text();
    if (st.cursor() > fcitx::utf8::length(text)) return std::nullopt;
    auto bytes = fcitx::utf8::ncharByteLength(text.begin(), st.cursor());
    if (bytes < 0) return std::nullopt;
    return std::string_view(text.data(), static_cast<size_t>(bytes));
}

bool endsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

class AutoCorrect final : public fcitx::AddonInstance {
public:
    explicit AutoCorrect(fcitx::Instance *instance) : instance_(instance) {
        configDir_ = xdgDir("XDG_CONFIG_HOME", ".config") + "/autocorrect";
        reloadConfig();

        using fcitx::EventType;
        using fcitx::EventWatcherPhase;
        handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextKeyEvent, EventWatcherPhase::PreInputMethod,
            [this](fcitx::Event &e) { guarded([&] { onPre(static_cast<fcitx::KeyEvent &>(e)); }); }));
        handlers_.emplace_back(instance_->watchEvent(
            EventType::InputContextKeyEvent, EventWatcherPhase::PostInputMethod,
            [this](fcitx::Event &e) { guarded([&] { onPost(static_cast<fcitx::KeyEvent &>(e)); }); }));
        for (auto type : {EventType::InputContextFocusIn, EventType::InputContextFocusOut,
                          EventType::InputContextReset,
                          EventType::InputContextSwitchInputMethod}) {
            handlers_.emplace_back(instance_->watchEvent(
                type, EventWatcherPhase::Default, [this](fcitx::Event &) { reset(); }));
        }
    }

    void reloadConfig() override {
        corrector_ = Corrector{};
        const std::string userMap =
            xdgDir("XDG_DATA_HOME", ".local/share") + "/autocorrect.omarchy/typomap.tsv";
        size_t typos = corrector_.loadTypoMap(userMap);
        if (typos == 0) typos = corrector_.loadTypoMap("/usr/share/autocorrect.omarchy/typomap.tsv");
        corrector_.loadNever(configDir_ + "/never");
        expansionsMtime_ = -2;
        maybeReloadExpansions();

        deny_.clear();
        std::ifstream in(configDir_ + "/deny");
        for (std::string line; std::getline(in, line);)
            if (!line.empty() && line[0] != '#') deny_.insert(line);

        AC_INFO() << "loaded " << typos << " typo fixes, " << corrector_.expansionCount()
                  << " expansions, " << deny_.size() << " extra denied programs";
    }

private:
    template <typename Fn>
    void guarded(Fn fn) {
        try {
            fn();
        } catch (...) {
            reset(); // fail open: the key goes through as if we weren't here
        }
    }

    bool allowed(fcitx::InputContext *ic) {
        if (ic->capabilityFlags().testAny(kHandsOff)) return false;
        if (isDeniedProgram(ic->program(), deny_)) return false;
        // Other engines hold letters in preedit, so the app doesn't have them.
        return instance_->inputMethod(ic).rfind("keyboard-", 0) == 0;
    }

    void onPre(fcitx::KeyEvent &ev) {
        if (ev.isRelease()) return;
        auto *ic = ev.inputContext();
        const auto key = ev.key();
        if (!allowed(ic) || key.states().testAny(kCommandMods)) {
            reset();
            return;
        }
        if (undo_) {
            bool undone = key.sym() == FcitxKey_BackSpace && undo(ic);
            undo_.reset(); // any key closes the window
            if (undone) {
                ev.filterAndAccept();
                return;
            }
        }
        if (auto boundary = boundaryText(key.sym()); boundary && replace(ic, *boundary))
            ev.filterAndAccept();
    }

    // Only keys the engine let through reach here, so these letters are in the app.
    void onPost(fcitx::KeyEvent &ev) {
        if (ev.isRelease()) return;
        const auto key = ev.key();
        if (key.isModifier()) return;
        if (key.states().testAny(kCommandMods)) {
            resetWord();
            return;
        }
        const auto sym = key.sym();
        if (isLetter(sym) || (sym == FcitxKey_apostrophe && !word_.empty())) {
            trackTiming(ev.time());
            word_ += static_cast<char>(sym);
            if (word_.size() > kMaxWord) tainted_ = true;
        } else if (sym == FcitxKey_BackSpace) {
            if (!word_.empty()) word_.pop_back();
        } else {
            resetWord();
        }
    }

    void trackTiming(int now) {
        if (lastKeyTime_ != 0 && now >= lastKeyTime_ && now - lastKeyTime_ < kFastGapMs) {
            if (++fastRun_ >= kFastRunToTaint) tainted_ = true;
        } else {
            fastRun_ = 0;
        }
        lastKeyTime_ = now;
    }

    bool replace(fcitx::InputContext *ic, const std::string &boundary) {
        if (word_.empty() || tainted_ || fileExists(configDir_ + "/off")) return false;
        maybeReloadExpansions();
        auto rep = corrector_.lookup(word_);
        if (!rep) return false;

        auto before = textBeforeCursor(ic);
        if (!before) return skip(ic, "no surrounding text");
        if (!endsWith(*before, word_)) return skip(ic, "text before cursor differs");
        if (before->size() > word_.size() &&
            isWordChar(static_cast<unsigned char>((*before)[before->size() - word_.size() - 1])))
            return skip(ic, "word starts mid-word");

        ic->deleteSurroundingText(-static_cast<int>(word_.size()), word_.size());
        ic->commitString(rep->text + boundary);
        undo_ = Undo{word_, rep->text + boundary, boundary, rep->isExpansion};
        AC_INFO() << (rep->isExpansion ? "expanded" : "corrected") << " a word in "
                  << ic->program();
        resetWord();
        return true;
    }

    bool undo(fcitx::InputContext *ic) {
        auto before = textBeforeCursor(ic);
        if (!before || !endsWith(*before, undo_->inserted)) return false;
        const auto n = fcitx::utf8::length(undo_->inserted);
        ic->deleteSurroundingText(-static_cast<int>(n), n);
        ic->commitString(undo_->original + undo_->boundary);
        if (!undo_->isExpansion) {
            corrector_.addNever(undo_->original);
            std::ofstream(configDir_ + "/never", std::ios::app) << toLowerAscii(undo_->original) << "\n";
        }
        AC_INFO() << "undone in " << ic->program();
        return true;
    }

    bool skip(fcitx::InputContext *ic, const char *why) {
        AC_INFO() << "skipped a fix in " << ic->program() << ": " << why;
        return false;
    }

    void maybeReloadExpansions() {
        const auto path = configDir_ + "/expansions";
        const long m = mtimeOf(path);
        if (m == expansionsMtime_) return;
        expansionsMtime_ = m;
        corrector_.loadExpansions(path);
    }

    void resetWord() {
        word_.clear();
        tainted_ = false;
        fastRun_ = 0;
    }

    void reset() {
        resetWord();
        undo_.reset();
    }

    struct Undo {
        std::string original;  // what was typed
        std::string inserted;  // what we committed (fix + boundary)
        std::string boundary;
        bool isExpansion;
    };

    fcitx::Instance *instance_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>> handlers_;
    Corrector corrector_;
    std::unordered_set<std::string> deny_;
    std::string configDir_;
    long expansionsMtime_ = -2;

    std::string word_;
    bool tainted_ = false;
    int fastRun_ = 0;
    int lastKeyTime_ = 0;
    std::optional<Undo> undo_;
};

class AutoCorrectFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
        return new AutoCorrect(manager->instance());
    }
};

} // namespace autocorrect

FCITX_ADDON_FACTORY_V2(autocorrect, autocorrect::AutoCorrectFactory);
