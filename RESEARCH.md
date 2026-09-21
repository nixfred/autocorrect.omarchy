# Research: does it already exist, and how would we build it? (2026-09-21)

Two research passes by Larry, plus one X search. Source clones (fcitx5 at tag 5.1.22,
fcitx5-lua, Hyprland, kitty) were read in the session scratchpad. [V] = read in the
source or docs. [I] = inferred.

## Verdict: build it as a C++ fcitx5 module, borrowing from smartcomplete

Nothing open source does this on vic today.

| Candidate | What it is | Verdict |
|---|---|---|
| fcitx5 built-in spell (`keyboard-us` hints) | Suggestions only, held in preedit, no auto-replace option [V] | leave it off |
| fcitx5-lua | No deleteSurroundingText, no capability flags [V] | can't do it |
| **ekremx25/smartcomplete** (MIT, fcitx5 C++ engine) | Corrects on Space by typo list + edit distance; `apply_space_autocorrect()` = deleteSurroundingText + commitString; denies 27 terminals | **borrow from**. But it's preedit-based, has no Backspace-undo, and **writes typed words to disk** |
| Mekhanic/smarttype | Exactly our spec (Backspace-undo, password/terminal off) | **proprietary, closed binary, curl\|bash, no Arch**. It proves the design works; never install it |
| IBus Typing Booster | Real autocorrect (`autoselectcandidate`) | IBus-only, preedit, saves typed text. It would mean swapping vic's IM |
| espanso | evdev read + uinput write | field-blind (no password awareness), undo_backspace is flaky, slow on Wayland (#2766) |
| AutoKey / autocorrect-linux / pynput tools | X11 only | irrelevant |
| lemmebee/frfix | evdev + wtype, French only | field-blind, reference only |

X search (`x research`, $0.045): no relevant posts in the last 7 days.

## Architecture (from reading fcitx5 5.1.22)

- A **module** watching `EventType::InputContextKeyEvent` in the `PreInputMethod`
  phase sees every key for any engine (`instance.cpp:1651-1676`). Template:
  `src/modules/imselector/` (83 lines).
- Recommended: module, not engine. It keeps `keyboard-us` (and its XCompose handling) untouched.
  The one cost is doing the password check ourselves, which is a single flag test.
- With hints off (vic now), letters aren't committed via CommitString; they go to the app
  as virtual-keyboard keys. The module must rebuild words from key events [V].
- **Replace at a boundary:** filter the boundary key, then `deleteSurroundingText(-n,n)` if
  `surroundingText().isValid()`, else forward n BackSpace. Then `commitString(fix+boundary)`.
  Upstream long-press uses the same fallback (`keyboard.cpp:745-750`).
  - Trap: input-method-v2 ALWAYS advertises the SurroundingText capability
    (`waylandimserverv2.cpp:44-46`). Test `isValid()`, not the flag.
  - Risk [I]: forwarded BackSpace (virtual keyboard) and commit (text-input) are separate
    channels, so ordering isn't guaranteed. The PoC must test this.
- **Guards** (`capabilityflags.h`): `PasswordOrSensitive`, `Terminal`, `NoSpellCheck`.
  text-input-v3 HIDDEN_TEXT/PASSWORD/PIN → Password; purpose TERMINAL → Terminal
  (`waylandimserverv2.cpp:329-404`). kitty sets PURPOSE_TERMINAL [V]. Also use an app
  deny-list via `InputContext::program()` (Wayland app_id).
- **Dictation:** wtype and ydotool both go through fcitx5's keyboard grab
  (`Hyprland InputManager.cpp:1823-1835`) [V/I]. Detect bursts by the gap between keys
  (a few ms injected vs ~50ms+ human) and pause.
- **Fail open:** the watcher runs inside the fcitx5 process. Keep it tiny and non-blocking;
  tier 2 (gpu) must be async.
- Build out-of-tree: `find_package(Fcitx5Core)`, `find_package(Fcitx5Module COMPONENTS Spell)`.
  ECM isn't installed and isn't needed.

## vic gap: Brave doesn't use the IME

`~/.config/brave-flags.conf` and `chromium-flags.conf` lack `--enable-wayland-ime`,
so browser text never reaches fcitx5 [V: flags + running process args]. Add the flag,
restart Brave, and re-test Compose there.

## Tier-1 data

| Dataset | Size | Licence |
|---|---|---|
| AutoCorrect.ahk (Biancolo) | ~4.7k, ambiguous entries pre-commented | CC BY-SA [I] |
| Wikipedia "common misspellings / For machines" | ~4.3k | CC BY-SA [I] |
| codespell dictionaries | large, code-oriented | CC BY-SA 3.0 [V] |
| espanso typofixer-en | 18k words | GPL-3.0 [V] |

## Links

smartcomplete https://github.com/ekremx25/smartcomplete ·
smarttype https://github.com/Mekhanic/smarttype ·
typing-booster https://github.com/mike-fabian/ibus-typing-booster ·
espanso https://github.com/espanso/espanso · typofixer https://github.com/Mte90/espanso-typofixer ·
fcitx5 #1676 (stale password flags on Wayland) https://github.com/fcitx/fcitx5/issues/1676
