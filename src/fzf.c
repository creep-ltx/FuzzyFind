/* fzf - FuzzyFind: the interactive finder for the terminal, after fzf.
 *
 *   fzf [QUERY=text] [INDEX=file] [FILES|DRAWERS] [EXACT] [CASE] [REVERSE]
 *
 * The list: lines from a pipe (List #? ALL LFORMAT %p%n | fzf), else every
 * path of the FuzzyFind index. Type and the list narrows, the best match
 * nearest the prompt; Up/Down (and Ctrl+K/J, Ctrl+P/N, the wheel) move;
 * Enter prints the chosen line and ends (return code 0); Esc or Ctrl+C
 * ends without one (5). A click picks a line, a double click takes it.
 * Keys: ENVARC:FuzzyFind/fzf.keys, written with the defaults on first use.
 *
 * Typing never waits for the search: it runs in pieces between key
 * presses, and a letter added to the query only looks again at what still
 * matched. Keys come from the console ("*") whenever the input is not one
 * (a pipe, or the NIL: backquotes give), and the screen is drawn on it
 * when the output is captured (Echo `fzf`): tui.library 0.4's TuiOpenIO. */
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/rdargs.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/fuzzy.h>
#include <proto/tui.h>
#include "version.h"
#include "fzf.h"

static const char __attribute__((used)) vers[] = "$VER: fzf " FF_VERSTR " (" FF_DATE ")";

struct Library *FuzzyBase, *TuiBase;

#define MAXLINE 1024
#define MAXQ    200
#define CHUNK   256                 /* entries searched between looks at the keys */
#define POOL    32768

struct Item {
    const char *s;
    UWORD len, name;
};

static struct Item *items;
static LONG nitems, capitems;
static struct { char *mem; LONG used, size; } pool;
static char **blocks;               /* every pool block, to free */
static LONG nblocks, capblocks;

static LONG *cand, ncand;           /* what this search looks at (indices) */
static LONG *hit, *hscore, nhit;    /* what matched so far, in the list's order */
static LONG *order;                 /* the matches best first, when done */
static LONG *tmp;                   /* for sorting */
static LONG done;                   /* how far through cand */
static int searching, sorted;

static struct Tui *t;
static char query[MAXQ + 1], lastq[MAXQ + 1];
static LONG qlen, cur, top;         /* the chosen match, the first one shown */
static FuzzyPattern *pat;
static ULONG pflags;
static int reverse, lastvalid;
static ULONG clicksecs, clickmics;
static LONG clickrow = -1;

/* ---- the list ---------------------------------------------------------- */

static void *poolget(LONG n)
{
    void *r;
    if (pool.used + n > pool.size) {
        LONG sz = n > POOL ? n : POOL;
        if (nblocks == capblocks) {
            LONG nc = capblocks ? capblocks * 2 : 64;
            char **nb = AllocVec(nc * sizeof(char *), MEMF_ANY);
            if (!nb) return NULL;
            if (blocks) {
                memcpy(nb, blocks, nblocks * sizeof(char *));
                FreeVec(blocks);
            }
            blocks = nb;
            capblocks = nc;
        }
        if (!(pool.mem = AllocVec(sz, MEMF_ANY))) return NULL;
        blocks[nblocks++] = pool.mem;
        pool.used = 0;
        pool.size = sz;
    }
    r = pool.mem + pool.used;
    pool.used += n;
    return r;
}

static int additem(const char *s, LONG len, LONG name)
{
    char *c;
    if (len > MAXLINE) len = MAXLINE;
    if (nitems == capitems) {
        LONG nc = capitems ? capitems * 2 : 1024;
        struct Item *ni = AllocVec(nc * sizeof(*ni), MEMF_ANY);
        if (!ni) return 0;
        if (items) {
            memcpy(ni, items, nitems * sizeof(*ni));
            FreeVec(items);
        }
        items = ni;
        capitems = nc;
    }
    if (!(c = poolget(len + 1))) return 0;
    memcpy(c, s, len);
    c[len] = 0;
    items[nitems].s = c;
    items[nitems].len = (UWORD)len;
    items[nitems].name = (UWORD)name;
    nitems++;
    return 1;
}

