WINE_ROOT ?= $(HOME)/.metalsharp/runtime/wine
MINGW32 = i686-w64-mingw32-g++
MINGW64 = x86_64-w64-mingw32-g++
WINE_LIBS32 = $(WINE_ROOT)/lib/wine/i386-windows
WINE_LIBS64 = $(WINE_ROOT)/lib/wine/x86_64-windows
WINE_UNIX64 = $(WINE_ROOT)/lib/wine/x86_64-unix
MOJO_DIR = mojoshader
MINGW_INC32 = /opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64/x86_64-w64-mingw32/include
MINGW_INC64 = /opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64/x86_64-w64-mingw32/include

MINGW64_CFLAGS = -shared -static -Wall -O2 \
	-Wl,--enable-stdcall-fixup \
	-D_WIN32 -D_WIN64 -D__USE_MINGW_ANSI_STDIO=1 \
	-I$(MINGW_INC64)

MINGW32_CFLAGS = -shared -Wall -O2 -static-libgcc -static-libstdc++ \
	-fno-exceptions -fno-rtti \
	-Wl,--enable-stdcall-fixup,--kill-at \
	-D_WIN32 -D__USE_MINGW_ANSI_STDIO=1 \
	-I$(MINGW_INC32)

UNIX_CFLAGS = -target x86_64-apple-macosx11.0 -bundle -Wall -O2 -fobjc-arc \
	-framework Metal -framework Foundation -framework QuartzCore -framework AppKit

MOJO_CFLAGS = -arch x86_64 -O2 -DMOJOSHADER_NO_VERSION_INCLUDE \
	-I$(MOJO_DIR) -I$(MOJO_DIR)/profiles

OUTPUT_DIR = $(HOME)/.metalsharp/wine-dlls

MOJO_OBJS = /tmp/mojo_main.o /tmp/mojo_common.o /tmp/mojo_prof_common.o \
	/tmp/mojo_prof_bytecode.o /tmp/mojo_prof_metal.o /tmp/mojo_prof_glsl.o \
	/tmp/mojo_prof_hlsl.o /tmp/mojo_prof_arb1.o /tmp/mojo_prof_d3d.o \
	/tmp/mojo_prof_spirv.o

.PHONY: all clean install

all: $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so $(OUTPUT_DIR)/d3d9.dll $(OUTPUT_DIR)/d3d9.so

/tmp/mojo_main.o: $(MOJO_DIR)/mojoshader.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_common.o: $(MOJO_DIR)/mojoshader_common.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_common.o: $(MOJO_DIR)/profiles/mojoshader_profile_common.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_bytecode.o: $(MOJO_DIR)/profiles/mojoshader_profile_bytecode.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_metal.o: $(MOJO_DIR)/profiles/mojoshader_profile_metal.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_glsl.o: $(MOJO_DIR)/profiles/mojoshader_profile_glsl.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_hlsl.o: $(MOJO_DIR)/profiles/mojoshader_profile_hlsl.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_arb1.o: $(MOJO_DIR)/profiles/mojoshader_profile_arb1.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_d3d.o: $(MOJO_DIR)/profiles/mojoshader_profile_d3d.c
	clang $(MOJO_CFLAGS) -c $< -o $@

/tmp/mojo_prof_spirv.o: $(MOJO_DIR)/profiles/mojoshader_profile_spirv.c
	clang $(MOJO_CFLAGS) -c $< -o $@

$(OUTPUT_DIR)/metalsharp_unix.so: metalsharp_unix.mm metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	clang++ $(UNIX_CFLAGS) -o $@ $<

$(OUTPUT_DIR)/d3d11.dll: d3d11_pe.cpp metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW64) $(MINGW64_CFLAGS) -o $@ $< -ldxgi

$(OUTPUT_DIR)/dxgi.dll: dxgi_pe.cpp metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW64) $(MINGW64_CFLAGS) -o $@ $<

/tmp/d3d9_read_stub.o: read_stub.c
	i686-w64-mingw32-gcc -c -O2 $< -o $@

/tmp/d3d9_ms_newdel.o: ms_newdel.cpp
	i686-w64-mingw32-g++ -c -O2 -fno-exceptions -fno-rtti $< -o $@

$(OUTPUT_DIR)/d3d9.dll: d3d9_pe.cpp d3d9_unix.h /tmp/d3d9_ms_newdel.o /tmp/d3d9_read_stub.o
	mkdir -p $(OUTPUT_DIR)
	$(MINGW32) $(MINGW32_CFLAGS) -o $@ $< /tmp/d3d9_ms_newdel.o /tmp/d3d9_read_stub.o \
		-L$(WINE_LIBS32) -lwinecrt0 -lntdll
	$(WINE_ROOT)/bin/winebuild --builtin $@

$(OUTPUT_DIR)/d3d9.so: d3d9_unix.mm d3d9_unix.h $(MOJO_OBJS)
	mkdir -p $(OUTPUT_DIR)
	clang++ $(UNIX_CFLAGS) -DMOJOSHADER_NO_VERSION_INCLUDE -I$(MOJO_DIR) -I$(MOJO_DIR)/profiles -o $@ $< $(MOJO_OBJS)

install: all
	for f in d3d11.dll dxgi.dll; do cp "$(OUTPUT_DIR)/$$f" "$(WINE_LIBS64)/$$f"; done
	cp "$(OUTPUT_DIR)/d3d9.dll" "$(WINE_LIBS32)/d3d9.dll"
	cp "$(OUTPUT_DIR)/d3d9.so" "$(WINE_UNIX64)/d3d9.so"
	cp "$(OUTPUT_DIR)/metalsharp_unix.so" "$(WINE_UNIX64)/d3d11.so"
	@python3 patch_builtin.py "$(WINE_LIBS64)"
	@python3 patch_builtin.py "$(WINE_LIBS32)"
	@echo "Installed to MetalSharp Wine runtime"

clean:
	rm -f $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so $(OUTPUT_DIR)/d3d9.dll $(OUTPUT_DIR)/d3d9.so
	rm -f /tmp/d3d9_read_stub.o /tmp/d3d9_ms_newdel.o
