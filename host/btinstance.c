// Compile the production adapter once per simulated Palm, each with its own globals.
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#define NAME(s) CAT(CAT(bt, BT_INSTANCE), s)
#define bt_available NAME(_available)
#define bt_open NAME(_open)
#define bt_close NAME(_close)
#define bt_send NAME(_send)
#define bt_recv NAME(_recv)
#define bt_sync NAME(_sync)
#define bt_service NAME(_service)
#define bt_error NAME(_error)
#define bt_status NAME(_status)
#include "../src/net/bluetooth.c"
