/* updatedb - FuzzyFind: build the index of every file and drawer.
 *
 *   updatedb [VOLUMES...] [TO=file] [PRI=n] [QUIET]
 *
 * Without volumes: the ones the ENV: setting FuzzyFind/Volumes names
 * (e.g. "Work: AmigaOS:"), else every mounted volume on a real disk (its
 * handler runs on an exec device such as scsi.device): network shares
 * and RAM: are left out. The index goes where fuzzy.library says
 * (FuzzyFind/Index, else S:FuzzyFind.index), written whole and renamed
 * into place, so a search never sees half of it. Ctrl-C stops and keeps
 * the old index.
 *
 * Each drawer is read with ExAll (names, sizes, dates in one call) and
 * its entries added before walking into its drawers, so neighbours share
 * most of their path and the index stays small. Soft links are kept but
 * never followed. */
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/exall.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/fuzzy.h>
#include "version.h"

static const char __attribute__((used)) vers[] = "$VER: updatedb " FF_VERSTR " (" FF_DATE ")";

struct Library *FuzzyBase;

#define MAXPATH  1024
#define MAXVOLS  32
#define EABUF    4096

static FuzzyWriter *w;
static char path[MAXPATH + 2];
static LONG entries, skipped, stopped;
static int quiet;

/* one drawer's entries, kept while walking into its drawers */
struct Item {
    struct Item *next;
    LONG type;
    ULONG size, date, prot;
    char name[1];
};

static ULONG secs(LONG days, LONG mins, LONG ticks)
{
    return (ULONG)days * 86400UL + (ULONG)mins * 60UL + (ULONG)ticks / 50;
}

static int broken(void)
{
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) stopped = 1;
    return stopped;
}

static void add(LONG len, UBYTE type, ULONG prot, ULONG size, ULONG date)
{
    struct FuzzyEntry e;
    e.Path = (CONST_STRPTR)path;
    e.PathLen = len;
    e.NameOffset = 0;
    e.Type = type;
    e.Prot = (UBYTE)prot;
    e.Pad = 0;
    e.Size = size;
    e.Date = date;
    if (FuzzyAddEntry(w, &e)) entries++;
    else skipped++;
}

/* the drawer at path (len bytes long): its entries, then into its drawers */
static void walk(LONG len)
{
    struct ExAllControl *eac;
    struct ExAllData *buf, *ed;
    struct Item *items = NULL, **tail = &items, *it, *nx;
    BPTR lock;
    LONG more;
    if (broken()) return;
    if (!(lock = Lock((STRPTR)path, ACCESS_READ))) {
        if (!quiet) printf("updatedb: %s: cannot read it, left out\n", path);
        return;
    }
    if (!(eac = AllocDosObject(DOS_EXALLCONTROL, NULL)) || !(buf = AllocVec(EABUF, MEMF_ANY))) {
        if (eac) FreeDosObject(DOS_EXALLCONTROL, eac);
        UnLock(lock);
        return;
    }
    eac->eac_LastKey = 0;
    do {
        more = ExAll(lock, buf, EABUF, ED_DATE, eac);
        if (!more && IoErr() != ERROR_NO_MORE_ENTRIES) break;
        if (eac->eac_Entries == 0) continue;
        for (ed = buf; ed; ed = ed->ed_Next) {
            LONG k = strlen((char *)ed->ed_Name);
            if (!(it = AllocVec(sizeof(*it) + k, MEMF_ANY))) continue;
            it->next = NULL;
            it->type = ed->ed_Type;
            it->size = ed->ed_Size;
            it->prot = ed->ed_Prot;
            it->date = secs(ed->ed_Days, ed->ed_Mins, ed->ed_Ticks);
            memcpy(it->name, ed->ed_Name, k + 1);
            *tail = it;
            tail = &it->next;
        }
    } while (more);
    FreeVec(buf);
    FreeDosObject(DOS_EXALLCONTROL, eac);
    UnLock(lock);

    for (it = items; it; it = nx) {
        LONG k = strlen(it->name), nl = len;
        nx = it->next;
        if (!stopped && len + 1 + k <= MAXPATH) {
            if (path[len - 1] != ':') path[nl++] = '/';
            memcpy(path + nl, it->name, k + 1);
            nl += k;
            if (it->type == ST_SOFTLINK || it->type == ST_LINKDIR)
                add(nl, FET_LINK, it->prot, 0, it->date);          /* kept, not followed */
            else if (it->type > 0) {
                add(nl, FET_DRAWER, it->prot, 0, it->date);
                walk(nl);
            } else
                add(nl, FET_FILE, it->prot, it->size, it->date);
            path[len] = 0;
        } else if (!stopped)
            skipped++;
        FreeVec(it);
    }
}

/* the volumes to index when none are given */
static int realdisk(struct DosList *vol, struct DosList *list)
{
    struct DosList *d;
    for (d = list; d; d = (struct DosList *)BADDR(d->dol_Next)) {
        struct FileSysStartupMsg *fs;
        UBYTE *dev;
        if (d->dol_Type != DLT_DEVICE || d->dol_Task != vol->dol_Task) continue;
        /* a startup that is a small number or a string is not a disk */
        if ((ULONG)d->dol_misc.dol_handler.dol_Startup < 64) continue;
        fs = (struct FileSysStartupMsg *)BADDR(d->dol_misc.dol_handler.dol_Startup);
        if (((ULONG)fs & 1) || TypeOfMem(fs) == 0) continue;
        dev = (UBYTE *)BADDR(fs->fssm_Device);
        if (!dev || TypeOfMem(dev) == 0) continue;
        {
            int k = dev[0];
            if (k > 0 && dev[k] == 0) k--;          /* some count the 0 */
            if (k >= 8 && !memcmp(dev + 1 + k - 7, ".device", 7)) return 1;
        }
    }
    return 0;
}

