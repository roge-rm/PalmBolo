#ifndef TEST_BT_PALMOS_H
#define TEST_BT_PALMOS_H
#include "../netstub/PalmOS.h"
typedef void *MemHandle;
typedef struct { UInt16 eType; union { struct { UInt16 chr, keyCode, modifiers; } keyDown; } data; } EventType;
typedef EventType SysEventType;
enum { nilEvent, keyDownEvent, penDownEvent, appStopEvent };
#define libEvtHookKeyMask 0x0400
Err FtrGet(UInt32, UInt16, UInt32 *);
UInt16 SysTicksPerSecond(void);
void SysTaskDelay(Int32);
void EvtGetEvent(EventType *, Int32);
Err EvtAddEventToQueue(const EventType *);
Boolean SysHandleEvent(EventType *);
Err EvtDequeueKeyEvent(SysEventType *, UInt16);
#endif
