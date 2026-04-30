WINE_ROOT = /Applications/Wine Stable.app/Contents/Resources/wine
MINGW = x86_64-w64-mingw32-g++
WINE_LIBS = $(WINE_ROOT)/lib/wine/x86_64-windows
WINE_UNIX = $(WINE_ROOT)/lib/wine/x86_64-unix
MOJO_DIR = mojoshader

MINGW_CFLAGS = -shared -static -Wall -O2 \
	-Wl,--enable-stdcall-fixup \
	-D_WIN32 -D_WIN64 -D__USE_MINGW_ANSI_STDIO=1 \
	-I/opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64/x86_64-w64-mingw32/include

UNIX_CFLAGS = -arch x86_64 -shared -fPIC -Wall -O2 -fobjc-arc \
	-framework Metal -framework Foundation -framework QuartzCore -framework AppKit

MOJO_CFLAGS = -arch x86_64 -O2 -DMOJOSHADER_NO_VERSION_INCLUDE \
	-I$(MOJO_DIR) -I$(MOJO_DIR)/profiles

OUTPUT_DIR = $(HOME)/.metalsharp/wine-dlls

MOJO_OBJS = /tmp/mojo_main.o /tmp/mojo_common.o /tmp/mojo_prof_common.o \
	/tmp/mojo_prof_bytecode.o /tmp/mojo_prof_metal.o /tmp/mojo_prof_glsl.o \
	/tmp/mojo_prof_hlsl.o /tmp/mojo_prof_arb1.o /tmp/mojo_prof_d3d.o \
	/tmp/mojo_prof_spirv.o

.PHONY: all clean install

all: $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so $(OUTPUT_DIR)/d3d9.dll $(OUTPUT_DIR)/d3d9_unix.so

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
	$(MINGW) $(MINGW_CFLAGS) -o $@ $< -ldxgi

$(OUTPUT_DIR)/dxgi.dll: dxgi_pe.cpp metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW) $(MINGW_CFLAGS) -o $@ $<

$(OUTPUT_DIR)/d3d9.dll: d3d9_pe.cpp d3d9_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW) $(MINGW_CFLAGS) -o $@ $<

$(OUTPUT_DIR)/d3d9_unix.so: d3d9_unix.mm d3d9_unix.h $(MOJO_OBJS)
	mkdir -p $(OUTPUT_DIR)
	clang++ $(UNIX_CFLAGS) -DMOJOSHADER_NO_VERSION_INCLUDE -I$(MOJO_DIR) -I$(MOJO_DIR)/profiles -o $@ $< $(MOJO_OBJS)

install: all
	for f in d3d9.dll d3d11.dll dxgi.dll; do rm -f "$(WINE_LIBS)/$$f"; cp "$(OUTPUT_DIR)/$$f" "$(WINE_LIBS)/$$f"; done
	rm -f "$(WINE_UNIX)/d3d9.so"; cp "$(OUTPUT_DIR)/d3d9_unix.so" "$(WINE_UNIX)/d3d9.so"
	rm -f "$(WINE_UNIX)/d3d11.so"; cp "$(OUTPUT_DIR)/metalsharp_unix.so" "$(WINE_UNIX)/d3d11.so"
	@python3 patch_builtin.py "$(WINE_LIBS)"
	@echo "Installed to Wine (PE + unix .so + builtin signature)"
	@echo "Run with: WINEPREFIX=~/.metalsharp/prefix WINEDLLOVERRIDES=d3d9,d3d11,dxgi=b,n wine <exe>"

clean:
	rm -f $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so $(OUTPUT_DIR)/d3d9.dll $(OUTPUT_DIR)/d3d9_unix.so
