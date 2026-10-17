# FuzzyFind: design

*Started 2026-10-17 with the user. Proposed, being built. The user chose the names (FuzzyFind,
`fzf`), a repo of its own, public.*

## Start here

FuzzyFind finds anything on the Amiga by a few letters: a catalogue (the **index**) of every file and
drawer on the local disks, built by `updatedb`; `locate` searches it; `fzf` is the interactive
finder for the terminal (type and the list narrows, best match on top). The matching and the
index are in **fuzzy.library** (AmigaLibs/fuzzy), so Ahiru, csh, Sumi or a launcher can use the
same catalogue and the same matching without running a command.

| Piece | Where | What |
|---|---|---|
| fuzzy.library | `../AmigaLibs/fuzzy` | matching and ranking; reading and writing the index |
| `updatedb` | this repo | builds the index of the chosen volumes |
| `locate` | this repo | prints the paths that match, plainly or fuzzily |
| `fzf` | this repo | the interactive finder (tui.library) |

## Why (talked through 2026-10-17)

- Aminet has no fuzzy finder at all, and only old `locate`s (GNU findutils 4.1 with ixemul,
  fdb 1.4 from 1989-92, dblocate): none fuzzy, none with UTF-8 or long paths, none in the
  background, none a library others can read.
- **Old disks:** matching never touches the disk; the index does. On FFS each file's header is a
  block of its own, so a full scan of an old drive is thousands of seeks (how long on a real
  mechanical drive or CF card: not measured). On the user's A1200 (PiStorm, SD card) listing all
  21,028 paths of its four local volumes took ~11 s through Wasabi (2026-10-17) - but `List ALL`
  had stopped early on Dump: ("object not found", rc 20, unnoticed): the disks hold 57,028 entries,
  as updatedb found later. The 21,028-path list stayed the test list for ranking and timing.
- **More than search:** with size, date and type the index is a catalogue: "what changed since
  last night" (compare two indexes), disk usage at a glance, duplicates, every `.mod` for a player,
  a launcher (type "dpai", Deluxe Paint starts; the Sprawl idea), and Claude asking "where is X on
  the A1200" through Wasabi.

## The index

- **One file**, default `S:FuzzyFind.index` (not in ENVARC: - ENVARC: is copied to RAM at every
  boot). The setting `FuzzyFind/Index` in ENV: can name another place.
- **Written whole and renamed into place** (`.new` then Rename), so a reader never sees half an
  index, and `updatedb` running while `fzf` reads is safe.
- **Sorted paths, front-coded** as Unix `locate` does: each entry stores how many bytes it
  shares with the path before it and then the rest. 21,028 paths of the A1200: 966 KB as a
  plain list, 226 KB front-coded (computed 2026-10-17). With size, date and type, ~400 KB
  (estimated).
- **Entry:** shared-prefix length, rest of the path, type (file / drawer / link), size, date
  (Amiga seconds since 1978-01-01, so no time-zone guessing), protection bits. Numbers as
  variable-length bytes (small numbers one byte).
- **Header:** `FFIX`, format version, when it was made, how many entries, the volumes indexed
  (name and the date of the volume, so a reader knows the disk is the same one).
- Paths as the file system gives them: Latin-1. Volume names, not device names (`Work:` not
  `DH1:`), so the index survives a changed mount order.
- **Format version 1** is documented in fuzzy.library's guide; readers refuse a version they do
  not know.

## Matching

