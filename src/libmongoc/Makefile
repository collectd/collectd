# MongoDB C Driver Makefile
#
# Copyright 2009, 2010 10gen Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Version
MONGO_MAJOR=0
MONGO_MINOR=6
MONGO_PATCH=0
BSON_MAJOR=$(MONGO_MAJOR)
BSON_MINOR=$(MONGO_MINOR)
BSON_PATCH=$(MONGO_PATCH)

# Library names
MONGO_LIBNAME=libmongoc
BSON_LIBNAME=libbson

# Standard or posix env.
ENV?=posix

# TODO: add replica set test, cpp test, platform tests, json_test
TESTS=test/auth_test test/bson_test test/bson_subobject_test test/count_delete_test \
  test/cursors_test test/endian_swap_test test/errors_test test/examples_test \
  test/functions_test test/gridfs_test test/helpers_test \
  test/oid_test test/resize_test test/simple_test test/sizes_test test/update_test \
  test/validate_test test/write_concern_test test/commands_test
MONGO_OBJECTS=src/bson.o src/encoding.o src/gridfs.o src/md5.o src/mongo.o \
 src/numbers.o
BSON_OBJECTS=src/bson.o src/numbers.o src/encoding.o

ifeq ($(ENV),posix)
    TESTS+=test/env_posix_test
    MONGO_OBJECTS+=src/env_posix.o
else
    MONGO_OBJECTS+=src/env_standard.o
endif

DYN_MONGO_OBJECTS=$(foreach i,$(MONGO_OBJECTS),$(patsubst %.o,%.os,$(i)))
DYN_BSON_OBJECTS=$(foreach i,$(BSON_OBJECTS),$(patsubst %.o,%.os,$(i)))

# Compile flags
ALL_DEFINES=$(DEFINES)
ALL_DEFINES+=-D_POSIX_SOURCE
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
DYN_FLAGS:=-fPIC -DMONGO_DLL_BUILD

# Endianness check
endian := $(shell sh -c 'echo "ab" | od -x | grep "6261" >/dev/null && echo little || echo big')
ifeq ($(endian),big)
    ALL_DEFINES+=-DMONGO_BIG_ENDIAN
endif

# Int64 type check
int64:=$(shell ./check_int64.sh $(CC) stdint.h && echo stdint)
ifeq ($(int64),stdint)
    ALL_DEFINES+=-DMONGO_HAVE_STDINT
else
    int64:=$(shell ./check_int64.sh $(CC) unistd.h && echo unistd)
    ifeq ($(int64),unistd)
        ALL_DEFINES+=-DMONGO_HAVE_UNISTD
    endif
endif
$(shell rm header_check.tmp tmp.c)

TEST_DEFINES=$(ALL_DEFINES)
TEST_DEFINES+=-DTEST_SERVER="\"127.0.0.1\""

OPTIMIZATION?=-O3
WARNINGS?=-Wall
DEBUG?=-ggdb
STD?=c99
PEDANTIC?=-pedantic
ALL_CFLAGS=-std=$(STD) $(PEDANTIC) $(CFLAGS) $(OPTIMIZATION) $(WARNINGS) $(DEBUG) $(ALL_DEFINES)
ALL_LDFLAGS=$(LDFLAGS)

# Shared libraries
DYLIBSUFFIX=so
STLIBSUFFIX=a

MONGO_DYLIBNAME=$(MONGO_LIBNAME).$(DYLIBSUFFIX)
MONGO_DYLIB_MAJOR_NAME=$(MONGO_DYLIBNAME).$(MONGO_MAJOR)
MONGO_DYLIB_MINOR_NAME=$(MONGO_DYLIB_MAJOR_NAME).$(MONGO_MINOR)
MONGO_DYLIB_PATCH_NAME=$(MONGO_DYLIB_MINOR_NAME).$(MONGO_PATCH)
MONGO_DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,$(MONGO_DYLIB_MINOR_NAME) -o $(MONGO_DYLIBNAME) $(ALL_LDFLAGS) $(DYN_MONGO_OBJECTS)

BSON_DYLIBNAME=$(BSON_LIBNAME).$(DYLIBSUFFIX)
BSON_DYLIB_MAJOR_NAME=$(BSON_DYLIBNAME).$(BSON_MAJOR)
BSON_DYLIB_MINOR_NAME=$(BSON_DYLIB_MAJOR_NAME).$(BSON_MINOR)
BSON_DYLIB_PATCH_NAME=$(BSON_DYLIB_MINOR_NAME).$(BSON_PATCH)
BSON_DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,$(BSON_DYLIB_MINOR_NAME) -o $(BSON_DYLIBNAME) $(ALL_LDFLAGS) $(DYN_BSON_OBJECTS)

# Static libraries
MONGO_STLIBNAME=$(MONGO_LIBNAME).$(STLIBSUFFIX)
BSON_STLIBNAME=$(BSON_LIBNAME).$(STLIBSUFFIX)

