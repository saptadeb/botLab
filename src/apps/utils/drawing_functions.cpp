#include <apps/utils/drawing_functions.hpp>
#include <common/pose_trace.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <lcmtypes/lidar_t.hpp>
#include <lcmtypes/robot_path_t.hpp>
#include <lcmtypes/particle_t.hpp>
#include <lcmtypes/particles_t.hpp>
#include <planning/frontiers.hpp>
#include <planning/obstacle_distance_grid.hpp>
#include <slam/occupancy_grid.hpp>

// Headers to remove for stencil version
#include <imagesource/image_u8.h>
#include <imagesource/image_u32.h>
#include <vx/vx_world.h>
#include <vx/vx_codes.h>
#include <vx/vxo_chain.h>
#include <vx/vx_colors.h>
#include <vx/vxo_box.h>
#include <vx/vxo_image.h>
#include <vx/vxo_lines.h>
#include <vx/vxo_mat.h>
#include <vx/vxo_mesh.h>
#include <vx/vxo_robot.h>
#include <vx/vxo_circle.h>
#include <vx/vxo_points.h>




void draw_robot(const pose_xyt_t& pose, const float* color,  const float* body_color, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw robot at the pose using vxo_robot ////////////////////////////
    vx_buffer_add_back(buffer, vxo_chain(vxo_mat_translate3(pose.x, pose.y, 0.0),
                                         vxo_mat_scale(0.1f),
                                         vxo_circle(vxo_mesh_style(body_color))));

    vx_buffer_add_back(buffer, vxo_chain(vxo_mat_translate3(pose.x, pose.y, 0.0),
                                         vxo_mat_rotate_z(pose.theta),
                                         vxo_mat_scale(0.15f),
                                         vxo_robot(vxo_mesh_style(color))));
}


void draw_pose_trace(const PoseTrace& poses, const float* color, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw PoseTrace as line segments connecting consecutive poses ////////////////////////////

    // Create a resource to hold all the poses
    vx_resc* poseResc = vx_resc_createf(poses.size() * 3);
    float* poseBuf = static_cast<float*>(poseResc->res);
    for(auto& p : poses)
    {
        *poseBuf++ = p.x;
        *poseBuf++ = p.y;
        *poseBuf++ = 0.0;
    }

    vx_object_t* trace = vxo_lines(poseResc,
                                   poses.size(),
                                   GL_LINE_STRIP,
                                   vxo_lines_style(color, 2.0));
    vx_buffer_add_back(buffer, trace);
}


void draw_laser_scan(const lidar_t& laser, const pose_xyt_t& pose, const float* color, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw lidar_t as specified in assignment ////////////////////////////

    std::vector<float> laser_points(laser.num_ranges * 4);  // x y x y x y ....

    for(int rayIndex = 0; rayIndex < laser.num_ranges; ++rayIndex)
    {
        laser_points[rayIndex*4]     = 0.0f;
        laser_points[rayIndex*4 + 1] = 0.0f;
        laser_points[rayIndex*4 + 2] = laser.ranges[rayIndex] * std::cos(-laser.thetas[rayIndex]);  // x-coordinate
        laser_points[rayIndex*4 + 3] = laser.ranges[rayIndex] * std::sin(-laser.thetas[rayIndex]);  // y-coordinate
    }

    vx_resc* vx_laser_ray_resc = vx_resc_copyf(laser_points.data(), laser_points.size());
    vx_object_t* scan_outline = vxo_chain(vxo_mat_translate2(pose.x, pose.y),
                                          vxo_mat_rotate_z  (pose.theta),
                                          vxo_lines(vx_laser_ray_resc,
                                                    laser.num_ranges*2,
                                                    GL_LINES,
                                                    vxo_lines_style(color, 1.0)));

    vx_buffer_add_back(buffer, scan_outline);
}


void draw_occupancy_grid(const OccupancyGrid& grid, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw OccupancyGrid as specified in assignment ////////////////////////////

    image_u8_t* gridImg = image_u8_create(grid.widthInCells(), grid.heightInCells());

    // Copy over the data -- want occupied cells black, i.e. 0 luminance, so need to invert the scale
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            gridImg->buf[gridImg->stride*y + x] = 127 - grid(x, y);
        }
    }

    vx_object_t* gridObject = vxo_image_from_u8(gridImg, 0, VX_TEX_MIN_FILTER);
    vx_buffer_add_back(buffer,
                       vxo_chain(vxo_mat_translate3(grid.originInGlobalFrame().x,
                                                    grid.originInGlobalFrame().y,
                                                    0),
                                 vxo_mat_scale(grid.metersPerCell()),
                       gridObject));

    image_u8_destroy(gridImg);
}


