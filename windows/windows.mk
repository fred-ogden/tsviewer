# Windows cross-compilation, included by the top-level Makefile.
#
#   make windows-zip    build tsviewer-<version>-win64.zip
#
# Requires a mingw-w64 cross compiler, wget, unzip and zip.  The GTK3 headers
# and DLLs are downloaded as a prebuilt bundle from petabyt/windows-gtk; only
# win64-gtk-2021.zip there carries the GTK 3.24 DLLs this program needs.

WIN_CC?=x86_64-w64-mingw32-gcc
WIN_GTK_URL=https://github.com/petabyt/windows-gtk/raw/master/win64-gtk-2021.zip

# The bundle unpacks into a directory named "win32" even though it is 64-bit
WIN_SDK=windows/win32
WIN_CFLAGS=-Wall -Wextra -O2 -std=gnu99 -D__USE_MINGW_ANSI_STDIO=1 -I$(WIN_SDK)/include
WIN_LIBS=$(WIN_SDK)/lib/*.dll -lm

VERSION=$(shell sed -n 's/^\#define TSVIEWER_VERSION "\(.*\)"/\1/p' $(SRC))
WIN_PKG=$(TARGET)-$(VERSION)-win64

# tsviewer.exe, the GTK3 DLLs it needs beside it, and the examples, in one zip
windows-zip: $(WIN_PKG).zip

$(WIN_PKG).zip: $(SRC) windows/tsviewer_win32_compat.h | $(WIN_SDK)
	rm -rf windows/$(WIN_PKG) $@
	mkdir -p windows/$(WIN_PKG)
	$(WIN_CC) $(WIN_CFLAGS) -o windows/$(WIN_PKG)/$(TARGET).exe $(SRC) $(WIN_LIBS)
	cp $(WIN_SDK)/lib/*.dll windows/$(WIN_PKG)/
	cp -r examples windows/$(WIN_PKG)/examples
	cd windows && zip -q -r ../$@ $(WIN_PKG)
	rm -rf windows/$(WIN_PKG)

$(WIN_SDK):
	wget -4 -O windows/gtk.zip $(WIN_GTK_URL)
	unzip -q windows/gtk.zip -d windows
	rm -f windows/gtk.zip

# Adds itself to the top-level "clean"; the downloaded bundle is kept
clean: clean-windows

clean-windows:
	rm -rf windows/$(WIN_PKG) $(WIN_PKG).zip

.PHONY: windows-zip clean-windows
