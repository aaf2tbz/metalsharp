#include <windows.h>
void* operator new(unsigned int sz) { return HeapAlloc(GetProcessHeap(), 0, sz); }
void operator delete(void* p) { if(p) HeapFree(GetProcessHeap(), 0, p); }
void operator delete(void* p, unsigned int) { if(p) HeapFree(GetProcessHeap(), 0, p); }
void* operator new[](unsigned int sz) { return HeapAlloc(GetProcessHeap(), 0, sz); }
void operator delete[](void* p) { if(p) HeapFree(GetProcessHeap(), 0, p); }
void operator delete[](void* p, unsigned int) { if(p) HeapFree(GetProcessHeap(), 0, p); }
