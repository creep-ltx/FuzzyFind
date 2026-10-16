/* fzf.h - fzf's actions and its keys (keys.c). */
#ifndef FZF_H
#define FZF_H

enum {
    A_NONE, A_ACCEPT, A_ABORT, A_UP, A_DOWN, A_PAGEUP, A_PAGEDOWN, A_FIRST, A_LAST,
    A_BACKSPACE, A_CLEAR, A_DELWORD, A_REDRAW, A_COUNT
};

void keys_load(void);
int keys_find(int key);

#endif
