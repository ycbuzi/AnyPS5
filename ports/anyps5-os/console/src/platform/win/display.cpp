#include "platform/Platform.h"

namespace platform {

int drmMaster(int) { return -1; }
int hotkeysOpen() { return 0; }
void hotkeysClose() {}
int hotkeysPoll() { return 0; }
const char* hotkeysLast() { return ""; }
bool isPsKey(int) { return false; }
bool overviewKey() { return false; }

}