- **fzf's "v1" way:** find the letters of the pattern in order left to right, then from the last
  one backwards to the shortest window, then score that window. One pass, no tables: fast on a
  68000 (fzf's v2 is Smith-Waterman, O(n*m), too heavy here).
- **Score:** a point per matched letter; bonuses for a letter at the start of a word (after
  `/ : _ - . space`, or a capital after a small letter), for letters in a row, for matches in the
  file name rather than the drawers above it; a small penalty for gaps. Higher is better; 0 is no
  match. Ties: shorter path first.
- **Letters:** Latin-1, case does not matter, and accents do not matter by default (`goteborg`
  finds `Göteborg`), as fzf normalizes; a flag makes it exact.
- **Several words** (space between) must all match, in any order, as in fzf. Later, fzf's extended
  syntax: `'exact`, `^start`, `end$`, `!not`.
- **Where the letters matched** can be asked for, for highlighting.
- No floating point; no multiply or divide in the loops.

## The commands

- `updatedb [VOLUMES...] [QUIET]`: without volumes, the ones in the setting `FuzzyFind/Volumes`
  (ENV:), else every mounted volume except `RAM Disk:` and network volumes. Uses ExAll (one call
  per drawer, the size, date and type with it). Skips soft links into drawers (they loop). Meant to
  run at boot or at night: `Run >NIL: updatedb QUIET` at low priority.
- `locate PATTERN [FUZZY] [LIMIT n] [FILES|DRAWERS] [INDEX file]`: plain (any case, part of the
  path) or fuzzy (best first); prints paths, one a line, for scripts and pipes.
- `fzf [QUERY] [INDEX]`: a list from a pipe (`List ... LFORMAT %p%n | fzf`), else the index.
  Type to narrow, Up/Down (and Ctrl+J/K, the wheel, a click) to move, Enter prints the choice and
  ends (return code 0), Esc ends without one (5). All keys configurable (the user's rule).
  Draws on the console even when its output is captured (`Echo \`fzf\``), which needs
  tui.library to draw on a console other than Output(): a new call `TuiOpenIO(in, out)` at the
  end of tui's table (0.4).

## Rules

- 68000 and up, AmigaOS 2.04+ (ExAll); three builds 000/020/060 (no floating point).
- The library gets a guide and a reference (Markdown); this repo a README with every feature.
- Keys configurable; 80 columns first; no private paths in the repo.

## State

- **0.1b1, 2026-10-17:** fuzzy.library 0.1b1 (AmigaLibs/fuzzy), updatedb, locate, fzf, and
  tui.library 0.4b1 (TuiOpenIO) built and tested in FS-UAE: A1200 test machine (68020, 3.2.1) in a
  Shell window and in CTerm (typing, keys, double click, a pipe, backquotes, Esc), A600 (68000,
  2.05). Ranking equal to fzf 0.74's top five on a real A1200's 21,028 paths.
- **Speed** (21,028 paths, one search; FS-UAE, the index in RAM:, so the processor's share only,
  not a real drive): 68000 7 MHz chip-only 14-28 s, 68020 14 MHz + fast 3-6 s. The user, 2026-10-17:
  the emulator's PC has an NVMe drive; most Amigas have mechanical drives or CF cards, so no speed
  claim without a real-machine measurement.
  Found on the way: the floor is the 68000 itself (touching 340 KB once in compiled C took 2.9 s on
  the A600); reading byte by byte through calls doubled it (fixed); a letter fingerprint (32 bits a
  path, built from each entry's new bytes) skips paths that cannot match.
- **Done 2026-10-17:** the hot loops in assembler (no measurable gain on the emulated 68000: the
  cost is spread over many steps per entry; see fuzzy.library's README), CTerm's tab title given
  back when fzf ends (tui.library 0.4: TuiClose clears it), the wheel in fzf (3 lines a notch, both
  ways, seen in CTerm).
- **On the real A1200, 2026-10-17** (the user's go; other sessions told): tui.library 0.4b1 (0.3b1
  kept as LIBS:tui.library-0.3b1.bak), fuzzy.library 0.1b1, C:updatedb/locate/fzf. updatedb: 57,028
  entries, 5.3 s, 1,090,137-byte index; a search 0.14-0.30 s (Emu68 + SD card: not typical); fzf in
  CTerm: typing, Esc, the title given back.
- **Next:** for a real gain on a 68000, a different index layout (proposed: not started); the real
  A1200 (needs the user's go: it is shared); real-machine speed figures (see the user's rule: none
  from the emulator in the docs).
