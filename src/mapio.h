#ifndef MAPIO_H
#define MAPIO_H
#include <PalmOS.h>
#define MAP_IO_LIMIT 1048576UL
#define MAP_IO_NAME 32
/* Receive and its helpers stay in code 1: cold Exchange launches have no A5. */
typedef UInt32 (*MapReadProc)(void *, UInt8 *, UInt32, Err *);
Err MapImport(MapReadProc read, void *ctx, UInt32 length, const Char *name, Char *saved);
Err MapReceive(ExgSocketType *socket);
#define SEC_MAPIO __attribute__((section("mapio")))
void SEC_MAPIO MapRegister(void);
void SEC_MAPIO MapBrowse(void);
void SEC_MAPIO MapBeam(UInt16 index, const Char *name);
#endif
