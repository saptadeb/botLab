include ../common.mk

CXXFLAGS = $(CXXFLAGS_STD) -O3 -DNDEBUG
LDFLAGS  = $(LDFLAGS_MAPPING) $(LDFLAGS_COMMON) $(LDFLAGS_STD) $(LDFLAGS_LCMTYPES) $(LDFLAGS_LCM)
LIBDEPS  = $(call libdeps, $(LDFLAGS))

LIB_PLANNING = $(LIB_PATH)/libplanning.a
LIBPLANNING_OBJS = \
	motion_planner.o \
	obstacle_distance_grid.o \
	astar.o \
	frontiers.o

$(LIB_PLANNING): $(LIBPLANNING_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

BIN_DIST_TEST  = $(BIN_PATH)/obstacle_distance_grid_test
BIN_ASTAR_TEST = $(BIN_PATH)/astar_test
BIN_GRID_GENERATOR = $(BIN_PATH)/grid_generator
BIN_EXPLORATION = $(BIN_PATH)/exploration

ALL = $(BIN_DIST_TEST) $(BIN_ASTAR_TEST) $(BIN_GRID_GENERATOR) $(BIN_EXPLORATION) $(LIB_PLANNING)

all: $(ALL)

$(BIN_DIST_TEST): obstacle_distance_grid_test.o $(LIBDEPS) $(LIB_PLANNING)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LIB_PLANNING) $(LDFLAGS) $(CXXFLAGS)

$(BIN_ASTAR_TEST): astar_test.o $(LIBDEPS) $(LIB_PLANNING)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LIB_PLANNING) $(CXXFLAGS) $(LDFLAGS)

$(BIN_ASTAR_TEST_FILES): astar_test_files.o $(LIBDEPS) $(LIB_PLANNING)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LIB_PLANNING) $(CXXFLAGS) $(LDFLAGS)
	
$(BIN_GRID_GENERATOR): grid_generator.o $(LIBDEPS)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

$(BIN_EXPLORATION): exploration.o exploration_main.o $(LIBDEPS) $(LIB_PLANNING)
	@echo "    $@"
	@$(CXX) -o $@ $^ $(LIB_PLANNING) $(CXXFLAGS) $(LDFLAGS)
	
clean:
	@rm -f *.o *~ *.a
	@rm -f $(ALL)
