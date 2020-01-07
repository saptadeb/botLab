include ../common.mk

CFLAGS  =  $(CFLAGS_STD)  $(CFLAGS_LCM)
LDFLAGS = $(LDFLAGS_STD) $(LDFLAGS_LCM)
LIBDEPS = $(call libdeps, $(LDFLAGS))

# Construct the pathnames to objs
LCMTYPES_NAMES := $(shell cd ../../lcmtypes && ls *.lcm)
LCMTYPES_OBJS  := $(LCMTYPES_NAMES:%.lcm=%.o)

LIB_LCMTYPES = $(LIB_PATH)/liblcmtypes.a
LIB_LCMTYPES_SO = $(LIB_PATH)/liblcmtypes.so

ALL = $(LIB_LCMTYPES)

all: $(ALL)

$(LIB_LCMTYPES): $(LCMTYPES_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $(LCMTYPES_OBJS)

$(LIB_LCMTYPES_SO): $(LCMTYPES_OBJS) $(LIBDEPS)
	@echo "    $@"
	@gcc -shared -o $@ $(LCMTYPES_OBJS)

clean:
	@rm -f *~ *.o *.a *.h *.c *.hpp *.py
	@rm -f $(ALL)
