/* keys - fzf's keys are the person's: ENVARC:FuzzyFind/fzf.keys, one
 * binding a line ("<C-k> up", "<Esc> abort"; "none" takes a key away; ';'
 * starts a comment), the same form as Maki's and Sumi's. Written with the
 * defaults (fzf's own keys) when missing; ENV:FuzzyFind/fzf.keys, the copy
 * made at boot, is read first. Keys not bound type into the query.
 *
 * Amiga facts behind the defaults: Right Amiga keys belong to CTerm's
 * menus and Left Amiga keys to its panes; Ctrl-C comes as a signal
 * (tui.library makes it a key); the A1200 has no Home, End or Page keys,
 * so Alt+arrows do their work too. */
#include <string.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/tui.h>
#include "fzf.h"

#define KEYFILE "ENVARC:FuzzyFind/fzf.keys"
#define KEYENV  "ENV:FuzzyFind/fzf.keys"
#define MAXB    64

static const char *const actnames[A_COUNT] = {
    "none", "accept", "abort", "up", "down", "pageup", "pagedown", "first", "last",
    "backspace", "clear", "deleteword", "redraw"
};

static const char defaults[] =
    "; fzf's keys: one a line, KEY ACTION. <C-x> is Ctrl+X, <A-x> Alt, <S-x> Shift.\n"
    "; Keys: <Up> <Down> <Left> <Right> <Home> <End> <PgUp> <PgDn> <Del> <BS> <Tab>\n"
    "; <Enter> <Esc> <Help> <Space> <F1>-<F12>, or a character. 'none' takes a key away.\n"
    "; Actions: accept abort up down pageup pagedown first last backspace clear\n"
    "; deleteword redraw. A key bound to nothing types into the query.\n"
    "; (Right Amiga keys are CTerm's menus, Left Amiga keys its panes: not here.)\n"
    "\n<Enter> accept\n<Esc> abort\n<C-c> abort\n<C-g> abort\n<C-q> abort\n"
    "<Up> up\n<C-k> up\n<C-p> up\n<Down> down\n<C-j> down\n<C-n> down\n"
    "<PgUp> pageup\n<A-Up> pageup\n<PgDn> pagedown\n<A-Down> pagedown\n"
    "<Home> first\n<C-Down> first\n<End> last\n<C-Up> last\n"
    "<BS> backspace\n<C-h> backspace\n<C-u> clear\n<C-w> deleteword\n<A-BS> deleteword\n"
    "<C-l> redraw\n";

typedef struct { int key, act; } Bind;
static Bind binds[MAXB];
static int nbinds;

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, const char *b)
{
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

static const struct { const char *n; int k; } named[] = {
    { "Up", K_UP }, { "Down", K_DOWN }, { "Left", K_LEFT }, { "Right", K_RIGHT },
    { "Home", K_HOME }, { "End", K_END }, { "PgUp", K_PGUP }, { "PgDn", K_PGDN },
    { "Ins", K_INS }, { "Del", K_DEL }, { "BS", K_BS }, { "Tab", K_TAB },
    { "Enter", K_ENTER }, { "Return", K_ENTER }, { "Esc", K_ESC }, { "Help", K_HELP },
    { "Space", ' ' }, { NULL, 0 }
};

/* "<C-s>" "<S-A-Left>" "<F3>" "x" -> a key, 0 if not understood */
static int parsekey(const char *s)
{
    int m = 0, i;
    char name[16];
    const char *e;
    if (s[0] != '<' || !s[1]) return (unsigned char)s[0] && !s[1] ? (unsigned char)s[0] : 0;
    s++;
    if (!(e = strchr(s, '>'))) return 0;
    while (e - s > 2 && s[1] == '-') {
        int c = lower((unsigned char)s[0]);
        if (c == 'c') m |= TM_CTRL;
        else if (c == 's') m |= TM_SHIFT;
        else if (c == 'a' || c == 'm') m |= TM_ALT;
        else return 0;
        s += 2;
    }
    if (e - s >= (int)sizeof(name)) return 0;
    memcpy(name, s, e - s);
    name[e - s] = 0;
    if (!name[0]) return 0;
    if (!name[1]) {
        int c = (unsigned char)name[0];
        if (m & TM_CTRL) c = lower(c);
        return c | m;
    }
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9') {
        int n = name[1] - '0';
        if (name[2] >= '0' && name[2] <= '9') n = n * 10 + name[2] - '0';
        return n >= 1 && n <= 12 ? K_F(n) | m : 0;
    }
    for (i = 0; named[i].n; i++)
        if (same(name, named[i].n)) return named[i].k | m;
    return 0;
}

static void addbind(int key, int act)
{
    int i;
    for (i = 0; i < nbinds; i++)
        if (binds[i].key == key) {
            binds[i].act = act;         /* a later line wins */
            return;
        }
    if (nbinds < MAXB) {
        binds[nbinds].key = key;
        binds[nbinds].act = act;
        nbinds++;
    }
}

static void parse(char *text)
{
    char *line = text;
    while (line && *line) {
        char *nl = strchr(line, '\n'), *k, *a, *e;
        int key, act;
        if (nl) *nl = 0;
        k = line;
        while (*k == ' ' || *k == '\t') k++;
        if (*k && *k != ';' && *k != '#') {
            for (e = k; *e && *e != ' ' && *e != '\t'; e++) ;
            a = e;
            while (*a == ' ' || *a == '\t') a++;
            *e = 0;
            for (e = a; *e && *e != ' ' && *e != '\t' && *e != '\r'; e++) ;
            *e = 0;
            key = parsekey(k);
            for (act = 0; act < A_COUNT && !same(a, actnames[act]); act++) ;
            if (key && act < A_COUNT) addbind(key, act);
        }
        line = nl ? nl + 1 : NULL;
    }
}

static char *readall(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *t;
    long n;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0 || n > 65536 || !(t = AllocVec(n + 1, MEMF_ANY))) {
        Close(fh);
        return NULL;
    }
    n = Read(fh, t, n);
    Close(fh);
    t[n > 0 ? n : 0] = 0;
    return t;
}

static void writeto(const char *dir, const char *path)
{
    BPTR l = Lock((STRPTR)dir, SHARED_LOCK), fh;
    if (!l) l = CreateDir((STRPTR)dir);
    if (l) UnLock(l);
    if ((fh = Open((STRPTR)path, MODE_NEWFILE)) != 0) {
        Write(fh, (APTR)defaults, sizeof(defaults) - 1);
        Close(fh);
    }
}

void keys_load(void)
{
    char *t, *d;
    nbinds = 0;
    /* the defaults first, the person's file over them */
    if ((d = AllocVec(sizeof(defaults), MEMF_ANY)) != NULL) {
        memcpy(d, defaults, sizeof(defaults));
        parse(d);
        FreeVec(d);
    }
    t = readall(KEYENV);
    if (!t) t = readall(KEYFILE);
    if (!t) {
        writeto("ENVARC:FuzzyFind", KEYFILE);
        writeto("ENV:FuzzyFind", KEYENV);
        return;
    }
    parse(t);
    FreeVec(t);
}

int keys_find(int key)
{
    int i;
    for (i = 0; i < nbinds; i++)
        if (binds[i].key == key) return binds[i].act;
    return A_NONE;
}
