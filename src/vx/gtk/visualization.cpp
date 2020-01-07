#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "vx/vxo_drawables.h"

#include "vx/gtk/vx_gtk_display_source.h"

// core api
#include "vx/vx_global.h"
#include "vx/vx_layer.h"
#include "vx/vx_world.h"
#include "vx/vx_colors.h"


#include <lcm/lcm.h>
#include "../../lcmtypes/odo_pose_xyt_t.h"
#include "../../lcmtypes/motion_capture_t.h"
#include "../../lcmtypes/velocity_cmd_t.h"
#include "../../lcmtypes/mbot_motor_command_t.h"

#include "priority_queue.h"  // defines node_t and zarray heap wrapper functions 
 
#define PROJ_HEIGHT 110
#define PROJ_LENGTH 146
#define PROJ_SCALE .01

#define BLOCK_SCALE .1

#define MAX_NUM_PTS 1
#define PATH_SIZE 10

#define BOT_RADIUS 8
#define WALL_RADIUS 8
#define GRID_MULT 3.0

typedef struct
{
    vx_object_t * obj;
    char * name;
} obj_data_t;


typedef struct
{
    zarray_t * obj_data;
    vx_world_t * world;

    vx_event_handler_t  vxeh; // for getting mouse, key, and touch events
    vx_mouse_event_t    last_mouse_event;

    vx_application_t vxapp;
    lcm_t* lcm;

    //data to place dots at mouse click locations
    //vx_object_t* points[MAX_NUM_PTS];
    int next_point;
    int num_points;

    double mot_coords[3];

    //data for arrows to be placed along mbots path
    vx_object_t* path_points[PATH_SIZE];
    int next_path;
    int num_path;

    pthread_t animate_thread;
    pthread_t path_thread;

    int map[PROJ_HEIGHT][PROJ_LENGTH];
    int origin;
    //use grid as map D1 = 10mm D2 = 10*sqrt(2)mm
    
} state_t;


/*
static void OdoHandler(const lcm_recv_buf_t* rbuf, const char* channel,
                        const odo_pose_xyt_t* msg, void* _user)
{
    state_t* state = (state_t*) _user;

    vx_object_t * rob = vxo_chain(vxo_mat_translate2(-msg->xyt[1]/100,msg->xyt[0]/100), 
                                  vxo_mat_rotate_z(msg->xyt[2]+(M_PI/2)),
                                  vxo_robot(vxo_mesh_style(vx_green),vxo_lines_style(vx_white,2.0f)));
    
    vx_buffer_add_back(vx_world_get_buffer(state->world,"robot"),rob);
    vx_buffer_swap(vx_world_get_buffer(state->world,"robot"));
}
*/


int zarray_query(zarray_t* za, node_t* node)
{
    node_t* test = malloc(sizeof(node_t));
    for(int i = 0; i < zarray_size(za); ++i){
        zarray_get(za, i, test);
        if((test->x == node->x) && (test->y == node->y)) {
            free(test);
            return 1;
        }
    }
    free(test);
    return 0;
}

double euclid_dist(node_t* start, node_t* end)
{
    double dx = fabs(start->x - end->x);
    double dy = fabs(start->y - end->y);

    return sqrt(dx*dx + dy*dy);
}

double diag_dist(node_t* start, node_t* end){
    double dx = fabs(start->x - end->x);
    double dy = fabs(start->y - end->y);
    double D1 = 1;
    double D2 = sqrt(2);

    return D1*(dx+dy) + (D2- 2*D1)*fmin(dx,dy);
}


//get_neighbors, get_path, and a_star are work for A* path planning

zarray_t* get_neighbors(state_t* state, int x, int y)
{
    zarray_t* za = zarray_create(sizeof(node_t));

    node_t node;
    
    node.x = x+1;
    for(int i = 1; i >= -1; i = i - 1){
        node.y = y + i;
        if(!state->map[x+2][y+i + state->origin]) {
            zarray_add(za, &node);
            //printf("no wall at (%d,%d)\n", x+1, state->origin + y + i);
        }
    }
    node.x = x;
    for(int i = 1; i >= -1; i = i - 1){
        node.y = y + i;
        if(!state->map[x+1][y+i + state->origin]) zarray_add(za, &node);
    }

    node.x = x-1;
    for(int i = 1; i >= -1; --i){
        node.y = y + i;
        if(!state->map[x+1][y+i + state->origin]) zarray_add(za, &node);
    }

    return za;
}

