# Cherry-picked from smartcomplete

- Source: https://github.com/ekremx25/smartcomplete @ `b14aed4` (2026-04-18)
- Licence: MIT, (c) 2026 ekremx25. See `LICENSE` in this folder. Keep it with anything derived.
- Picked 2026-09-21 by Larry. Files are copied **verbatim** unless marked EXCERPT.

## What we took and why

| Here | Upstream | Use |
|---|---|---|
| `src/text_utils.{h,cpp}` | `src/predictor/text_utils.*` | `match_case_to_buffer` (keep "Teh" → "The" case), `to_lower_ascii`, and `normalize_contraction_typo` ("dont't" → "don't"). Pure functions, reusable as-is. |
| `src/blocklist.excerpt.cpp` | `predictor.h:38-52`, `predictor.cpp:633-645` | Default terminal deny-list and the `program()` matcher. |
| `src/engine.cpp` | `src/engine/engine.cpp` | **Reference only, not compiled.** `apply_space_autocorrect()` (bottom of file) is the replace move we want: `deleteSurroundingText(-n, n)` + `commitString(fix + " ")`, gated on `surroundingText().isValid()`. Also `is_letter_key`. |
| `tests/blocklist_test.cpp` | `tests/blocklist_test.cpp` | Test shape for our deny-list. |
| `/data/dict/en_US.txt` | `data/dict/en_US.txt` | 74,141 lowercase words. This is our **"is it a real word?" guard**: never auto-change a word in here. |
| `/data/sources/smartcomplete_en_typo_map.txt` | `data/rules/en_typo_map.txt` | 275 typo→fix pairs. **Raw input only.** It must go through the merge filter before use (see below). |

## What we did NOT take, and why

- **The engine design itself.** It replaces keyboard-us and holds every letter in preedit
  (underlined text), which changes how typing looks in every app. We watch keys as a module instead.
- `learn_word()` / `user_freq.txt` / `learned_bigrams.txt`: these **write typed words to disk**,
  which our no-keystroke-logging rule forbids.
- `ai_reranker.*`: sends context to OpenAI. Never.
- The "merge" contraction rules in `detect_space_autocorrect()` ("I am" → "I'm",
  "you are" → "you're", "it is" → "it's"). They change style, not typos. Not ported.
- Bigrams, phrases, grammar rules, emoji, trie prediction: those are autocomplete, not autocorrect.
  The bigrams could rank ambiguous fixes later. Revisit then; they're easy to fetch.

## Problems found in what we took (fix when porting)

1. **The typo map has dangerous keys.** 25 of its 275 keys are real dictionary words. Examples:
   `advise→advice`, `mount→month`, `whit→with`, `seize→seize`, `license→license`, and slang
   expansions like `bc→because`, `ur→your`, `rn→right now`, `tho→though`. Auto-applied, they would
   wreck correct text. The merge step must drop any key that is in `en_US.txt` and any identity
   pair. Find them all with:
   `awk -F'\t' 'NR==FNR{d[$1];next} $1 in d' data/dict/en_US.txt data/sources/smartcomplete_en_typo_map.txt`
2. **The blocklist prefix match is too greedy.** `"st"` blocks every app starting with "st"
   (Steam, Strata…), and `"screen"` blocks anything starting with "screen". Use exact match for
   short names and prefix match only for long ones.
3. **The blocklist misses vic's terminals.** Ghostty (`com.mitchellh.ghostty`) is installed on vic
   but not listed. Add it, plus editors where code lives (code, zed, nvim-qt…) as a choice for Fred.
   The capability flags (Terminal/Password/Sensitive) are the first guard anyway; the list is backup.
4. `en_US.txt` has **no apostrophes** (0 lines), so contractions ("don't") aren't "real words" to
   it, and it's all lowercase with no proper nouns. Add a contractions list and Fred's personal
   dictionary on top.
5. `en_US.txt` provenance: shipped under the repo's MIT licence, but upstream doesn't say where the
   list came from (it looks SCOWL-like). **Unverified.** Fine for private use; check it before the
   repo goes public.
