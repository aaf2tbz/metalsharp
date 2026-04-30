WINE_ROOT = /Applications/Wine Stable.app/Contents/Resources/wine
MINGW = x86_64-w64-mingw32-g++
WINE_LIBS = $(WINE_ROOT)/lib/wine/x86_64-windows
WINE_UNIX = $(WINE_ROOT)/lib/wine/x86_64-unix

MINGW_CFLAGS = -shared -static-libgcc -static-libstdc++ -Wall -O2 \
	-Wl,--enable-stdcall-fixup \
	-D_WIN32 -D_WIN64 -D__USE_MINGW_ANSI_STDIO=1 \
	-I/opt/homebrew/Cellar/mingw-w64/14.0.0/toolchain-x86_64/x86_64-w64-mingw32/include

UNIX_CFLAGS = -shared -fPIC -Wall -O2 -fobjc-arc \
	-framework Metal -framework Foundation -framework QuartzCore -framework AppKit

OUTPUT_DIR = $(HOME)/.metalsharp/wine-dlls

.PHONY: all clean install

all: $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so

$(OUTPUT_DIR)/metalsharp_unix.so: metalsharp_unix.mm metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	clang++ $(UNIX_CFLAGS) -o $@ $<

$(OUTPUT_DIR)/d3d11.dll: d3d11_pe.cpp metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW) $(MINGW_CFLAGS) -o $@ $< -ldxgi

$(OUTPUT_DIR)/dxgi.dll: dxgi_pe.cpp metalsharp_unix.h
	mkdir -p $(OUTPUT_DIR)
	$(MINGW) $(MINGW_CFLAGS) -o $@ $<

install: all
	mkdir -p $(OUTPUT_DIR)
	cp $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so $(OUTPUT_DIR)/
	@echo "DLLs installed to $(OUTPUT_DIR)"
	@echo "Set WINEDLLOVERRIDES=d3d11,dxgi=n before running wine"

clean:
	rm -f $(OUTPUT_DIR)/d3d11.dll $(OUTPUT_DIR)/dxgi.dll $(OUTPUT_DIR)/metalsharp_unix.so
