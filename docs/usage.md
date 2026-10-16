# Using FuzzyFind

- [updatedb](#updatedb)
- [locate](#locate)
- [fzf](#fzf)
- [Settings](#settings)
- [Ideas](#ideas)

## updatedb

```
updatedb [VOLUMES...] [TO=file] [PRI=n] [QUIET]
```

Builds the index: every file and drawer, with its size, date, type and protection bits.

| Argument | |
|---|---|
| `VOLUMES` | the volumes to index (`Work: AmigaOS:`). Without them: the setting `FuzzyFind/Volumes`, else every volume on a real disk |
| `TO` | write the index there instead of the usual place |
| `PRI` | run at this task priority (`PRI=-5`: let everything else go first) |
| `QUIET` | say nothing (for the Startup-Sequence) |

- **Which volumes by itself:** those whose file system runs on an exec device (`scsi.device`,
  `brcm-sdhc.device`, `uaehf.device`...). Network shares (NFS, SMB) and RAM: are left out: they
  can be huge, slow or gone.
- **Soft links** are listed but never followed (they can loop).
- **Ctrl-C** stops it; the old index stays as it was. Return code 5.
- **It prints** each volume and its entries, then the total and the time:

```
Workbench: 1575
1575 entries in 1.9 s: S:FuzzyFind.index
```

Keep it fresh at boot, at low priority, in `S:User-Startup`:

```
Run >NIL: updatedb QUIET PRI=-5
```

## locate

```
locate PATTERN... [FUZZY] [LIMIT=n] [FILES|DRAWERS] [NAME] [CASE] [COUNT] [INDEX=file]
```

Prints the paths that match, one a line. Return code 0 if any matched, 5 if none, 20 if there is
no index.

| Argument | |
|---|---|
| `PATTERN` | what to find; several words must all be there |
| `FUZZY` (`F`) | the letters in order with gaps, the best first (as fzf); `LIMIT` of them (20 unless given) |
| `LIMIT` (`N`) | at most this many |
| `FILES`, `DRAWERS` | only files, or only drawers |
| `NAME` | look only at the name, not the drawers above it |
| `CASE` | case matters (otherwise `README` finds `ReadMe`) |
| `COUNT` | print how many matched, not the paths |
| `INDEX` | another index than the usual one |

- **Plain** (without `FUZZY`): the text anywhere in the path, any case, accents folded
  (`goteborg` finds `Göteborg`), in the index's order. As Unix `locate`.
- **An Amiga pattern** (`#?.mod`, `(a|b)#?`) is matched against each **name**:

```
locate #?.mod                 ; every .mod file
locate #?.library COUNT       ; how many libraries
locate startup DRAWERS        ; drawers with startup in their path
locate dpaint FUZZY LIMIT=5   ; the 5 best fuzzy matches
```

## fzf

```
fzf [QUERY=text] [INDEX=file] [FILES|DRAWERS] [EXACT] [CASE] [REVERSE]
```

The interactive finder. The list is every path of the index, or the lines piped into it. Type and
it narrows; the best match is nearest the prompt.

| Argument | |
|---|---|
| `QUERY` | start with this typed |
| `INDEX` | another index |
| `FILES`, `DRAWERS` | only files, or only drawers (from the index) |
| `EXACT` | each word as one piece of text, no gaps (as `locate` without `FUZZY`) |
| `CASE` | case matters |
| `REVERSE` | the prompt at the top and the list going down |

**The keys** (all can be changed, see Settings):

| Key | Does |
|---|---|
| letters | type into the query |
| Up, Ctrl+K, Ctrl+P | to a worse match (up the list) |
| Down, Ctrl+J, Ctrl+N | to a better match |
| Page Up / Down, Alt+Up / Down | a page |
| Home, Ctrl+Down / End, Ctrl+Up | the best match / the last one |
| Backspace, Ctrl+H | take back a letter |
| Ctrl+U | clear the query |
| Ctrl+W, Alt+Backspace | take back a word |
| Ctrl+L | draw everything again |
| Enter | print the chosen line and end (return code 0) |
| Esc, Ctrl+C, Ctrl+G, Ctrl+Q | end without one (return code 5) |

**The mouse** (in CTerm): a click picks a line, a double click takes it, the wheel moves.

**With other commands:**

```
Edit `fzf`                                ; edit the file you pick
CD `fzf DRAWERS`                          ; go to the drawer you pick
List Work:Music ALL LFORMAT %p%n | fzf    ; pick from any list
fzf QUERY=.mod FILES                      ; files only, with .mod already typed
```

`fzf` draws on the console even when its output goes elsewhere (in backquotes, or `>file`): that
needs tui.library 0.4 or newer.

**While it searches** (on a slow machine with a big index), the line above the prompt says how far
it is, and typing goes on: each key restarts the search, and a letter added only looks again at
what still matched.

## Settings

| Setting | |
|---|---|
| `ENV:FuzzyFind/Index` | where the index is (default `S:FuzzyFind.index`) |
| `ENV:FuzzyFind/Volumes` | the volumes `updatedb` indexes when given none, e.g. `Work: AmigaOS: Dump:` |
| `ENVARC:FuzzyFind/fzf.keys` | fzf's keys: one a line, `KEY ACTION` (`<C-k> up`, `<Esc> abort`); `none` takes a key away. Written with the defaults the first time fzf runs; the copy in `ENV:` is read first |

To set one for good, write it in `ENVARC:FuzzyFind/` (kept over reboots) and copy it to
`ENV:FuzzyFind/` (used now): `Echo "Work: AmigaOS:" >ENVARC:FuzzyFind/Volumes`, then
`Copy ENVARC:FuzzyFind/Volumes ENV:FuzzyFind/`.

fzf's actions for the keys file: `accept abort up down pageup pagedown first last backspace clear
deleteword redraw`.

## Ideas

What the index can do beyond searching (talked through 2026-10-17, not built yet): what changed
since last night (two indexes compared), disk usage at a glance, duplicates (same name and size),
a launcher (type "dpai", Deluxe Paint starts), and other programs reading it through
fuzzy.library: Ahiru jumping to any file, csh's `cd` to any drawer by a few letters.
