# Compiler ##########################################################################################

cc ::= gcc 

# compiler flags shared between debug and release build
# (generate position independent code, use GNU/C23 standard, compile to object file without linking)
CC_FLAGS_BASE ::= -fPIC -std=gnu2x -c -pthreads

# flags for debug build (include debug information, optimization level 2, compiler warnings)
CC_FLAGS_DEBUG ::= $(CC_FLAGS_BASE) -g -O2 -Wall -Wpedantic -Wextra -Wno-unused-parameter

# flags for release build (strip symbols, optimization level 3)
CC_FLAGS_RELEASE ::= $(CC_FLAGS_BASE) -s -O3

# change this to $(CC_FLAGS_RELEASE) for release build
CC_FLAGS ::= $(CC_FLAGS_DEBUG)

CC ::= $(cc) $(CC_FLAGS)

# Linker ############################################################################################

ld ::= $(cc)

LD ::= $(ld)

# Object File and Executable ########################################################################
.PHONY: all lib

all: build/js

build/js: build/main.o build/js.o
	$(LD) build/main.o build/js.o -o build/js

build/main.o: src/*.h src/main.c | build
	$(CC) src/main.c -o build/main.o

build/js.o: src/*.h src/js.c | build
	$(CC) src/js.c -o build/js.o

build:
	mkdir -p build

lib: build/js.o

# Phony Targets #####################################################################################
.PHONY: clean run

clean:
	rm -f -r build/*

run: build/js
	build/js
