CC=gcc
CFLAGS=-Wall -Wextra -O2 -std=c99 -pedantic $(shell pkg-config --cflags gtk+-3.0)
LIBS=$(shell pkg-config --libs gtk+-3.0) -lm

TARGET=tsviewer
SRC=tsviewer_main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET) *.o

# Optional Windows cross-compilation: "make windows-zip".  Everything it needs
# lives in the windows directory, and nothing here depends on it being present.
-include windows/windows.mk

.PHONY: all clean