# Overrides
kernel_name := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(kernel_name),SunOS)
    ALL_LDFLAGS+=-ldl -lnsl -lsocket
    INSTALL_CMD=cp -r
    MONGO_DYLIB_MAKE_CMD=$(CC) -G -o $(MONGO_DYLIBNAME) -h $(MONGO_DYLIB_MINOR_NAME) $(ALL_LDFLAGS)
    BSON_DYLIB_MAKE_CMD=$(CC) -G -o $(BSON_DYLIBNAME) -h $(BSON_DYLIB_MINOR_NAME) $(ALL_LDFLAGS)
endif
ifeq ($(kernel_name),Darwin)
    ALL_CFLAGS+=-std=$(STD) $(CFLAGS) $(OPTIMIZATION) $(WARNINGS) $(DEBUG) $(ALL_DEFINES)
    DYLIBSUFFIX=dylib
    MONGO_DYLIB_MINOR_NAME=$(MONGO_LIBNAME).$(DYLIBSUFFIX).$(MONGO_MAJOR).$(MONGO_MINOR)
    MONGO_DYLIB_MAJOR_NAME=$(MONGO_LIBNAME).$(DYLIBSUFFIX).$(MONGO_MAJOR)
    MONGO_DYLIB_MAKE_CMD=$(CC) -shared -Wl,-install_name,$(MONGO_DYLIB_MINOR_NAME) -o $(MONGO_DYLIBNAME)

    BSON_DYLIB_MINOR_NAME=$(BSON_LIBNAME).$(DYLIBSUFFIX).$(BSON_MAJOR).$(BSON_MINOR)
    BSON_DYLIB_MAJOR_NAME=$(BSON_LIBNAME).$(DYLIBSUFFIX).$(BSON_MAJOR)
    BSON_DYLIB_MAKE_CMD=$(CC) -shared -Wl,-install_name,$(BSON_DYLIB_MINOR_NAME) -o $(BSON_DYLIBNAME)
endif

# Installation
ifeq ($(kernel_name),SunOS)
    INSTALL?=cp -r
endif
INSTALL?= cp -a
INSTALL_INCLUDE_PATH?=/usr/local/include
INSTALL_LIBRARY_PATH?=/usr/local/lib

# TARGETS
all: $(MONGO_DYLIBNAME) $(BSON_DYLIBNAME) $(MONGO_STLIBNAME) $(BSON_STLIBNAME)

# Dependency targets. Run 'make deps' to generate these.
bson.o: src/bson.c src/bson.h src/encoding.h
encoding.o: src/encoding.c src/bson.h src/encoding.h
env_standard.o: src/env_standard.c src/env.h src/mongo.h src/bson.h
env_posix.o: src/env_posix.c src/env.h src/mongo.h src/bson.h
gridfs.o: src/gridfs.c src/gridfs.h src/mongo.h src/bson.h
md5.o: src/md5.c src/md5.h
mongo.o: src/mongo.c src/mongo.h src/bson.h src/md5.h src/env.h
numbers.o: src/numbers.c

$(MONGO_DYLIBNAME): $(DYN_MONGO_OBJECTS)
	$(MONGO_DYLIB_MAKE_CMD)

$(MONGO_STLIBNAME): $(MONGO_OBJECTS)
	$(AR) -rs $@ $(MONGO_OBJECTS)

$(BSON_DYLIBNAME): $(DYN_BSON_OBJECTS)
	$(BSON_DYLIB_MAKE_CMD)

$(BSON_STLIBNAME): $(BSON_OBJECTS)
	$(AR) -rs $@ $(BSON_OBJECTS)

install:
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) src/mongo.h src/bson.h $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(MONGO_DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(MONGO_DYLIB_PATCH_NAME)
	$(INSTALL) $(BSON_DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(BSON_DYLIB_PATCH_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(MONGO_DYLIB_PATCH_NAME) $(MONGO_DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(BSON_DYLIB_PATCH_NAME) $(BSON_DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(MONGO_DYLIB_MINOR_NAME) $(MONGO_DYLIBNAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(BSON_DYLIB_MINOR_NAME) $(BSON_DYLIBNAME)
	$(INSTALL) $(MONGO_STLIBNAME) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) $(BSON_STLIBNAME) $(INSTALL_LIBRARY_PATH)

test: $(TESTS)
	sh runtests.sh

valgrind: $(TESTS)
	sh runtests.sh -v

docs:
	python docs/buildscripts/docs.py

clean:
	rm -rf $(MONGO_DYLIBNAME) $(MONGO_STLIBNAME) $(BSON_DYLIBNAME) $(BSON_STLIBNAME) src/*.o src/*.os test/*_test

deps:
	$(CC) -MM -DMONGO_HAVE_STDINT src/*.c

32bit:
	$(MAKE) CFLAGS="-m32" LDFLAGS="-pg"

%_test: %_test.c $(MONGO_STLIBNAME)
	$(CC) -o $@ -L. -Isrc $(TEST_DEFINES) $(ALL_LDFLAGS) $< $(MONGO_STLIBNAME)

%.o: %.c
	$(CC) -o $@ -c $(ALL_CFLAGS) $<

%.os: %.c
	$(CC) -o $@ -c $(ALL_CFLAGS) $(DYN_FLAGS) $<

.PHONY: clean docs test
