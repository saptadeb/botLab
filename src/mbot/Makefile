include ../common.mk

CFLAGS  =  $(CFLAGS_STD)  $(CFLAGS_COMMON)  $(CFLAGS_LCMTYPES) -O3
CXXFLAGS = $(CXXFLAGS_STD) $(CFLAGS_COMMON)  $(CFLAGS_LCMTYPES) -O3
LDFLAGS = $(LDFLAGS_STD) $(LDFLAGS_COMMON) $(LDFLAGS_LCMTYPES) $(LDFLAGS_RPLIDAR) 
LIBDEPS = $(call libdeps, $(LDFLAGS))

BIN_DRIVE_SQUARE = $(BIN_PATH)/drive_square
BIN_RPLIDAR_DRIVER = $(BIN_PATH)/rplidar_driver
BIN_MOTION_CONTROLLER = $(BIN_PATH)/motion_controller
BIN_TIMESYNC = $(BIN_PATH)/timesync
BIN_WIFI_READER = $(BIN_PATH)/wifi_reader
BIN_OLED_EXAMPLE = $(BIN_PATH)/oled_example

ALL = $(BIN_RPLIDAR_DRIVER) $(BIN_SWEEP_DRIVER) $(BIN_WIFI_READER) $(BIN_TIMESYNC) $(BIN_DRIVE_SQUARE) $(BIN_MOTION_CONTROLLER) $(BIN_MC_SMOOTH) $(BIN_OLED_EXAMPLE)

all: $(ALL)

laptop-only: $(BIN_TIMESYNC) $(BIN_DRIVE_SQUARE) $(BIN_MOTION_CONTROLLER)

$(BIN_DRIVE_SQUARE): drive_square.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(BIN_MOTION_CONTROLLER): motion_controller.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(BIN_RPLIDAR_DRIVER): rplidar_driver.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(BIN_TIMESYNC): timesync.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LDFLAGS)

$(BIN_WIFI_READER): wifi_reader.o $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_OLED_EXAMPLE): oled_example.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f *.o *~ *.a
	@rm -f $(ALL)
