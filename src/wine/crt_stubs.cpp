#include <windows.h>
#include <stdlib.h>

extern "C" {
int _crt_atexit(void (*)(void)) { return 0; }
_invalid_parameter_handler __cdecl _set_invalid_parameter_handler(_invalid_parameter_handler h) { return nullptr; }
int _configthreadlocale(int) { return 0; }
}
