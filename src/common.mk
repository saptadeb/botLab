############################################################
#
# This file should only contain CFLAGS_XXX and LDFLAGS_XXX directives.
# CFLAGS and LDFLAGS themselves should NOT be set: that is the job
# for the actual Makefiles (which will combine the flags given here)
#
# *** DO NOT SET CFLAGS or LDFLAGS in this file ***
#
# Our recommended flags for all projects. Note -pthread specifies reentrancy
############################################################

# -Wno-format-zero-length: permit printf("");
# -Wno-unused-parameter: permit a function to ignore an argument

ROOT_PATH    := $(subst /src/common.mk,,$(realpath $(lastword $(MAKEFILE_LIST))))
SRC_PATH     := $(ROOT_PATH)/src
BIN_PATH     := $(ROOT_PATH)/bin
LIB_PATH     := $(ROOT_PATH)/lib
JAVA_PATH    := $(ROOT_PATH)/java
MATLAB_PATH  := $(ROOT_PATH)/matlab
SOLNS_PATH   := $(ROOT_PATH)/solns
CONFIG_DIR   := $(shell pwd)/../../config

CFLAGS_SHARED = -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_REENTRANT \
		-Wall -Wno-unused-parameter -Wno-deprecated-declarations -pthread -fPIC -I$(SRC_PATH)

CFLAGS_STD   := -std=gnu99 -Wno-format-zero-length $(CFLAGS_SHARED)
CXXFLAGS_STD := -std=c++0x $(CFLAGS_SHARED)
LDFLAGS_STD  := -lm -lpthread

CC           := gcc
CXX          := g++
LD           := gcc

#.SILENT:

# dynamic libraries
ifeq "$(shell uname -s)" "Darwin"
	LDSH := -dynamic
	SHEXT := .dylib
	WHOLE_ARCHIVE_START := -all_load
else
	LD := gcc
	LDSH := -shared
	SHEXT := .so
	WHOLE_ARCHIVE_START := -Wl,-whole-archive
	WHOLE_ARCHIVE_STOP := -Wl,-no-whole-archive
endif

############################################################
#
# External libraries
#
# List these in roughly the order of dependency; those with fewest
# dependencies first. Within each LDFLAGS, list the dependencies in
# decreasing order (e.g., end with LDFLAGS_GLIB)
#
############################################################

# rplidar
LDFLAGS_RPLIDAR := $(LIB_PATH)/librplidar_sdk.a

# glib
CFLAGS_GLIB  := `pkg-config --cflags glib-2.0 gmodule-2.0`
LDFLAGS_GLIB := `pkg-config --libs glib-2.0 gmodule-2.0 gthread-2.0 gobject-2.0`

# gsl
CFLAGS_GSL   := -DHAVE_INLINE `pkg-config --cflags gsl`
LDFLAGS_GSL   := `pkg-config --libs gsl`

# jpeg
ifeq "$(shell test -f /usr/lib/libjpeg-ipp.so -o -f /usr/lib64/libjpeg-ipp.so && echo ipp)" "ipp"
	LDFLAGS_JPEG := -ljpeg-ipp
else
	LDFLAGS_JPEG := -ljpeg
endif

# gtk
CFLAGS_GTK   :=`pkg-config --cflags gtk+-2.0`
LDFLAGS_GTK  :=`pkg-config --libs gtk+-2.0 gthread-2.0`

# lcm
CFLAGS_LCM  := `pkg-config --cflags lcm`
LDFLAGS_LCM := `pkg-config --libs lcm`

# Open GL
CFLAGS_GL    :=
LDFLAGS_GL   := -lGLU -lglut

# libusb-1.0
CFLAGS_USB := `pkg-config --cflags libusb-1.0`
LDFLAGS_USB := `pkg-config --libs libusb-1.0`

# libpng
CFLAGS_PNG := `pkg-config --cflags libpng`
LDFLAGS_PNG := `pkg-config --libs libpng`

# dc1394
CFLAGS_DC1394 := `pkg-config --cflags libdc1394-2`
LDFLAGS_DC1394 := `pkg-config --libs libdc1394-2`

# Intel Integrated Performance Primitives
IPPA:=
IPP_LIBS:=-lguide -lippcore -lippi -lippcc -lippcv
ifeq "$(shell uname -s)" "Darwin"
    IPP_BASE:=/Library/Frameworks/Intel_IPP.framework
    CFLAGS_IPP:=-I$(IPP_BASE)/Headers
    LDFLAGS_IPP:=-L$(IPP_BASE)/Libraries $(IPP_LIBS)
