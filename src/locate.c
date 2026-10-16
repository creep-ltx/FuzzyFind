/* locate - FuzzyFind: the paths in the index that match.
 *
 *   locate PATTERN... [FUZZY] [LIMIT=n] [FILES|DRAWERS] [NAME] [CASE]
 *          [COUNT] [INDEX=file]
 *
 * Plain (the default, as Unix locate): every path with the text in it, any
 * case, accents folded (goteborg finds Göteborg), several words all
 * present; in the index's order. An Amiga pattern (#?.mod) is matched
 * against the name instead. FUZZY: the letters in order with gaps, the
 * best first (fzf's ranking), LIMIT of them (20 unless given). One path a
 * line, for scripts and pipes. Return code 0 if anything matched, 5 if
 * nothing did. */
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/fuzzy.h>
#include "version.h"

static const char __attribute__((used)) vers[] = "$VER: locate " FF_VERSTR " (" FF_DATE ")";

struct Library *FuzzyBase;

enum { A_PAT, A_FUZZY, A_LIMIT, A_FILES, A_DRAWERS, A_NAME, A_CASE, A_COUNT, A_INDEX, A_N };

int main(void)
{
    static const char tmpl[] = "PATTERN/M/A,FUZZY=F/S,LIMIT=N/K/N,FILES/S,DRAWERS/S,NAME/S,CASE/S,"
                               "COUNT/S,INDEX/K";
    LONG args[A_N];
    struct RDArgs *rd;
    struct FuzzyIndex *x;
    STRPTR *w;
    char pattern[512], *pp = pattern, *wild = NULL;
    LONG err, found = 0, i;
    ULONG pflags, sflags = 0;
    int rc = 5;

    memset(args, 0, sizeof(args));
    if (!(rd = ReadArgs((STRPTR)tmpl, args, NULL))) {
        PrintFault(IoErr(), (STRPTR)"locate");
        return 10;
    }
    if (!(FuzzyBase = OpenLibrary(FUZZYNAME, 0))) {
        printf("locate: needs fuzzy.library\n");
        FreeArgs(rd);
        return 20;
    }
    for (w = (STRPTR *)args[A_PAT]; *w; w++) {          /* the words, one pattern */
        LONG k = strlen((char *)*w);
        if (pp + k + 2 > pattern + sizeof(pattern)) break;
        if (pp != pattern) *pp++ = ' ';
        memcpy(pp, *w, k);
        pp += k;
    }
    *pp = 0;
    pflags = args[A_CASE] ? FPF_CASE : 0;
    if (args[A_FILES]) sflags |= FSF_FILES;
    if (args[A_DRAWERS]) sflags |= FSF_DRAWERS;
    if (args[A_NAME]) sflags |= FSF_NAME;

    /* an Amiga pattern (#? and friends): matched against each name */
    if (!args[A_FUZZY]) {
        LONG plen = strlen(pattern) * 2 + 2;
        if ((wild = AllocVec(plen, MEMF_ANY)) &&
            (args[A_CASE] ? ParsePattern((STRPTR)pattern, (STRPTR)wild, plen)
                          : ParsePatternNoCase((STRPTR)pattern, (STRPTR)wild, plen)) != 1) {
            FreeVec(wild);
            wild = NULL;
        }
    }

    if (!(x = FuzzyOpenIndex((CONST_STRPTR)args[A_INDEX], &err))) {
        char where[256];
        if (args[A_INDEX]) strncpy(where, (char *)args[A_INDEX], sizeof(where) - 1), where[255] = 0;
        else FuzzyIndexPath((STRPTR)where, sizeof(where));
        printf("locate: %s: %s%s\n", where, FuzzyErrorText(err),
               err == FERR_NOFILE ? " (run updatedb to make it)" : "");
        if (wild) FreeVec(wild);
        CloseLibrary(FuzzyBase);
        FreeArgs(rd);
        return 20;
    }

    if (args[A_FUZZY]) {                    /* the best first */
        LONG limit = args[A_LIMIT] ? *(LONG *)args[A_LIMIT] : 20;
        struct FuzzyResults *r = FuzzySearch(x, (CONST_STRPTR)pattern, pflags | sflags, limit > 0 ? limit : 20);
        if (r) {
            if (args[A_COUNT]) printf("%ld\n", (long)r->Matched);
            else
                for (i = 0; i < r->Count && !(SetSignal(0, 0) & SIGBREAKF_CTRL_C); i++)
                    printf("%s\n", r->Hits[i].Path);
            found = r->Matched;
            FuzzyFreeResults(r);
        }
    } else {                                /* plain: in the index's order */
        FuzzyPattern *p = FuzzyPrepare((CONST_STRPTR)pattern, pflags | FPF_PLAIN);
        struct FuzzyEntry e;
        LONG limit = args[A_LIMIT] ? *(LONG *)args[A_LIMIT] : 0x7FFFFFFF;
        while (p && FuzzyNextEntry(x, &e)) {
            const char *t = (const char *)e.Path;
            LONG len = e.PathLen;
            if ((sflags & FSF_FILES) && e.Type != FET_FILE) continue;
            if ((sflags & FSF_DRAWERS) && e.Type != FET_DRAWER) continue;
            if (wild) {
                if (!(args[A_CASE] ? MatchPattern((STRPTR)wild, (STRPTR)t + e.NameOffset)
                                   : MatchPatternNoCase((STRPTR)wild, (STRPTR)t + e.NameOffset)))
                    continue;
            } else {
                if (sflags & FSF_NAME) {
                    t += e.NameOffset;
                    len -= e.NameOffset;
                }
                if (FuzzyScore(p, (CONST_STRPTR)t, len, NULL, 0) <= 0) continue;
            }
            found++;
            if (!args[A_COUNT] && found <= limit) printf("%s\n", e.Path);
            if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
                printf("***Break\n");
                break;
            }
        }
        if (args[A_COUNT]) printf("%ld\n", (long)found);
        FuzzyFreePattern(p);
    }
    if (found) rc = 0;
    FuzzyCloseIndex(x);
    if (wild) FreeVec(wild);
    CloseLibrary(FuzzyBase);
    FreeArgs(rd);
    return rc;
}
