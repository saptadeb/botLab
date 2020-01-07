include ../common.mk

CFLAGS =   $(CFLAGS_STD)  $(CFLAGS_IMAGESOURCE)  $(CFLAGS_MATH)  $(CFLAGS_COMMON) \
	   `pkg-config --cflags freetype2` -pedantic -O2
LDFLAGS = $(LDFLAGS_STD) $(LDFLAGS_IMAGESOURCE) $(LDFLAGS_MATH) $(LDFLAGS_COMMON)
LIBDEPS = $(call libdeps, $(LDFLAGS))

LIB_VX = $(LIB_PATH)/libvx.a
LIBVX_DRAWABLE_OBJS = \
	vxo_arrow.o \
	vxo_axes.o \
	vxo_box.o \
	vxo_chain.o \
	vxo_circle.o \
	vxo_cylinder.o \
	vxo_depth_test.o \
	vxo_grid.o \
	vxo_image.o \
	vxo_lines.o \
	vxo_mat.o \
	vxo_mesh.o \
	vxo_objmtl.o \
	vxo_pix_coords.o \
	vxo_points.o \
	vxo_rect.o \
	vxo_robot.o \
	vxo_sphere.o \
	vxo_square_pyramid.o \
	vxo_star.o \
	vxo_tetrahedron.o \
	vxo_text.o \
	vxo_triangle.o
LIBVX_USER_OBJS = \
	default_camera_mgr.o \
	default_event_handler.o \
	vx_camera_pos.o \
	vx_console.o \
	vx_layer.o \
	vx_matrix_stack.o \
	vx_program.o \
	vx_ray3.o \
	vx_util.o \
	vx_viewport_mgr.o \
	vx_world.o \
	vxp.o
LIBVX_CORE_OBJS = \
	vx_code_input_stream.o \
	vx_code_output_stream.o \
	vx_global.o \
	vx_remote_display_source.o \
	vx_resc.o \
	vx_resc_manager.o \
	vx_tcp_display.o \
	vx_tcp_util.o
LIBVX_OBJS = $(LIBVX_CORE_OBJS) $(LIBVX_USER_OBJS) $(LIBVX_DRAWABLE_OBJS)


# The only objects with direct GL references are:
LIB_VXGL = $(LIB_PATH)/libvxgl.a
LIBVXGL_OBJS = vx_gl_renderer.o glcontext.o glcontext-x11.o

ALL = $(LIB_VX) $(LIB_VXGL) vx_make_font

#############################################################
#gcc -shared -Wl,-soname,$(LIB) -o $(LIB) $(LIBVX_OBJS)

all: $(ALL)

$(LIB_VX): $(LIBVX_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

$(LIB_VXGL): $(LIBVXGL_OBJS) $(LIBDEPS)
	@echo "    $@"
	@ar rc $@ $^

vx_make_font: vx_make_font.o
	@echo "    $@"
	@$(CC) $(CFLAGS) -o vx_make_font $< $(LDFLAGS_IMAGESOURCE) $(LDFLAGS_COMMON) $(LDFLAGS_STD)

clean:
	@rm -f *~ *.o *.d
	@rm -f vx_make_font $(ALL)