static LONG nameof(const char *s, LONG len)
{
    LONG i = len;
    while (i > 0 && s[i - 1] != '/' && s[i - 1] != ':') i--;
    return i;
}

static void readpipe(BPTR in)
{
    static char line[MAXLINE + 2], buf[4096];
    LONG n, i, k = 0;
    while ((n = Read(in, buf, sizeof(buf))) > 0)
        for (i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (k) additem(line, k, nameof(line, k));
                k = 0;
            } else if (k < MAXLINE)
                line[k++] = c;
        }
    if (k) additem(line, k, nameof(line, k));
}

static int readindex(const char *name, ULONG flags)
{
    struct FuzzyIndex *x;
    struct FuzzyEntry e;
    LONG err;
    if (!(x = FuzzyOpenIndex((CONST_STRPTR)name, &err))) {
        char where[256];
        if (name) strncpy(where, name, 255), where[255] = 0;
        else FuzzyIndexPath((STRPTR)where, sizeof(where));
        printf("fzf: %s: %s%s\n", where, FuzzyErrorText(err), err == FERR_NOFILE ? " (run updatedb to make it)" : "");
        return 0;
    }
    while (FuzzyNextEntry(x, &e)) {
        if ((flags & FSF_FILES) && e.Type != FET_FILE) continue;
        if ((flags & FSF_DRAWERS) && e.Type != FET_DRAWER) continue;
        if (!additem((const char *)e.Path, e.PathLen, e.NameOffset)) break;
    }
    FuzzyCloseIndex(x);
    return 1;
}

/* ---- searching --------------------------------------------------------- */

/* a before b: higher score, then shorter, then earlier in the list */
static int before(LONG a, LONG sa, LONG b, LONG sb)
{
    if (sa != sb) return sa > sb;
    if (items[a].len != items[b].len) return items[a].len < items[b].len;
    return a < b;
}

/* the matches best first: a merge sort over (index, score) pairs */
static void sortorder(void)
{
    LONG w, i, n = nhit;
    LONG *a = order, *b = tmp;
    for (i = 0; i < n; i++) order[i] = i;   /* positions in hit[] */
    for (w = 1; w < n; w <<= 1) {
        for (i = 0; i < n; i += w << 1) {
            LONG l = i, m = i + w < n ? i + w : n, r = i + (w << 1) < n ? i + (w << 1) : n, j = l, k = m, o = l;
            while (j < m && k < r) {
                LONG x = a[j], y = a[k];
                if (!before(hit[y], hscore[y], hit[x], hscore[x])) b[o++] = a[j++];
                else b[o++] = a[k++];
            }
            while (j < m) b[o++] = a[j++];
            while (k < r) b[o++] = a[k++];
        }
        { LONG *s = a; a = b; b = s; }
    }
    if (a != order) memcpy(order, a, n * sizeof(LONG));
    sorted = 1;
}

/* a new query: start a search (over what still matched, if it only grew) */
static void newsearch(void)
{
    LONG i;
    int narrower = lastvalid && !searching && qlen > (LONG)strlen(lastq) &&
                   !strncmp(query, lastq, strlen(lastq));
    if (pat) FuzzyFreePattern(pat);
    pat = FuzzyPrepare((CONST_STRPTR)query, pflags);
    if (narrower) {                 /* only the old matches can match now */
        for (i = 0; i < nhit; i++) cand[i] = hit[i];
        ncand = nhit;
    } else {
        for (i = 0; i < nitems; i++) cand[i] = i;
        ncand = nitems;
    }
    nhit = 0;
    done = 0;
    searching = 1;
    sorted = 0;
    cur = 0;
    top = 0;
    strcpy(lastq, query);
    lastvalid = 1;
}

