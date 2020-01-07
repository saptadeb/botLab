include ../common.mk

CXXFLAGS = $(CXXFLAGS_STD) \
	$(CFLAGS_LCMTYPES) \
	$(CFLAGS_LCM) \
	$(CFLAGS_COMMON) \
	-O3
	
CFLAGS = $(CFLAGS_STD) \
	$(CFLAGS_LCMTYPES) \
	$(CFLAGS_LCM) \
	-O3

LDFLAGS = $(LDFLAGS_STD) \
      $(LDFLAGS_LCMTYPES) \
      $(LDFLAGS_LCM) \
      $(LDFLAGS_COMMON)

BIN_OPTITRACK_DRIVER=$(BIN_PATH)/optitrack_driver

all:  $(BIN_OPTITRACK_DRIVER)

### Specific programs to build

$(BIN_OPTITRACK_DRIVER): optitrack.o optitrack_driver.o
	@echo "    $@"
	@$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

# Clean target
clean:
	@rm -f *~ *.o $(BIN_OPTITRACK_DRIVER) *.a
