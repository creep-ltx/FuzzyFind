# FuzzyFind

**Find anything on your Amiga by typing a few letters.** FuzzyFind keeps a catalogue of every file and drawer
on your disks and lets you search it the way fzf does on Linux: type a few letters, `mltvw`, and
`Workbench:Utilities/MultiView` is on top.

Three commands, for AmigaOS 2.04 and up, any 680x0:

| Command | What it does |
|---|---|
| **`updatedb`** | builds the catalogue (the index) of your disks; run it at boot or at night |
| **`locate`** | prints the paths that match, for scripts and pipes |
| **`fzf`** | the interactive finder: type and the list narrows, Enter picks |

## Features

**updatedb**
- Indexes every real disk by itself; network shares (NFS, SMB) and RAM: are left out. Or name the
  volumes, or set them once in `ENV:FuzzyFind/Volumes`.
- Keeps each file's size, date, type and protection bits: a catalogue, not just names.
- Small: a real A1200's 57,028 files and drawers took a 1.04 MB index.
- Safe: the new index replaces the old one only when it is complete; Ctrl-C keeps the old one.
- Low priority if you like (`PRI=-5`), quiet for the Startup-Sequence (`QUIET`).

**locate**
- Plain, as Unix locate: every path with the text in it, any case, accents folded (`goteborg`
  finds `Göteborg`), several words all present.
- Amiga patterns: `locate #?.mod` matches names.
- Fuzzy (`FUZZY`): the best matches first, as fzf ranks them.
- Only files (`FILES`), only drawers (`DRAWERS`), the name only (`NAME`), a count (`COUNT`), a
  limit (`LIMIT`).

**fzf**
- Lists every path of the index, or the lines piped into it (`List ... | fzf`).
- fzf's ranking: letters at word starts, in a row and in the file name count most.
- Typing never waits, even on a 68000: the search runs in pieces between key presses, and each
  added letter only re-checks what still matched.
- Matched letters highlighted; long paths show their end, where the name is.
- Mouse: the wheel moves, a click picks, a double click takes.
- Works in backquotes: ``Edit `fzf` `` edits the file you pick.
- Every key configurable: `ENVARC:FuzzyFind/fzf.keys`, written with fzf's own keys the first time.
- In CTerm (colours, mouse, pastes) or any Shell window.

## Using it

```
updatedb                          ; once, then at boot or at night
locate startup                    ; every path with "startup" in it
locate #?.mod COUNT               ; how many .mod files
locate dpaint FUZZY LIMIT=5       ; the 5 best fuzzy matches
fzf                               ; find any file, interactively
Edit `fzf QUERY=user-startup`     ; pick a file and edit it
List Work: ALL LFORMAT %p%n | fzf ; pick from any list
```

The details, the keys and the settings: [docs/usage.md](docs/usage.md). How it is built and why:
[docs/design.md](docs/design.md).

## Installing

| File | Where |
|---|---|
| `updatedb`, `locate`, `fzf` | `C:` |
| `fuzzy.library` (from AmigaLibs, the build for your processor) | `LIBS:` |
| `tui.library` 0.4 or newer (from AmigaLibs; for fzf) | `LIBS:` |

Then run `updatedb` once. To keep the index fresh, add to `S:User-Startup`:

```
Run >NIL: updatedb QUIET PRI=-5
```

## Speed

On a real A1200 with a PiStorm (Emu68, which runs as a fast 68040; the files on an SD card, so
not typical of an Amiga), measured 2026-10-17: `updatedb` indexed 57,028 files and drawers in
5.3 s, reading the index took 0.14 s, and one search 0.14-0.30 s.

**Not measured on a stock Amiga yet.** In FS-UAE (2026-10-17, the index in RAM:, so only the
processor's share): one search of a 21,028-path test list took 14-28 s on a 7 MHz 68000 with chip
memory only, and 3-6 s on a 14 MHz 68020 with fast memory. A search reads the whole index, so on a
real machine reading it from the hard drive, CF card or SD card comes on top, and that depends on
the drive. Fewer files means faster searches. In `fzf` only the first letter searches the whole
index; each further letter only re-checks what already matched.

## Building

`make` with bebbo's m68k-amigaos-gcc, with the **AmigaLibs** repository beside this one
(`../AmigaLibs`: fuzzy.library and tui.library's headers).

## State

**0.1b1, 2026-10-17.** On the real A1200 (PiStorm, Emu68, OS 3.2.3): updatedb over its four disks
(57,028 entries, network shares left out), locate, and fzf in CTerm (typing, Esc, the tab title
given back). In FS-UAE on the A1200 test machine (68020, OS 3.2.1) and the A600
(68000, Workbench 2.05): `updatedb` on their disks, every `locate` mode, `fzf` typing, moving,
picking, from a pipe and in backquotes, Esc..

## License

MIT, see [LICENSE](LICENSE).