zarray_t* get_path(state_t* state, node_t* current)
{
    zarray_t* path = zarray_create(sizeof(node_t));
    zarray_insert(path,0, current);
    
    vx_buffer_t* vb = vx_world_get_buffer(state->world, "visited_nodes");

    while(current->parent != NULL){
        vx_object_t* vo = vxo_chain(vxo_mat_translate2(current->y*BLOCK_SCALE,current->x*BLOCK_SCALE),
                                        vxo_mat_scale(BLOCK_SCALE),
                                        vxo_box(vxo_mesh_style(vx_yellow)));
        
        vx_buffer_add_back(vb, vo);
        zarray_insert(path,0, current);
        // printf("current(%d,%d)\t added by (%d,%d)\n", current->x,current->y,
        //                                               current->parent->x, current->parent->y);
        current = current->parent; 
        
    }
    vx_buffer_swap(vb);

    return path;
}

void follow_path(state_t* state, zarray_t* path)
{
    node_t* node = malloc(sizeof(node_t));
    velocity_cmd_t cmd;
    if(path != NULL){        
        for(int i = 0; i < zarray_size(path); ++i){
            zarray_get(path, i, node);
            cmd.goal_x = node->x *10;
            cmd.goal_y = node->y * -10;
            velocity_cmd_t_publish(state->lcm,"GS_VELOCITY_CMD",&cmd);
        }    
    }
    free(node);
}

//returns a zarray with all the visited nodes, the end pos should be at the end
zarray_t* a_star(state_t* state, node_t goal)
{
    priority_queue_t* frontier = heap_create();
    zarray_t* visited = zarray_create(sizeof(node_t));

    int x = state->mot_coords[0]*10;
    int y = state->mot_coords[1]*10;
    
    
    node_t start;
    start.x = x;
    start.y = y;
    start.cost = 0;
    start.parent = NULL;


    //printf("a* start pos(x,y)[cm] = (%d,%d)\n", x,y);
    //printf("a* end goal(x,y) = (%d,%d)\n", goal.x, goal.y);

    heap_push(frontier, &start);

    node_t current;
    node_t* neighbor = malloc(sizeof(node_t));
    node_t* node = malloc(sizeof(node_t));

    while(!heap_empty(frontier)){
        current = heap_top(frontier);

        heap_pop(frontier);
        zarray_add(visited, &current);

        
        vx_buffer_t* vb = vx_world_get_buffer(state->world, "visited_nodes");

        for(int i = 0; i < zarray_size(visited); ++i){
            zarray_get(visited, i, node);
            
            vx_object_t* vo = vxo_chain(vxo_mat_translate2(node->y*BLOCK_SCALE,node->x*BLOCK_SCALE),
                                        vxo_mat_scale(BLOCK_SCALE),
                                        vxo_box(vxo_mesh_style(vx_green)));
            vx_buffer_add_back(vb, vo);
        }
        
        vx_buffer_swap(vb);
        

        if(current.x == goal.x && current.y == goal.y){
            free(neighbor);
            free(node);
            return get_path(state, &current);
        }
        
        zarray_t* neighbors = get_neighbors(state, current.x, current.y);

        for(int i = 0; i < zarray_size(neighbors); ++i){
        
            zarray_get(neighbors, i , neighbor);
            
            double cost = current.cost + euclid_dist(&current, neighbor);

            int node_pos = heap_query(frontier, neighbor);

            if(node_pos >= 0 && cost < frontier->heap[node_pos].cost){
                //printf("new cost = %f, old cost = %f\n",cost, neighbor->cost);
                heap_remove_elt(frontier, node_pos);
                node_pos = -1;
            }

            if((node_pos == -1 && !zarray_query(visited, neighbor))) {
                neighbor->cost = cost;
                neighbor->rank = cost + euclid_dist(neighbor, &goal);
                neighbor->parent = malloc(sizeof(node_t));
                *(neighbor->parent) = current;
                
                heap_push(frontier, neighbor);
            }
           
        }
    }
    printf("No path to that point is possible\n");
    return NULL;
}

