LCM = lcm-gen
LCMTYPES_NAMES := $(shell ls *.lcm)

# Construct the pathnames to the C types
LCMTYPES_C := $(LCMTYPES_NAMES:%.lcm=%.c)
LCMTYPES_O = $(LCMTYPES_C:%.c=%.o)
ALL = $(LCMTYPES_C) $(LCMTYPES_O)


CC = gcc
CFLAGS = -g `pkg-config --cflags lcm`

all: $(ALL)

%.c: %.lcm
	$(LCM) -c --c-typeinfo $<
	$(LCM) -x $<
%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -rf build/
	rm -f *.c *.h *.o *.cpp *.hpp *~
