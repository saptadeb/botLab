include ../../common.mk

CFLAGS = $(CFLAGS_STD) $(CFLAGS_COMMON) $(CFLAGS_GSL) -O4 -DNDEBUG
LDFLAGS = $(LDFLAGS_STD) $(LDFLAGS_COMMON) $(LDFLAGS_GSL)
LIBDEPS = $(call libdeps, $(LDFLAGS))

LIB_MATH = $(LIB_PATH)/libmath.a
LIBMATH_OBJS = \
	fasttrig.o \
	matd.o \
	math_util.o \
	svd22.o

ALL = $(LIB_MATH)

all: $(ALL)

$(LIB_MATH): $(LIBMATH_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

clean:
	@rm -f *.o *~ *.a
	@rm -f $(ALL)