else
    ifeq "$(shell uname -m)" "x86_64"
        IPP_SEARCH := /usr/local/intel/ipp/5.1/em64t \
                      /opt/intel/ipp/5.2/em64t       \
		      /opt/intel/ipp/5.3/em64t       \
		      /opt/intel/ipp/5.3.1.062/em64t \
	              /opt/intel/ipp/5.3.2.068/em64t
        test_dir = $(shell [ -e $(dir) ] && echo $(dir))
        IPP_SEARCH := $(foreach dir, $(IPP_SEARCH), $(test_dir))
        IPP_BASE := $(firstword $(IPP_SEARCH))
        IPP_LIBS:=-lguide -lippcoreem64t -lippiem64t -lippjem64t -lippccem64t -lippcvem64t -lpthread -lippsem64t
        IPPA:=em64t
    else
        IPP_BASE:=/usr/local/intel/ipp/5.1/ia32
    endif
    CFLAGS_IPP:=-I$(IPP_BASE)/include
    LDFLAGS_IPP:=-L$(IPP_BASE)/sharedlib -Wl,-R$(IPP_BASE)/sharedlib $(IPP_LIBS)
endif


############################################################
#
# Internal libraries
#
# List these in roughly the order of dependency; those with fewest
# dependencies first. Within each LDFLAGS, list the dependencies in
# decreasing order (e.g., end with LDFLAGS_GLIB)
#
############################################################

# handy makefile function for determining our internal library dependencies based upon LDFLAGS:
# LIBDEPS=$(call libdeps, $(LDFLAGS))
libdeps = $(filter $(wildcard $(LIB_PATH)/*.a), $(patsubst -l%, $(LIB_PATH)/lib%.a, $(sort $(filter -l%, $(1)))))

# common
CFLAGS_COMMON  := -I$(SRC_PATH) -DCONFIG_DIR='"$(CONFIG_DIR)"'
LDFLAGS_COMMON := -L$(LIB_PATH) -lcommon $(LDFLAGS_STD)

# mapping
CFLAGS_MAPPING  := -I$(SRC_PATH) $(CFLAGS_STD)
LDFLAGS_MAPPING := -L$(LIB_PATH) -lmapping $(LDFLAGS_STD)

# planning
CFLAGS_PLANNING  := -I$(SRC_PATH) $(CFLAGS_STD)
LDFLAGS_PLANNING := -L$(LIB_PATH) -lplanning $(LDFLAGS_STD)

# math
CFLAGS_MATH  := -I$(SRC_PATH) $(CFLAGS_COMMON)
LDFLAGS_MATH := -L$(LIB_PATH) -lmath $(LDFLAGS_COMMON)

# lcmtypes
CFLAGS_LCMTYPES  := -I$(SRC_PATH) -I$(SOLNS_PATH)/src $(CFLAGS_LCM)
LDFLAGS_LCMTYPES := -L$(LIB_PATH) -llcmtypes $(LDFLAGS_LCM)

# imagesource
CFLAGS_IMAGESOURCE  := $(CFLAGS_COMMON) $(CFLAGS_GTK) $(CFLAGS_USB) $(CFLAGS_PNG) $(CFLAGS_DC1394)
LDFLAGS_IMAGESOURCE := -L$(LIB_PATH) -limagesource $(LDFLAGS_COMMON) $(LDFLAGS_GTK) $(LDFLAGS_USB) $(LDFLAGS_PNG) $(LDFLAGS_DC1394)

# vx (no GUI)
CFLAGS_VX  := -I$(SRC_PATH) $(CFLAGS_IMAGESOURCE) $(CFLAGS_MATH) $(CFLAGS_COMMON)
LDFLAGS_VX := -L$(LIB_PATH) -lvx $(LDFLAGS_IMAGESOURCE) $(LDFLAGS_MATH) $(LDFLAGS_COMMON)

# vx gl
CFLAGS_VX_GL  := -I$(SRC_PATH) $(CFLAGS_MATH) $(CFLAGS_VX) $(CFLAGS_COMMON)
LDFLAGS_VX_GL := -L$(LIB_PATH) -lvxgl -lGL -lX11 $(LDFLAGS_VX) $(LDFLAGS_MATH) $(LDFLAGS_COMMON)

# vx GUI only
CFLAGS_VX_GTK  := -I$(SRC_PATH)
LDFLAGS_VX_GTK := -L$(LIB_PATH) -lvxgtk $(LDFLAGS_VX_GL) $(LDFLAGS_VX) -lz

############################################################
#
# Build rules
#
############################################################

%.o: %.c %.h
	@echo "    $@"
	@$(CC) $(CFLAGS) -c $<

%.o: %.c
	@echo "    $@"
	@$(CC) $(CFLAGS) -c $<

%.o: %.cpp
	@echo "    $@"
	@$(CXX) $(CXXFLAGS) -c $<


MAKEFLAGS += --no-print-directory