void draw_particles(const particles_t& particles, vx_buffer_t* buffer)
{
    int total_points = particles.num_particles;
    float particle_plot[2 * total_points];
    std::vector<particle_t> temp;
    float particle_color[4 * total_points];

    int i = 0;
    for (auto& temp : particles.particles)
    {
      particle_plot[2*i] = temp.pose.x;
      particle_plot[2*i + 1] = temp.pose.y;
      particle_color[4*i] = 255 * temp.weight;      //red
      particle_color[4*i + 1] = 0;    //green
      particle_color[4*i + 2] = -255*temp.weight + 255;    //blue
      particle_color[4*i + 3] = 255;  //alpha
      i++;
    }

    vx_resc_t *colors = vx_resc_copyf(particle_color, total_points*4);
    vx_resc_t *estimated_poses = vx_resc_copyf(particle_plot, total_points*2);
    vx_buffer_add_back(buffer, vxo_points(estimated_poses, total_points, vxo_points_style_multi_colored(colors, 2.5f)));
}


void draw_path(const robot_path_t& path, const float* color, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw robot_path_t as specified in assignment ////////////////////////////

    if(path.path_length == 0)
    {
        return;
    }

    // Draw the path as line segments between target poses, which are drawn as little robots
    for(auto& pose : path.path)
    {
        float wypnt_color[] ={ 0.0f, 0.5f, 0.0f, 1.0f}; //Define waypoints with clear cyan color
        vx_buffer_add_back(buffer, vxo_chain(vxo_mat_translate3(pose.x, pose.y, 0.0),
                                         vxo_mat_scale(0.015f),
                                         vxo_circle(vxo_mesh_style(wypnt_color))));
    }

    vx_resc* poseResc = vx_resc_createf(path.path.size() * 3);
    float* poseBuf = static_cast<float*>(poseResc->res);
    for(auto& pose : path.path)
    {
        *poseBuf++ = pose.x;
        *poseBuf++ = pose.y;
        *poseBuf++ = 0.0;
    }

    vx_object_t* trace = vxo_lines(poseResc,
                                   path.path.size(),
                                   GL_LINE_STRIP,
                                   vxo_lines_style(color, 2.0));
    vx_buffer_add_back(buffer, trace);
}


void draw_distance_grid(const ObstacleDistanceGrid& grid, float cspaceDistance, vx_buffer_t* buffer)
{
    ////////////////// TODO: Draw ObstacleDistanceGrid as specified in assignment ////////////////////////////
    //////// We recommend using image_u32 to represent the different colors /////////////////////////////////

    image_u32_t* img = image_u32_create(grid.widthInCells(), grid.heightInCells());

    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            int index = x + y*img->stride;


            if(grid(x, y) > 0.35f) // free space
            {
                img->buf[index] = 0xFFFFFFFF; // white
            }
            else if((grid(x, y) >= 0.15f)&&(grid(x, y) < 0.25f))
            {
                img->buf[index] = 0xFFFF0000; // blue
            }
            else if((grid(x, y) >= 0.25f)&&(grid(x, y) <= 0.35f))
            {
                img->buf[index] = 0xFF00FF00; // green
            }
            else if(grid(x, y) == 0.0f) // an obstacle
            {
                img->buf[index] = 0xFF000000; // black
            }
            else // inside the collision zone
            {
                img->buf[index] = 0xFF0000FF; // red
            }
        }
    }

    vx_object_t* gridObject = vxo_image_from_u32(img, 0, VX_TEX_MIN_FILTER);
    vx_buffer_add_back(buffer,
                       vxo_chain(vxo_mat_translate3(grid.originInGlobalFrame().x,
                                                    grid.originInGlobalFrame().y,
                                                    0),
                                 vxo_mat_scale(grid.metersPerCell()),
                       gridObject));

    image_u32_destroy(img);
}


void draw_frontiers(const std::vector<frontier_t>& frontiers,
                    double metersPerCell,
                    const float* color,
                    vx_buffer_t* buffer)
{
    //////////////////// TODO: Draw the frontiers using one box for each cell located along a frontier ////////////////

    for(auto& f : frontiers)
    {
        for(auto& c : f.cells)
        {
            vx_buffer_add_back(buffer, vxo_chain(vxo_mat_translate3(c.x, c.y, 0.0),
                                                 vxo_mat_scale(metersPerCell),
                                                 vxo_box(vxo_mesh_style(color))));
        }
    }
}
