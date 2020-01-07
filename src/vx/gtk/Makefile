include ../../common.mk

# flags for building the gtk library
CFLAGS  =   $(CFLAGS_STD) $(CFLAGS_VX_GL)  $(CFLAGS_VX)  $(CFLAGS_IMAGESOURCE)  $(CFLAGS_MATH)  $(CFLAGS_COMMON)  $(CFLAGS_GTK) -O2
LDFLAGS =  $(LDFLAGS_STD) $(LDFLAGS_VX_GL) $(LDFLAGS_VX) $(LDFLAGS_IMAGESOURCE) $(LDFLAGS_MATH) $(LDFLAGS_COMMON) $(LDFLAGS_GTK) $(LDFLAGS_LCM) -lz
LIBDEPS = $(call libdeps, $(LDFLAGS))

LIB_VXGTK = $(LIB_PATH)/libvxgtk.a
VXGTK_OBJS = priority_queue.o vx_gtk_display_source.o vx_gtk_display.o gtkuimagepane.o vx_gtk_buffer_manager.o

BIN_VX_DEMO = $(BIN_PATH)/vx_demo
BIN_VX_ZOO = $(BIN_PATH)/vx_zoo
BIN_VX_BLOCK_MAZE = $(BIN_PATH)/vx_block_maze
BIN_VX_REMOTE_VIEWER = $(BIN_PATH)/vx_remote_viewer
BIN_VX_SCENE_VIEWER = $(BIN_PATH)/vx_scene_viewer

BLOCK_LCMS = ../../lcmtypes/motion_capture_t.o ../../lcmtypes/pose_xyt_t.o ../../lcmtypes/velocity_cmd_t.o ../../lcmtypes/mbot_motor_command_t.o


ALL = $(LIB_VXGTK) $(BIN_VX_DEMO) $(BIN_VX_ZOO) $(BIN_VX_REMOTE_VIEWER) $(BIN_VX_SCENE_VIEWER)

all: $(ALL)

$(LIB_VXGTK): $(VXGTK_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

$(BIN_VX_DEMO): vx_demo.o $(LIB_VXGTK) $(LIB_VXGL) $(LIB_VX) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_VX_ZOO): vx_zoo.o $(LIB_VXGTK) $(LIB_VXGL) $(LIB_VX) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

#$(BIN_VX_BLOCK_MAZE): vx_block_maze.o $(BLOCK_LCMS) $(LIB_VXGTK) $(LIB_VXGL) $(LIB_VX) $(LIBDEPS) 
#	@echo "    $@"
#	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_VX_SCENE_VIEWER): vx_scene_viewer.o $(LIB_VXGTK) $(LIB_VXGL) $(LIB_VX) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

$(BIN_VX_REMOTE_VIEWER): vx_remote_viewer.o $(LIB_VXGTK) $(LIBDEPS)
	@echo "    $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

clean:
	@rm -f *.o *~ *.a
	@rm -rf  $(ALL)
