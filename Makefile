CC = gcc
CFLAGS = -Wall -Wextra -Wconversion -Wdouble-promotion \
		 -Wno-unused-parameter -Wno-unused-function -Wno-sign-conversion \
		 -D TESTBENCH

RAYLIB_DIR   := C:/Users/jacob.stoker/scoop/apps/raylib-mingw/current/raylib-5.5_win64_mingw-w64

SRC_FILES := $(wildcard *.c) \

INCLUDES := -I"$(RAYLIB_DIR)/include"

LDFLAGS := -L"$(RAYLIB_DIR)/lib" -lraylib -lopengl32 -lgdi32 -lwinmm

OUT := chip8.exe

.PHONY: all clean

all:
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC_FILES) -o $(OUT) $(LDFLAGS)

clean:
	rm -f $(OUT)