static void MotHandler(const lcm_recv_buf_t* rbuf, const char* channel,
                        const motion_capture_t* msg, void* _user)
{
    state_t* state = (state_t*) _user;

    double theta = msg->theta;

    vx_object_t * rob = vxo_chain(vxo_mat_translate2(-msg->y/100,msg->x/100),
                                  vxo_mat_rotate_z(theta + M_PI/2),
                                  vxo_mat_scale(.75),
                                  //vxo_robot(vxo_mesh_style(vx_green),vxo_lines_style(vx_white,2.0f)));
                                  vxo_sphere(vxo_mesh_style(vx_white)));

    state->mot_coords[0] = msg->x/100;
    state->mot_coords[1] = -msg->y/100;


    state->mot_coords[2] = -theta - M_PI/2;
    vx_buffer_add_back(vx_world_get_buffer(state->world,"robot"),rob);
    vx_buffer_swap(vx_world_get_buffer(state->world,"robot"));
}


static void draw(state_t* state, vx_world_t * world, zarray_t * obj_data);
static void display_finished(vx_application_t * app, vx_display_t * disp);
static void display_started(vx_application_t * app, vx_display_t * disp);
void random_maze_generate(state_t* state);

//Macro to add vxo objects
#define ADD_OBJECT(s, call)                                          \
    {                                                               \
        obj_data_t data = {.obj = s call, .name = #s};   \
        zarray_add(state->obj_data, &data);                          \
    }                                                               \


void * render_loop(void * data)
{
    state_t* state = data;
    while(1){
        lcm_handle(state->lcm);
    }
}

void* path_loop(void* data)
{
    state_t* state = data;
    
    while(1){
        vx_buffer_t* vb =  vx_world_get_buffer(state->world,"path");
        if(state->num_path%PATH_SIZE == 0){
            state->next_path = 0;
        }
        
        ++state->num_path;
             
        vx_object_t* vo = vxo_chain(vxo_mat_translate2(state->mot_coords[1],state->mot_coords[0]),
                                    vxo_mat_rotate_z(state->mot_coords[2]),
                                    vxo_mat_scale(.5),
                                    vxo_arrow(vxo_mesh_style(vx_red)));
        state->path_points[state->next_path++] = vo;

        for(int i = 0; i < fmin(state->num_path,PATH_SIZE); ++i){
            vx_buffer_add_back(vb,state->path_points[i]);
        }

        //printf("next path = %d, path_points = %d\n", state->next_path, state->num_path);

        vx_buffer_swap(vb);
        usleep(2000000);

    }
}


static int mouse_event (vx_event_handler_t *vxeh, vx_layer_t *vl, vx_camera_pos_t *pos, vx_mouse_event_t *mouse)
{
    state_t *state = vxeh->impl;

    //velocity_cmd_t cmd;
    vx_object_t* pt= NULL;

    if ((mouse->button_mask & VX_BUTTON1_MASK) &&
        !(state->last_mouse_event.button_mask & VX_BUTTON1_MASK)) {

        vx_ray3_t ray;
        vx_camera_pos_compute_ray (pos, mouse->x, mouse->y, &ray);

        double ground[3];
        vx_ray3_intersect_xy (&ray, 0, ground);

        //printf ("Mouse clicked at coords: [%8.3f, %8.3f]  Ground clicked at coords: [%6.3f, %6.3f]\n",
                //mouse->x, mouse->y, ground[0], ground[1]);

        printf("point placed at coords[%6.3f, %6.3f]\n", ground[0], ground[1]);

        pt = vxo_chain(vxo_mat_translate2(ground[0],ground[1]),
                       vxo_mat_scale(0.1),
                       vxo_sphere(vxo_mesh_style(vx_red)));

        if(state->num_points % MAX_NUM_PTS == 0){
            state->next_point = 0;
        }
        
        ++state->num_points;
        state->points[state->next_point++] = pt;

        // vx_buffer_t* buff = vx_world_get_buffer(state->world, "pt");
        // vx_buffer_add_back(buff, pt);
        // vx_buffer_swap(buff);

        int x = 10*ground[1];
        
        int y = 10*ground[0];
        node_t goal;
        goal.x = x;
        goal.y = y;

        //cmd.goal_x = (double)x;
        //cmd.goal_y = (double)y;

       
        vx_buffer_t * vb = vx_world_get_buffer(state->world, "waypoints");

        for(int i = 0; i < fmin(state->num_points,MAX_NUM_PTS); ++i){
            vx_buffer_add_back(vb,state->points[i]);
        }
        vx_buffer_swap(vb);


    
        zarray_t* a_path = a_star(state, goal);
        node_t* node = malloc(sizeof(node_t));

        velocity_cmd_t cmd;
        if(a_path != NULL){

            for(int i = 0; i < zarray_size(a_path); ++i){
                zarray_get(a_path, i, node);
                cmd.goal_x = node->x *10;
                cmd.goal_y = node->y * -10;
                velocity_cmd_t_publish(state->lcm,"GS_VELOCITY_CMD",&cmd);
            }    
        }
        free(node);
        
        
        
        //velocity_cmd_t_publish(state->lcm,"GS_VELOCITY_CMD",&cmd);
    }

    if(pt != NULL){
        vx_buffer_t * vb = vx_world_get_buffer(state->world, "waypoints");

        for(int i = 0; i < fmin(state->num_points,MAX_NUM_PTS); ++i){
            vx_buffer_add_back(vb,state->points[i]);
        }
        vx_buffer_swap(vb);
    }
    
    // store previous mouse event to see if the user *just* clicked or released
    state->last_mouse_event = *mouse;

    return 0;
}

static int key_event (vx_event_handler_t* vxeh, vx_layer_t* vl, vx_key_event_t* key)
{
    state_t* state = vxeh->impl;

    mbot_motor_command_t cmd;
    
    if(!key->released){
        if(key->key_code == 'w' || key->key_code == 'W' || key->key_code == VX_KEY_UP) {
            cmd.left_motor_speed = .35;
            cmd.right_motor_speed = .35;
        }
        else if(key->key_code == 'a' || key->key_code == 'A' || key->key_code == VX_KEY_LEFT){
            cmd.left_motor_speed = -.3;
            cmd.right_motor_speed = .3;
        }
        else if(key->key_code == 's' || key->key_code == 'S' || key->key_code == VX_KEY_DOWN){
            cmd.left_motor_speed = -.35;
            cmd.right_motor_speed = -.35;
        }
        else if(key->key_code == 'd' || key->key_code == 'D' || key->key_code == VX_KEY_RIGHT){
            cmd.left_motor_speed = .3;
            cmd.right_motor_speed = -.3;
        }
        cmd.left_motor_enabled = 1;
        cmd.right_motor_enabled = 1;

        mbot_motor_command_t_publish(state->lcm, "MBOT_MOTOR_COMMAND", &cmd);
    }
    
    return 0;
}


int main(int argc, char **argv)
{
	vx_global_init();

	state_t* state = calloc(1,sizeof(state_t));
	state->world = vx_world_create();
	state->obj_data = zarray_create(sizeof(obj_data_t));
    state->vxapp.display_finished = display_finished;
    state->vxapp.display_started = display_started;
    state->vxapp.impl = state;
    state->vxeh.mouse_event = mouse_event;
    state->vxeh.key_event = key_event;
    state->vxeh.impl = state;

    state->next_point = 0;
    state->num_points = 0;

    state->origin = 146/2;

    for(int i = 0; i < 3; ++i){
        state->mot_coords[i] = 0;
    }
    
    vx_buffer_t* vb = vx_world_get_buffer(state->world, "compass");
    vx_object_t* vo = vxo_chain(vxo_mat_translate2(0,0),
                                    vxo_mat_rotate_z(0),
                                    vxo_mat_scale(.5),
                                    vxo_arrow(vxo_mesh_style(vx_red)));
    vx_buffer_add_back(vb, vo);
    vo = vxo_chain(vxo_mat_translate2(0,0),
                                    vxo_mat_rotate_z(M_PI/2),
                                    vxo_mat_scale(.5),
                                    vxo_arrow(vxo_mesh_style(vx_green)));
    vx_buffer_add_back(vb, vo);

    vo = vxo_chain(vxo_mat_translate2(0,0),
                                    vxo_mat_rotate_z(3*M_PI/2),
                                    vxo_mat_scale(.5),
                                    vxo_arrow(vxo_mesh_style(vx_blue)));
    vx_buffer_add_back(vb, vo);
    vx_buffer_swap(vb);

    state->lcm = lcm_create(NULL);
    
    motion_capture_t_subscribe(state->lcm, "MOTION_CAPT_POS",&MotHandler,state);
    
    ADD_OBJECT(vxo_box,(vxo_mesh_style(vx_blue)));
    ADD_OBJECT(vxo_box,(vxo_mesh_style(vx_red)));
    ADD_OBJECT(vxo_sphere,(vxo_mesh_style(vx_white)));    
    
   
    pthread_create(&state->animate_thread, NULL, render_loop, state);
    pthread_create(&state->path_thread, NULL, path_loop, state);
        
    gdk_threads_init ();
    gdk_threads_enter ();

    gtk_init (&argc, &argv);

    vx_gtk_display_source_t * appwrap = vx_gtk_display_source_create(&state->vxapp);
    GtkWidget * window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget * canvas = vx_gtk_display_source_get_widget(appwrap);
    gtk_window_set_default_size (GTK_WINDOW (window), 1024, 768);
    gtk_container_add(GTK_CONTAINER(window), canvas);
    gtk_widget_show (window);
    gtk_widget_show (canvas); // XXX Show all causes errors!

    g_signal_connect_swapped(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_main (); // Blocks as long as GTK window is open
    gdk_threads_leave ();

 
    vx_gtk_display_source_destroy(appwrap);
 
    vx_global_destroy();
}

void build_wall(state_t* state, int x, int y, int max_x, int max_y){

    for(int i = WALL_RADIUS; i >= -WALL_RADIUS; --i){
        for(int j = WALL_RADIUS; j >= -WALL_RADIUS; --j){
            if((x+i) >= 0 && (y+j) >= 0 && x+i < max_x && y+j < max_y) state->map[x+i][y+j] = 1;
        }
    }
}

static void draw(state_t* state, vx_world_t * world, zarray_t * obj_data)
{
    vx_buffer_t * vb = vx_world_get_buffer(world, "block_maze");

    
    for(int i = 0; i < 110; ++i){
        for(int j = 0; j < 146; ++j){
            //maze.blocks[i][j] = 0;
            state->map[i][j] = 0;
        }
    }

    obj_data_t data;

    zarray_get(obj_data,0,&data);
    vx_object_t* vo = data.obj;
    zarray_get(obj_data,1,&data);
    vx_object_t* vo_red = data.obj;
/*
    for(int i = 0; i < 20; ++i){
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(-10*BLOCK_SCALE,i*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(10*BLOCK_SCALE,i*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo));
        //state->map[i][state->origin-10] = 1;
        //state->map[i][state->origin+10] = 1;
        build_wall(state, i, state->origin - 10);
        build_wall(state, i, state->origin + 10);


        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((i+10)*BLOCK_SCALE,20*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo_red));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((-i-10)*BLOCK_SCALE,20*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo_red));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((-i-30)*BLOCK_SCALE,20*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo));
        //state->map[20][state->origin+i+10] = 1;
        //state->map[20][state->origin-i-10] = 1;
        //state->map[20][state->origin-i-30] = 1;
        build_wall(state, 20, state->origin+i+10);
        build_wall(state, 20, state->origin-i-10);
        build_wall(state, 20, state->origin-i-30);

        

        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((i+10)*BLOCK_SCALE,40*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((10-i)*BLOCK_SCALE,40*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo_red));
        //state->map[40][state->origin+i+10] = 1;
        //state->map[40][state->origin+10-i] = 1;
        build_wall(state, 40, state->origin+i+10);
        build_wall(state, 40, state->origin+10-i);
        
        
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(29*BLOCK_SCALE, (i+20)*BLOCK_SCALE), vxo_mat_scale(BLOCK_SCALE), vo));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(-30*BLOCK_SCALE, (i+40)*BLOCK_SCALE), vxo_mat_scale(BLOCK_SCALE), vo));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((-i-10)*BLOCK_SCALE, 40*BLOCK_SCALE), vxo_mat_scale(BLOCK_SCALE), vo));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(-50*BLOCK_SCALE,(i+20)*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo_red));
        vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2(-50*BLOCK_SCALE,(i+40)*BLOCK_SCALE),vxo_mat_scale(BLOCK_SCALE),vo));
        //state->map[i+20][state->origin+29] = 1;
        //state->map[i+40][state->origin-30] = 1;
        //state->map[40][state->origin-i-10] = 1;
        //state->map[i+20][state->origin-50] = 1;
        //state->map[i+40][state->origin-50] = 1;
        build_wall(state, i+20, state->origin+29);
        build_wall(state, i+40, state->origin-30);
        build_wall(state, 40, state->origin-i-10);
        build_wall(state, i+20, state->origin-50);
        build_wall(state, i+40, state->origin-50);
        

    }
*/
  
    zarray_get(obj_data,2,&data);
    vx_object_t* vo_sphere = data.obj;
    
    
    vx_buffer_add_back(vb,vxo_chain(vxo_mat_translate2(0,0),vxo_mat_scale(0.1),vo_sphere)); //point at origin
    
    vx_buffer_add_back(vb,vxo_chain(vxo_mat_translate2(PROJ_LENGTH*5.0*PROJ_SCALE,0.0),vxo_mat_scale(0.1),vo_sphere));
    vx_buffer_add_back(vb,vxo_chain(vxo_mat_translate2(PROJ_LENGTH*5.0*PROJ_SCALE,PROJ_HEIGHT*10.0*PROJ_SCALE),vxo_mat_scale(0.1),vo_sphere));
    vx_buffer_add_back(vb,vxo_chain(vxo_mat_translate2(-PROJ_LENGTH*5.0*PROJ_SCALE,PROJ_HEIGHT*10.0*PROJ_SCALE),vxo_mat_scale(0.1),vo_sphere));
    vx_buffer_add_back(vb,vxo_chain(vxo_mat_translate2(-PROJ_LENGTH*5.0*PROJ_SCALE,0.0),vxo_mat_scale(0.1),vo_sphere));
 
    printf("PL*PS = %d\n", PROJ_LENGTH*PROJ_SCALE);
    for(int i = 0; i < 110; ++i){
        state->map[i][0] = 1;
        state->map[i][145] = 1;
    }

    for(int i = 0; i < 146; ++i){
        state->map[0][i] = 1;
        state->map[109][i] = 1;
    }
    

    //maze_blocks_t_publish(state->lcm, "MAZE",&maze);

    vx_buffer_swap(vb);

}

static void display_finished(vx_application_t * app, vx_display_t * disp)
{
    
    state_t* state = app->impl;
    pthread_cancel(state->animate_thread);
    pthread_cancel(state->path_thread);
    free(state);
}

static void display_started(vx_application_t * app, vx_display_t * disp)
{
    state_t* state = app->impl;

    vx_layer_t* layer = vx_layer_create(state->world);
    vx_layer_set_display(layer, disp);
    
    vx_layer_camera_op (layer, OP_PROJ_ORTHO);
    
    float eye[3]    = {  0,  0,  9 };
    float lookat[3] = {  0,  0,  0 };
    float up[3]     = {  0,  1,  0 };
    vx_layer_camera_lookat (layer, eye, lookat, up, 1);
    
    float xy0[] = {-PROJ_LENGTH*5.0*PROJ_SCALE+.5,0.5};
    float xy1[] = {PROJ_LENGTH*5.0*PROJ_SCALE - .5,PROJ_HEIGHT*10.0*PROJ_SCALE -.5};
    vx_layer_camera_fit2D(layer, xy0, xy1, 0);

    vx_layer_set_background_color(layer,vx_black);
    vx_layer_add_event_handler(layer, &state->vxeh);

    draw(state, state->world, state->obj_data);
    random_maze_generate(state);

    for(int i = 1; i  < 2*BOT_RADIUS; ++i){
        for(int j = -BOT_RADIUS; j < BOT_RADIUS; ++j){
            state->map[i][state->origin + j] = 0;
        }
    }
}

typedef struct 
{
    int in;
    int walls[4];
}maze_block_t;

typedef struct 
{
    int row;
    int col;
    int path;
}temp_block_t;



int maze_complete(maze_block_t* in[], int rows, int cols)
{
    for(int i = 0; i < rows; ++i){
        for(int j = 0; j < cols; ++j){
            if(in[i][j].in == 0) return 0;
        }
    }
    return 1;
}



temp_block_t get_from_frontier(maze_block_t** maze, int rows, int cols){
    srand(time(NULL));
    temp_block_t frontier[rows*cols];
    int front_size = 0;
    temp_block_t block;
    for(int i = 0; i < rows; ++i){
        for(int j = 0; j < cols; ++j){
            if(maze[i][j].in){
                if(i+1 < rows){ 
                    if(maze[i+1][j].in == 0) {
                        block.row = i + 1;
                        block.col = j;
                        block.path = 0;
                        
                        frontier[front_size] = block;
                        ++front_size;
                    }
                }
                if(i-1 >=0){ 
                    if(maze[i-1][j].in == 0){
                        block.row = i -1;
                        block.col = j;
                        block.path = 2;
                        
                        frontier[front_size] = block;
                        ++front_size;
                    }
                }
                if(j+1 < cols){ 
                    if(maze[i][j+1].in == 0){
                        block.row = i;
                        block.col = j+1;
                        block.path = 3;
                        frontier[front_size] = block;

                        ++front_size;
                    }
                }
                if(j-1 >= 0) {
                    if(maze[i][j-1].in == 0){
                        block.row = i;
                        block.col = j-1; 
                        block.path = 1;
                        frontier[front_size] = block;
                        // if(block.row <0 || block.row >= rows || block.col < 0 || block.col >= cols){
                        //     printf("frontier[%d] = (%d,%d)\n", front_size, block.row, block.col);
                        // }
                        ++front_size;
                    }
                }
                

            }
        }
    }

    int chosen = rand()%front_size;
    return frontier[chosen];
}

void draw_maze(state_t* state, maze_block_t** maze, int rows, int cols){
    int real_row;
    int real_col;
    vx_buffer_t* vb = vx_world_get_buffer(state->world, "maze_grid");
    int length = PROJ_LENGTH;
    int height = PROJ_HEIGHT;
    //printf("rows = %d, cols = %d", rows, cols);

    printf("draw maze\n");
    for(int i = 0; i < rows; ++i){
        for(int j = 0; j < cols; ++j){

            real_row = i *BOT_RADIUS*3;
            real_col = (j - cols/2)* BOT_RADIUS*3;

            if(maze[i][j].walls[0] == 1){
                //printf("print bottom wall of(%d,%d) at (%d,%d)\n",i,j,real_row,real_col);
                for(int k = -BOT_RADIUS*1.5; k < BOT_RADIUS*1.5; ++k){
                    vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((real_col+k)*BLOCK_SCALE, real_row*BLOCK_SCALE),
                                                     vxo_mat_scale(BLOCK_SCALE),
                                                     vxo_box(vxo_mesh_style(vx_maroon))));
                    //printf("bottom wall pos (%d, %d)\n", (real_row), state->origin + real_col + k);
                    build_wall(state, real_row, state->origin +real_col + k, height, length);
                    //build_wall(state, state->origin + real_col + k, real_row, cols, rows);
                }
            }

            if(maze[i][j].walls[1] == 1){
                for(int k = 0; k < 3*BOT_RADIUS; ++k){
                    vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((real_col + 1.5*BOT_RADIUS)*BLOCK_SCALE, (real_row + k)*BLOCK_SCALE),
                                                     vxo_mat_scale(BLOCK_SCALE),
                                                     vxo_box(vxo_mesh_style(vx_maroon))));
                    build_wall(state, real_row+ k, state->origin + real_col + 1.5*BOT_RADIUS, height, length);
                    //build_wall(state, state->origin + real_col + BOT_RADIUS, real_row + k, cols, rows);
                }
            }

            if(maze[i][j].walls[2] == 1){
                for(int k = -BOT_RADIUS*1.5; k < BOT_RADIUS*1.5; ++k){
                    vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((real_col +k)*BLOCK_SCALE, (real_row + 3*BOT_RADIUS)*BLOCK_SCALE),
                                                     vxo_mat_scale(BLOCK_SCALE),
                                                     vxo_box(vxo_mesh_style(vx_maroon))));
                    build_wall(state, real_row+ 3*BOT_RADIUS, state->origin + real_col + k,height, length);
                    //build_wall(state, state->origin + real_col + k, real_row  + 2*BOT_RADIUS, cols, rows);
                }
            }
             
            if(maze[i][j].walls[3] == 1){
                for(int k = 0; k < 3*BOT_RADIUS; ++k){
                    vx_buffer_add_back(vb, vxo_chain(vxo_mat_translate2((real_col - 1.5*BOT_RADIUS)*BLOCK_SCALE, (real_row + k)*BLOCK_SCALE),
                                                     vxo_mat_scale(BLOCK_SCALE),
                                                     vxo_box(vxo_mesh_style(vx_maroon))));
                    build_wall(state, real_row + k, state->origin + real_col - 1.5*BOT_RADIUS, height, length);
                }
            }     
            
        }
    }
    vx_buffer_swap(vb);
}

