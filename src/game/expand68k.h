// Shared by bot.c and expand68k.S, the 68k path search step: where the
// fields expand68k.S reads lie in a 68k GameState. bot.c checks each one
// against offsetof when it builds for the Palm.
#ifndef EXPAND68K_H
#define EXPAND68K_H

#define EX_SEARCH   2830    // GameState.search
#define EX_HIDDEN   19      // GameState.hiddenMines
#define EX_OX       2       // BotSearch.ox
#define EX_OY       3
#define EX_BOAT     6
#define EX_BESTH    8
#define EX_NEAR     10
#define EX_BEST     18
#define EX_COUNT    22
#define EX_FBASE    28
#define EX_CELL     54
#define EX_DANGER   566
#define EX_DISTANCE 822
#define EX_HEAP     2870

#endif
