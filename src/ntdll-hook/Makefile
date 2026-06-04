CC = x86_64-w64-mingw32-gcc
CFLAGS = -Wall -Wextra -O2 -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE
LDFLAGS = -shared -static-libgcc -static-libstdc++
TARGET = metalsharp_ntdll_hook.dll

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ -lws2_32 -lntdll

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) src/*.o

.PHONY: all clean
