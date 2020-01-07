include ../common.mk

CFLAGS =   $(CFLAGS_STD)  $(CFLAGS_COMMON)  $(CFLAGS_GTK)  $(CFLAGS_USB)  $(CFLAGS_PNG)  $(CFLAGS_DC1394)
LDFLAGS = $(LDFLAGS_STD) $(LDFLAGS_COMMON) $(LDFLAGS_GTK) $(LDFLAGS_USB) $(LDFLAGS_PNG) $(LDFLAGS_DC1394)
LIBDEPS = $(call libdeps, $(LDFLAGS))

LIB_IMAGESOURCE = $(LIB_PATH)/libimagesource.a
LIBIMAGESOURCE_OBJS = \
	image_convert.o \
	image_source.o \
	image_source_dc1394.o \
	image_source_filedir.o \
	image_source_islog.o \
	image_source_null.o \
	image_source_pgusb.o \
	image_source_tcp.o \
	image_source_v4l2.o \
	image_u32.o \
	image_u8.o \
	image_u8x3.o \
	image_util.o \
    pnm.o

BIN_ISVIEW = $(BIN_PATH)/isview
BIN_ISTEST = istest
BIN_TCPSTREAM = tcpstream

ALL = $(LIB_IMAGESOURCE) $(BIN_ISVIEW) $(BIN_ISTEST) $(BIN_TCPSTREAM)

all: $(ALL)

$(LIB_IMAGESOURCE): $(LIBIMAGESOURCE_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

$(BIN_ISVIEW): isview.o $(LIB_IMAGESOURCE) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_ISTEST): istest.o $(LIB_IMAGESOURCE) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_TCPSTREAM): tcp_image_streamer.o $(LIB_IMAGESOURCE) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f *~ *.o
	@rm -f $(ALL)
