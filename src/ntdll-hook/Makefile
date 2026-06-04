CC = x86_64-w64-mingw32-gcc
WINE_ROOT = $(HOME)/.metalsharp/runtime/wine
WINE_LIBS = $(WINE_ROOT)/lib/wine/x86_64-windows
WINEBUILD = $(WINE_ROOT)/bin/winebuild
CFLAGS = -Wall -Wextra -O2 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
LDFLAGS = -shared -static-libgcc -static-libstdc++ \
	-L$(WINE_LIBS)
TARGET = metalsharp_ntdll_hook.dll
TARGET_RAW = metalsharp_ntdll_hook.dll.raw

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET_RAW): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lws2_32 -lkernel32

$(TARGET): $(TARGET_RAW)
	cp $< $@
	$(WINEBUILD) --builtin $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(TARGET_RAW) src/*.o

.PHONY: all clean
