# autocorrect.omarchy — global, real-time typo fixing on vic

> Identity and global rules: `~/.claude/CLAUDE.md` (Larry, 9 Laws, Constitution).
> Machine context: `~/Projects/CLAUDE.md` (vic, RTX 4050, Ollama).
> This file is the project brief. Born 2026-09-21 from Fred asking: *"would it be
> possible to have you automatically spell check and fix my typing real time
> globally with a quick 'no I meant to type that wrong' button?"*

**Status: nothing built yet.** No code, no research done beyond the state survey
below. `autocorrect.omarchy` is a working name; Fred names things (Pulse, Halo,
Reel, Swish), so ask him before it goes public or gets a plugin id.

---

## The goal

1. Fix typos **as Fred types, in every app**, at word boundaries (space, punctuation, Enter).
2. **Undo = Backspace.** Pressing Backspace *immediately* after a correction restores
   exactly what he typed (the macOS model). That word then goes on a
   never-correct-again list, so the fix is learned for good.
3. Optionally show it: a small toast or bar flash, e.g. `speel → spell  (⌫ undo)`.
4. A clear **ON/OFF switch** in the bar, like `larry-voice`. Fred likes distinct switches.

## The decisions already made (from the design conversation, do not re-litigate)

- **Claude is NOT in the per-keystroke loop.** There are three reasons: latency
  (a 300ms+ cloud round-trip on every word), privacy (a global hook sees passwords,
  SSH sessions and private messages, which must never leave vic), and token cost.
- **Tier 1: a dictionary plus a typo map.** It's sub-millisecond, deterministic and
  does most of the work.
- **Tier 2 (optional, later): the local `gpu` model** (Ollama on the 4050) only for
  words tier 1 can't settle. It runs on vic only. Honour `~/.claude/LOCAL-GPU-POLICY.md`.
- **The hook point is fcitx5.** It is already vic's input method, and on Wayland
  it's the one legitimate layer that sees and can rewrite text in every app.
  keyd/evdev-level hacks can't know the field type or the surrounding text.

## Hard guardrails (these are what keep it from being dangerous)

- **Never act in password fields.** fcitx5 gets content-purpose/hints from
  text-input-v3 (`CapabilityFlag::Password`, `Sensitive`). Bail out on those.
- **Never act in terminals by default** (kitty, anything with terminal purpose).
  Autocorrecting a shell command is how `rm` hits the wrong thing. Use a per-app
  allow/deny list and deny terminals by default.
- **Never correct** words in Fred's personal dictionary: code identifiers, hostnames
  (`vic`, `gus`, `blu`, `fnix`), `omarchy`, names, anything he's undone before.
- **Don't break Compose.** Omarchy uses fcitx5 to turn CapsLock compose sequences in
  `~/.XCompose` into text (see the comment in `omarchy-fcitx5.service`). Test that
  after every change.
- **Don't mangle dictation or pastes.** Voxtype/Moon-key dictation types through
  wtype/ydotool. Verify whether those go through the IM keyboard grab. If they
  do, detect bulk input and leave it alone.
- **Never log keystrokes to disk.** The only persisted data is the learned-words
  list and the typo map. There's no telemetry of what he typed.
- **Fail open.** If the addon crashes or hangs, typing must still work. A wedged
  input method means a dead keyboard.

---

## vic state survey (verified 2026-09-21, by Larry, with tools)

| Thing | State |
|---|---|
| fcitx5 | **5.1.22**, running (`/usr/bin/fcitx5 --disable notificationitem`) |
| How it's started | systemd user unit **`omarchy-fcitx5.service`** (`/usr/lib/systemd/user/`). Restart: `systemctl --user restart omarchy-fcitx5` |
| IM env | `INPUT_METHOD=fcitx`, `QT_IM_MODULE=fcitx`, `XMODIFIERS=@im=fcitx`, `SDL_IM_MODULE=fcitx`. `GTK_IM_MODULE` is **unset** (GTK uses native Wayland text-input) |
| Profile | a single group, `keyboard-us`. Config is in `~/.config/fcitx5/` |
| fcitx5 addons installed | `/usr/lib/fcitx5/`: includes **`libspell.so`** (built-in spell module), `libquickphrase.so`, `libwaylandim.so`, `libclipboard.so`, `libemoji.so`, etc. |
| fcitx5 spell data | `/usr/share/fcitx5/spell/en_dict.fscd`, the spell module's own English dictionary |
| Dev headers | **present**: `/usr/include/Fcitx5/{Config,Core,Module,Utils,GClient}`. `cmake` installed. `extra-cmake-modules` NOT checked |
| fcitx5-lua | **not installed**, available in `extra` (5.0.18). It would allow a Lua addon and skip C++ |
| hunspell | binary installed, **no dictionaries** (`hunspell-en_us` in `extra`, not installed) |
| espanso | not installed |
| Also on vic | `keyd`, `wtype`, `ydotool` |
| kitty | `listen_on unix:${XDG_RUNTIME_DIR}/omarchy-kitty-{kitty_pid}`; nothing IME-specific set |