/* a piece of the search; when it ends, the matches are sorted */
static void step(void)
{
    LONG end = done + CHUNK < ncand ? done + CHUNK : ncand, i;
    for (i = done; i < end; i++) {
        LONG k = cand[i];
        LONG s = qlen ? FuzzyScore(pat, (CONST_STRPTR)items[k].s, items[k].len, NULL, 0) : 1;
        if (s > 0) {
            hit[nhit] = k;
            hscore[nhit] = s;
            nhit++;
        }
    }
    done = end;
    if (done >= ncand) {
        searching = 0;
        if (qlen) sortorder();
        else {                      /* nothing typed: the list as it came */
            for (i = 0; i < nhit; i++) order[i] = i;
            sorted = 1;
        }
    }
}

/* ---- drawing ----------------------------------------------------------- */

#define C_TEXT    TC_DEFAULT
#define C_MATCH   10
#define C_CURBG   8
#define C_MARK    9
#define C_PROMPT  14
#define C_INFO    8

static LONG listrows(void) { return t->Rows - 2 > 1 ? t->Rows - 2 : 1; }

/* the screen row of the n-th shown match (0 = the best) */
static LONG rowof(LONG n)
{
    return reverse ? 2 + n : t->Rows - 3 - n;
}

static void drawline(LONG row, LONG k, int iscur)
{
    UWORD pos[64];
    LONG np = 0, i, w = t->Cols - 2, len = items[k].len, from = 0;
    const char *s = items[k].s;
    /* the chosen line: a grey bar in CTerm; reversed in a plain console,
     * whose few pens would make a grey bar black under black text */
    LONG bg = iscur && t->CTerm ? C_CURBG : TC_DEFAULT;
    LONG at = iscur ? (t->CTerm ? TA_BOLD : TA_REV) : 0;
    TuiFill(t, 0, row, t->Cols, ' ', C_TEXT, bg, iscur && !t->CTerm ? TA_REV : 0);
    if (iscur) TuiPutC(t, 0, row, '>', C_MARK, bg, TA_BOLD | (at & TA_REV));
    if (len > w) from = len - w + 2;        /* too long: the end, where the name is */
    if (qlen) {
        memset(pos, 0xFF, sizeof(pos));
        FuzzyScore(pat, (CONST_STRPTR)s, len, pos, 64);
        np = 64;
    }
    if (from) {
        TuiPut(t, 2, row, "..", 2, C_INFO, bg, at);
        TuiPut(t, 4, row, s + from + 2, len - from - 2, C_TEXT, bg, at);
    } else
        TuiPut(t, 2, row, s, len, C_TEXT, bg, at);
    for (i = 0; i < np; i++) {
        LONG p = pos[i];
        if (p == 0xFFFF || p < from + (from ? 2 : 0)) continue;
        TuiPutC(t, 2 + p - from, row, (UBYTE)s[p], C_MATCH, bg, TA_BOLD | (at & TA_REV));
    }
}

static void draw(void)
{
    char info[80];
    LONG rows = listrows(), n, shown = sorted ? nhit : 0;
    TuiClear(t);
    if (cur >= shown) cur = shown ? shown - 1 : 0;
    if (cur < top) top = cur;
    if (cur >= top + rows) top = cur - rows + 1;
    for (n = 0; n < rows && top + n < shown; n++)
        drawline(rowof(n), hit[order[top + n]], top + n == cur);
    if (searching)
        sprintf(info, "  %ld/%ld  searching %ld%%", (long)nhit, (long)nitems,
                (long)(ncand ? done * 100 / ncand : 100));
    else
        sprintf(info, "  %ld/%ld", (long)nhit, (long)nitems);
    TuiPut(t, 0, reverse ? 1 : t->Rows - 2, info, strlen(info), C_INFO, TC_DEFAULT, 0);
    TuiPut(t, 0, reverse ? 0 : t->Rows - 1, "> ", 2, C_PROMPT, TC_DEFAULT, TA_BOLD);
    TuiPut(t, 2, reverse ? 0 : t->Rows - 1, query, qlen, C_TEXT, TC_DEFAULT, 0);
    TuiCursor(t, 2 + qlen, reverse ? 0 : t->Rows - 1, 1);
}

