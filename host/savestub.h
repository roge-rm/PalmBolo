#ifndef SAVE_TEST_STUB
#define SAVE_TEST_STUB
#include "mapstub/PalmOS.h"
#include "../src/game/game.h"
#ifndef Int8
typedef int8_t Int8;
#endif
#define SEC_FRONT
#define dmModeReadOnly 1
DmOpenRef DmOpenDatabaseByTypeCreator(UInt32, UInt32, UInt16);
Boolean SaveExists(void);
Boolean SaveWrite(const GameState *);
Boolean SaveRead(GameState *);
void SaveDrop(void);
void SaveAllow(Boolean);
Boolean SaveAllowed(void);
#endif