Prior art in Larry's history: **none.** `git log --grep` for
autocorrect/spell/espanso/fcitx found nothing, and neither did the memory grep.

---

## Open questions, in the order to answer them

1. **Does it already exist?** Before writing a line, per Law 7: research fcitx5
   autocorrect addons, fcitx5-lua autocorrect scripts, IBus/Typing Booster (it has
   autocorrect-ish behaviour, but it's IBus), espanso typo packages and their
   Wayland status, and anything on GitHub/X (`x research "fcitx5 autocorrect"`).
   Report back to Fred before building.
2. **What can the built-in `libspell` do?** It already powers hints for the keyboard
   IM. Can the `keyboard-us` engine's spell hinting be turned into auto-replace
   through config alone? Read the fcitx5 source (`src/modules/spell`,
   `src/im/keyboard`).
3. **Lua or C++?** fcitx5-lua is faster to write and hot-reloads. C++ gets full
   `InputContext` access (surrounding text, capability flags,
   `deleteSurroundingText`). Check whether the Lua API exposes what we need:
   commit hooks, surrounding text and capability flags.
4. **How does replacement work mechanically?** The likely route is: on a word
   boundary, read surrounding text, then `deleteSurroundingText(-len, len)` and
   `commitString(fixed + boundary)`. Not every app supports surrounding text
   (Electron and some terminals don't), so find the fallback (forward N
   BackSpace key events?) or skip those apps.
5. **Does the undo window close on the next keystroke?** Presumably yes: any key other
   than Backspace finalizes it.
6. **Where does the UI live?** Maybe an Omarchy bar plugin (`nixfred.<name>`) for the
   toggle and a recent-fixes list, talking to the addon over a file or D-Bus.
   Follow existing plugin conventions (see below).

## Conventions to follow

- **Omarchy plugins:** look at `~/Projects/power.omarchy` (`manifest.json`,
  `Panel.qml`, `Model.js`, `check`, `test/`) and the live
  `~/.config/omarchy/plugins/nixfred.*`. Memory has hard-won gotchas. Read
  these before touching QML:
  - `~/.claude/MEMORY/AUTO/omarchy-plugin-edit-out-of-tree.md`: develop here, deploy by copy
  - `omarchy-plugin-runtime-python-not-bun.md`: plugin helpers use python3, not bun
  - `omarchy-plugin-hot-reload-zombie-ipc.md`: deploy with a restart
  - `plugin-panels-never-scroll-pro-density.md`, `omarchy-chamfer-is-the-brand.md`
  - The QML gotchas list in `~/.claude/MEMORY/AUTO/MEMORY.md`
- **Shell restart:** `omarchy restart shell`. **NEVER `omarchy refresh`**, which is a
  factory reset. Never restore `shell.json` wholesale (see `~/Projects/CLAUDE.md`).
- **Stack:** C++ or Lua for the fcitx5 addon (dictated by fcitx5), python3 for plugin
  helpers, TypeScript/bun for anything else standalone.
- **Package installs** (`fcitx5-lua`, `hunspell-en_us`) are low-risk, but say so before
  running `pacman`.
- **Git:** `git init` done locally. **No remote yet.** Before any `gh repo create`,
  ask Fred public vs private. His plugins are usually PUBLIC under `nixfred/`, so
  sanitize before the first push (Law 11).

## Test plan skeleton

- Unit: the typo map and dictionary decisions (pure functions, no fcitx5).
- Live: `fcitx5-diagnose`, a throwaway text field, and then each app class: GTK
  (a gnome-text-editor-type app), Qt, Chromium/Brave (Electron), and kitty
  (must be untouched).
- Safety: a password field in Brave (must be untouched), the compose sequence
  still works, dictation via the Moon key doesn't get "corrected", and after
  `systemctl --user stop omarchy-fcitx5` the keyboard still types.