/* ---- the keys and the mouse -------------------------------------------- */

static void move(LONG by)
{
    LONG shown = sorted ? nhit : 0;
    cur += by;
    if (cur < 0) cur = 0;
    if (cur >= shown) cur = shown ? shown - 1 : 0;
}

/* 1: take the chosen line; -1: end without one; 0: go on */
static int act(int a, int key)
{
    switch (a) {
    case A_ACCEPT: return sorted && nhit ? 1 : 0;
    case A_ABORT: return -1;
    case A_UP: move(reverse ? -1 : 1); break;
    case A_DOWN: move(reverse ? 1 : -1); break;
    case A_PAGEUP: move(reverse ? -listrows() : listrows()); break;
    case A_PAGEDOWN: move(reverse ? listrows() : -listrows()); break;
    case A_FIRST: cur = 0; break;
    case A_LAST: move(0x7FFFFFF); break;
    case A_BACKSPACE:
        if (qlen) {
            query[--qlen] = 0;
            newsearch();
        }
        break;
    case A_CLEAR:
        if (qlen) {
            qlen = 0;
            query[0] = 0;
            newsearch();
        }
        break;
    case A_DELWORD:
        while (qlen && query[qlen - 1] == ' ') qlen--;
        while (qlen && query[qlen - 1] != ' ') qlen--;
        query[qlen] = 0;
        newsearch();
        break;
    case A_REDRAW: TuiRedraw(t); break;
    default:
        if (key >= 32 && key < 256 && key != 127 && qlen < MAXQ) {
            query[qlen++] = (char)key;
            query[qlen] = 0;
            newsearch();
        }
    }
    return 0;
}

static int mouse(TuiEv *e)
{
    LONG n;
    if (e->mact == TB_WHEELUP) move(reverse ? -3 : 3);
    else if (e->mact == TB_WHEELDOWN) move(reverse ? 3 : -3);
    else if (e->mact == TB_PRESS) {
        n = reverse ? e->row - 2 : t->Rows - 3 - e->row;
        if (n < 0 || n >= listrows() || top + n >= (sorted ? nhit : 0)) return 0;
        cur = top + n;
        if (e->row == clickrow && (e->secs - clicksecs) * 1000000UL + e->mics - clickmics < 500000UL)
            return 1;                   /* a double click: take it */
        clickrow = e->row;
        clicksecs = e->secs;
        clickmics = e->mics;
    }
    return 0;
}

/* ---- the program ------------------------------------------------------- */