static int findvolumes(char names[][64], ULONG *dates)
{
    struct DosList *dl, *list;
    int n = 0;
    dl = LockDosList(LDF_VOLUMES | LDF_DEVICES | LDF_READ);
    list = (struct DosList *)BADDR(((struct DosInfo *)BADDR(DOSBase->dl_Root->rn_Info))->di_DevInfo);
    while ((dl = NextDosEntry(dl, LDF_VOLUMES)) && n < MAXVOLS) {
        UBYTE *b = BADDR(dl->dol_Name);
        struct DateStamp *ds = &dl->dol_misc.dol_volume.dol_VolumeDate;
        if (!dl->dol_Task || !realdisk(dl, list)) continue;
        memcpy(names[n], b + 1, b[0] < 60 ? b[0] : 60);
        names[n][b[0] < 60 ? b[0] : 60] = 0;
        dates[n] = secs(ds->ds_Days, ds->ds_Minute, ds->ds_Tick);
        n++;
    }
    UnLockDosList(LDF_VOLUMES | LDF_DEVICES | LDF_READ);
    return n;
}

int main(void)
{
    static const char tmpl[] = "VOLUMES/M,TO/K,PRI/K/N,QUIET/S";
    LONG args[4] = { 0, 0, 0, 0 };
    struct RDArgs *rd;
    static char names[MAXVOLS][64];
    ULONG dates[MAXVOLS];
    char setting[512], where[MAXPATH];
    int n = 0, i, rc = 0;
    LONG err;
    struct DateStamp t0, t1;

    if (!(rd = ReadArgs((STRPTR)tmpl, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)"updatedb");
        return 10;
    }
    quiet = args[3] != 0;
    if (!(FuzzyBase = OpenLibrary(FUZZYNAME, 0))) {
        printf("updatedb: needs fuzzy.library\n");
        FreeArgs(rd);
        return 20;
    }
    if (args[2]) SetTaskPri(FindTask(NULL), *(LONG *)args[2]);

    /* which volumes: the arguments, the setting, or every real disk */
    if (args[0]) {
        STRPTR *v;
        for (v = (STRPTR *)args[0]; *v && n < MAXVOLS; v++, n++) {
            LONG k = strlen((char *)*v);
            while (k > 0 && ((*v)[k - 1] == ':' || (*v)[k - 1] == '/')) k--;
            memcpy(names[n], *v, k < 60 ? k : 60);
            names[n][k < 60 ? k : 60] = 0;
            dates[n] = 0;
        }
    } else if (GetVar((STRPTR)"FuzzyFind/Volumes", (STRPTR)setting, sizeof(setting), 0) > 0) {
        char *s = setting;
        while (*s && n < MAXVOLS) {
            LONG k = 0;
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;
            while (s[k] && s[k] != ' ' && s[k] != '\t' && s[k] != '\n') k++;
            if (!k) break;
            memcpy(names[n], s, k < 60 ? k : 60);
            names[n][k < 60 ? k : 60] = 0;
            while (names[n][0] && names[n][strlen(names[n]) - 1] == ':') names[n][strlen(names[n]) - 1] = 0;
            dates[n++] = 0;
            s += k;
        }
    } else
        n = findvolumes(names, dates);
    if (!n) {
        printf("updatedb: no volumes to index\n");
        CloseLibrary(FuzzyBase);
        FreeArgs(rd);
        return 10;
    }

    if (args[1]) strncpy(where, (char *)args[1], sizeof(where) - 1), where[sizeof(where) - 1] = 0;
    else FuzzyIndexPath((STRPTR)where, sizeof(where));
    if (!(w = FuzzyNewIndex((CONST_STRPTR)where, &err))) {
        printf("updatedb: %s: %s\n", where, FuzzyErrorText(err));
        CloseLibrary(FuzzyBase);
        FreeArgs(rd);
        return 20;
    }
    for (i = 0; i < n; i++) FuzzyAddVolume(w, (CONST_STRPTR)names[i], dates[i]);

    DateStamp(&t0);
    for (i = 0; i < n && !stopped; i++) {
        LONG before = entries, k = strlen(names[i]);
        memcpy(path, names[i], k);
        path[k] = ':';
        path[k + 1] = 0;
        add(k + 1, FET_DRAWER, 0, 0, dates[i]);
        walk(k + 1);
        if (!quiet && !stopped) printf("%s %ld\n", path, (long)(entries - before));
    }
    DateStamp(&t1);
    if (stopped) {
        FuzzyEndIndex(w, FALSE);
        printf("updatedb: stopped; the old index stays\n");
        rc = 5;
    } else if ((err = FuzzyEndIndex(w, TRUE)) != FERR_OK) {
        printf("updatedb: %s: %s\n", where, FuzzyErrorText(err));
        rc = 20;
    } else if (!quiet) {
        LONG ms = ((t1.ds_Days - t0.ds_Days) * 1440 + t1.ds_Minute - t0.ds_Minute) * 60000 +
                  (t1.ds_Tick - t0.ds_Tick) * 20;
        printf("%ld entries in %ld.%01ld s", (long)entries, (long)(ms / 1000), (long)(ms % 1000 / 100));
        if (skipped) printf(" (%ld paths too long, left out)", (long)skipped);
        printf(": %s\n", where);
    }
    CloseLibrary(FuzzyBase);
    FreeArgs(rd);
    return rc;
}