void random_maze_generate(state_t* state)
{
  

    int length = PROJ_LENGTH;
    int height = PROJ_HEIGHT;

    int rows = 0, cols = 0;

    //for(int i = -length/2 ; i < length/2; i = i + BOT_RADIUS*2){
    for(int i = -BOT_RADIUS *1.5; i > -length/2; i = i - 3*BOT_RADIUS){
        ++cols;
    }

    for(int i = BOT_RADIUS *1.5; i < length/2; i += 3*BOT_RADIUS){
        ++cols;
    }

    for(int i = 0; i < height; i = i + BOT_RADIUS*3){
         ++rows; 
    }
    ++cols;

    maze_block_t** maze;

    maze = malloc(rows * sizeof *maze);

    for(int i = 0; i < rows; ++i){
        maze[i] = malloc(cols * sizeof *maze[i]);
    }

    for(int i = 0; i < rows; ++i){
        for(int j = 0; j < cols; ++j){
            maze[i][j].in = 0;
            maze[i][j].walls[0] = 1;
            maze[i][j].walls[1] = 1;
            maze[i][j].walls[2] = 1;
            maze[i][j].walls[3] = 1;
        }
    }
    

    int r0 = 0;
    int c0 = cols/2;
    
    maze[r0][c0].in = 1;
    printf("yo\n");
    printf("rows = %d, cols = %d\n", rows, cols);
    while(!maze_complete(maze, rows, cols)){
        //printf("maze incomplete\n");
        temp_block_t new_block = get_from_frontier(maze, rows, cols);
        int row_pos = new_block.row;
        int col_pos = new_block.col;
        //printf("added (%d,%d) remove wall %d\n\n", row_pos, col_pos, new_block.path);
        maze[row_pos][col_pos].in = 1;

        if(new_block.path == 0){
            maze[row_pos][col_pos].walls[0] = 0;
            maze[row_pos-1][col_pos].walls[2] = 0;
        }
        else if(new_block.path == 1){
            maze[row_pos][col_pos].walls[1] = 0;
            maze[row_pos][col_pos+1].walls[3] = 0;
        }
        else if(new_block.path == 2){
            maze[row_pos][col_pos].walls[2] = 0;
            maze[row_pos+1][col_pos].walls[0] = 0;
        }
        
        else{
            maze[row_pos][col_pos].walls[3] = 0;
            maze[row_pos][col_pos-1].walls[1] = 0;
        }

    }

    draw_maze(state, maze, rows, cols);

}