int main(void)
{
    static const char tmpl[] = "QUERY/K,INDEX/K,FILES/S,DRAWERS/S,EXACT/S,CASE/S,REVERSE/S";
    enum { A_QUERY, A_INDEX, A_FILES, A_DRAWERS, A_EXACT, A_CASE, A_REV, NA };
    LONG args[NA];
    struct RDArgs *rd;
    BPTR con = 0;
    int rc = 20, piped, outcon, incon, r = 0;
    TuiEv e;

    memset(args, 0, sizeof(args));
    if (!(rd = ReadArgs((STRPTR)tmpl, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)"fzf");
        return 10;
    }
    if (!(FuzzyBase = OpenLibrary(FUZZYNAME, 0))) {
        printf("fzf: needs fuzzy.library\n");
        goto out;
    }
    if (!(TuiBase = OpenLibrary(TUINAME, 0)) || (TuiBase->lib_Version == 0 && TuiBase->lib_Revision < 4)) {
        printf("fzf: needs tui.library 0.4 or newer\n");
        goto out;
    }
    pflags = (args[A_EXACT] ? FPF_PLAIN : 0) | (args[A_CASE] ? FPF_CASE : 0);
    reverse = args[A_REV] != 0;

    /* the list */
    /* a pipe or a file, not NIL: (what backquotes give a command) */
    piped = !IsInteractive(Input()) && Input() &&
            ((struct FileHandle *)BADDR(Input()))->fh_Type != NULL;
    if (piped) readpipe(Input());
    else if (!readindex((const char *)args[A_INDEX], (args[A_FILES] ? FSF_FILES : 0) | (args[A_DRAWERS] ? FSF_DRAWERS : 0)))
        goto out;
    if (!nitems) {
        rc = 5;
        goto out;
    }
    cand = AllocVec(nitems * sizeof(LONG), MEMF_ANY);
    hit = AllocVec(nitems * sizeof(LONG), MEMF_ANY);
    hscore = AllocVec(nitems * sizeof(LONG), MEMF_ANY);
    order = AllocVec(nitems * sizeof(LONG), MEMF_ANY);
    tmp = AllocVec(nitems * sizeof(LONG), MEMF_ANY);
    if (!cand || !hit || !hscore || !order || !tmp) {
        printf("fzf: out of memory\n");
        goto out;
    }

    /* the console: keys from it when the list came through a pipe, and the
     * screen on it when the output is captured */
    outcon = IsInteractive(Output());
    incon = IsInteractive(Input());
    if (!incon || !outcon) con = Open((STRPTR)"*", MODE_OLDFILE);
    if (!(t = TuiOpenIO(incon ? 0 : con, outcon ? 0 : con))) {
        printf("fzf: not in a terminal\n");
        goto out;
    }
    TuiTitle(t, (CONST_STRPTR)"fzf");
    keys_load();
    if (args[A_QUERY]) {
        strncpy(query, (char *)args[A_QUERY], MAXQ);
        qlen = strlen(query);
    }
    newsearch();

    for (;;) {
        LONG got;
        if (searching) {
            step();
            if (searching && !TuiKeyWaiting(t)) {
                if ((done / CHUNK & 7) == 0) {  /* now and then: how far */
                    draw();
                    TuiFlush(t);
                }
                continue;
            }
        }
        draw();
        TuiFlush(t);
        if (searching) {
            got = TuiPoll(t, &e);
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
                r = -1;
                break;
            }
        } else
            got = TuiWait(t, &e, -1, 0, NULL);
        if (!got) continue;
        if (e.type == TE_END) {
            r = -1;
            break;
        }
        if (e.type == TE_RESIZE) TuiResized(t);
        else if (e.type == TE_MOUSE) r = mouse(&e);
        else if (e.type == TE_PASTE) {
            LONG i;
            for (i = 0; i < e.len && qlen < MAXQ; i++)
                if (e.text[i] >= 32) query[qlen++] = e.text[i];
            query[qlen] = 0;
            newsearch();
        } else if (e.type == TE_KEY)
            r = act(keys_find(e.key), e.key);
        if (r) break;
    }
    TuiClose(t);
    t = NULL;
    if (r > 0) {
        LONG k = hit[order[cur]];
        Write(Output(), (APTR)items[k].s, items[k].len);
        Write(Output(), "\n", 1);
        rc = 0;
    } else
        rc = 5;
out:
    {
        LONG i;
        for (i = 0; i < nblocks; i++) FreeVec(blocks[i]);
        if (blocks) FreeVec(blocks);
    }
    if (items) FreeVec(items);
    if (cand) FreeVec(cand);
    if (hit) FreeVec(hit);
    if (hscore) FreeVec(hscore);
    if (order) FreeVec(order);
    if (tmp) FreeVec(tmp);
    if (pat) FuzzyFreePattern(pat);
    if (con) Close(con);
    if (TuiBase) CloseLibrary(TuiBase);
    if (FuzzyBase) CloseLibrary(FuzzyBase);
    FreeArgs(rd);
    return rc;
}
