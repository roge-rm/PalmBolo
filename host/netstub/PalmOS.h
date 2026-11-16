#ifndef TEST_PALMOS_H
#define TEST_PALMOS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef int16_t Int16;
typedef int32_t Int32;
typedef uint16_t Err;
typedef char Char;
typedef uint8_t Boolean;
#define errNone 0
#define StrCopy strcpy
#define StrPrintF sprintf
#define MemSet(p,n,v) memset(p,v,n)
Err SysLibFind(const Char *, UInt16 *);
UInt32 TimGetSeconds(void);
UInt32 TimGetTicks(void);
#endif
