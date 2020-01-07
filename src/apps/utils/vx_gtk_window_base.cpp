#include <apps/utils/vx_gtk_window_base.hpp>
#include <apps/utils/vx_utils.hpp>
#include <vx/gtk/vx_gtk_display_source.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <unistd.h>


int handle_touch_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_camera_pos_t* pos, vx_touch_event_t* mouse);
int handle_mouse_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_camera_pos_t* pos, vx_mouse_event_t* mouse);
int handle_key_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_key_event_t* key);
void gui_display_started(vx_application_t *app, vx_display_t *disp);
void gui_display_finished(vx_application_t *app, vx_display_t *disp);
void destroy_event_handler(vx_event_handler_t* vh);


VxGtkWindowBase::VxGtkWindowBase(int argc, char** argv, int widthInPixels, int heightInPixels, int framesPerSecond)
: world_(0)
, width_(widthInPixels)
, height_(heightInPixels)
, framesPerSecond_(framesPerSecond)
, isInitialized_(false)
, isRunning_(false)
{
    assert(argc > 0);
    assert(argv);
    assert(widthInPixels > 0);
    assert(heightInPixels > 0);
    assert(framesPerSecond > 0);
    
    app_init(argc, argv);
    
    world_ = vx_world_create();
    eventHandler_.key_event      = handle_key_event;
    eventHandler_.mouse_event    = handle_mouse_event;
    eventHandler_.touch_event    = handle_touch_event;
    eventHandler_.destroy        = destroy_event_handler;
    eventHandler_.dispatch_order = 100;
    eventHandler_.impl           = this;
    
    application_.display_started = gui_display_started;
    application_.display_finished = gui_display_finished;
    application_.impl = this;
}


VxGtkWindowBase::~VxGtkWindowBase(void)
{
}


void VxGtkWindowBase::run(void)
{
    vx_gtk_display_source_t* displaySource = vx_gtk_display_source_create(&application_);
    GtkWidget* window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget* canvas = vx_gtk_display_source_get_widget(displaySource);
    
    gtk_window_set_default_size(GTK_WINDOW (window), width_, height_);
    
    createGuiLayout(window, canvas);
    
    g_signal_connect_swapped(G_OBJECT (window), "destroy", G_CALLBACK (gtk_main_quit), NULL);
    
    isRunning_ = true;
    
    pthread_t renderThread;
    pthread_create(&renderThread, NULL, render_loop, this);
    
    gtk_main(); // Blocks as long as GTK window is open
    gdk_threads_leave();
    
    isRunning_ = false;
    
    pthread_join(renderThread, NULL);
}


int VxGtkWindowBase::onMouseEvent(vx_layer_t* layer, 
                                  vx_camera_pos_t* cameraPosition, 
                                  vx_mouse_event_t* event,
                                  Point<float> worldPoint)
{
    // By default, don't consume mouse events
    return 0;
}


int VxGtkWindowBase::onKeyEvent(vx_layer_t* layer, vx_key_event_t* event)
{
    // By default, don't consume keyboard events
    return 0;
}


void VxGtkWindowBase::onDisplayStart(vx_display_t* display)
{
    layer_ = vx_layer_create(world_);
    vx_layer_set_background_color(layer_, vx_white);
    vx_layer_set_display(layer_, display);
    vx_layer_add_event_handler(layer_, &eventHandler_);
    
    vx_layer_camera_op(layer_, OP_PROJ_PERSPECTIVE);
    float eye[3]    = {  0,  0,  1 };
    float lookat[3] = {  0,  0,  0 };
    float up[3]     = {  0,  1,  0 };
    vx_layer_camera_lookat(layer_, eye, lookat, up, 1);

    vx_code_output_stream_t *couts = vx_code_output_stream_create (128);
    couts->write_uint32 (couts, OP_LAYER_CAMERA);
    couts->write_uint32 (couts, vx_layer_id (layer_));
    couts->write_uint32 (couts, OP_INTERFACE_MODE);
    couts->write_float  (couts, 2.5f);
    display->send_codes(display, couts->data, couts->pos);
    vx_code_output_stream_destroy (couts);
}


void VxGtkWindowBase::onDisplayFinish(vx_display_t* display)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    vx_layer_destroy(layer_);
}


void VxGtkWindowBase::initialize(vx_display_t* display)
{
    onDisplayStart(display);
    isInitialized_ = true;
}


void VxGtkWindowBase::createGuiLayout(GtkWidget* window, GtkWidget* vxCanvas)
{
    GtkWidget* vbox = gtk_vbox_new (0, 0);
    gtk_box_pack_start(GTK_BOX (vbox), vxCanvas, 1, 1, 0);
    gtk_widget_show(vxCanvas);    // XXX Show all causes errors!
    
    gtk_container_add(GTK_CONTAINER (window), vbox);
    gtk_widget_show(window);
    gtk_widget_show(vbox);
}


int handle_touch_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_camera_pos_t* pos, vx_touch_event_t* mouse)
{
    // Ignore all touch events
    return 0;
}


int handle_mouse_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_camera_pos_t* pos, vx_mouse_event_t* mouse)
{
    VxGtkWindowBase* window = static_cast<VxGtkWindowBase*>(vh->impl);
    
    // Find the world coordinate to pass along in the mouse event 
    vx_ray3_t ray;
    vx_camera_pos_compute_ray (pos, mouse->x, mouse->y, &ray);
    
    double ground[3];
    vx_ray3_intersect_xy (&ray, 0, ground);
    Point<float> worldPoint;
    worldPoint.x = ground[0];
    worldPoint.y = ground[1];
    
    return window->onMouseEvent(vl, pos, mouse, worldPoint);
}


int handle_key_event(vx_event_handler_t* vh, vx_layer_t* vl, vx_key_event_t* key)
{
    VxGtkWindowBase* window = static_cast<VxGtkWindowBase*>(vh->impl);
    return window->onKeyEvent(vl, key);
}


void gui_display_started(vx_application_t *app, vx_display_t *disp)
{
    VxGtkWindowBase* gui = static_cast<VxGtkWindowBase*>(app->impl);
    gui->initialize(disp);
}


void gui_display_finished(vx_application_t *app, vx_display_t *disp)
{
    VxGtkWindowBase* gui = static_cast<VxGtkWindowBase*>(app->impl);
    gui->onDisplayFinish(disp);
}


void destroy_event_handler(vx_event_handler_t* vh)
{
    // No specific state needs to be destroy
}


void* render_loop(void* data)
{
    VxGtkWindowBase* window = static_cast<VxGtkWindowBase*>(data);
    
    // Continue running until we are signaled otherwise. This happens
    // when the window is closed/Ctrl+C is received.
    
    while(window->isRunning_)
    {
        if(window->isInitialized_)
        {
            window->render();
            usleep(1000000 / window->framesPerSecond_);
        }
    }
    
    return 0;
}
