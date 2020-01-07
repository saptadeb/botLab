#ifndef APPS_UTILS_DRAWING_FUNCTIONS_HPP
#define APPS_UTILS_DRAWING_FUNCTIONS_HPP

#include <vx/vx_types.h>
#include <vector>

class ObstacleDistanceGrid;
class OccupancyGrid;
class PoseTrace;
class particles_t;
class pose_xyt_t;
class lidar_t;
class robot_path_t;
struct frontier_t;


/**
* draw_robot draws the robot as an isoceles triangle. The center of the triangle is located at the robot's current
* position and the triangle points in the orientation of the robot's heading.
* 
* \param    pose        Pose at which to draw the robot
* \param    color       Color to draw the robot
* \param    buffer      Buffer to add the robot object to
*/
void draw_robot(const pose_xyt_t& pose, const float color[4],const float body_color[4], vx_buffer_t* buffer);

/**
* draw_pose_trace draws the trace of robot poses as a sequence of connected line segments. A line is drawn between
* each consecutive pair of poses.
* 
* \param    poses           Sequence of poses to be drawn
* \param    color           Color to draw the line segments
* \param    buffer          Buffer to add the line segments to
*/
void draw_pose_trace(const PoseTrace& poses, const float color[4], vx_buffer_t* buffer);

/**
* draw_laser_scan draws a laser scan as a set of rays starting at the pose at which the scan was taken and terminating
* at the measured endpoint of the ray. The laser scan is assumed to be measured from the provided robot pose and that
* the x-axis of the laser rangefinder is aligned with the x-axis of the robot coordinate frame.
* 
* \param    laser           Laser scan to be drawn
* \param    pose            Pose of the robot when the scan was taken
* \param    color           Color to draw the laser scan
* \param    buffer          Buffer to add the laser scan to
*/
void draw_laser_scan(const lidar_t& laser, const pose_xyt_t& pose, const float color[4], vx_buffer_t* buffer);

/**
* draw_occupancy_grid draws an OccupancyGrid as a collection of square cells. Each cell corresponds to the log-odds of
* that particular location being occupied. White indicated the lowest odds of occupancy, i.e. it's free space. Black
* indicates the highest odds of occupancy, i.e. there's definitely an obstacle. Gray means the occupancy is unknown.
* 
* The occupancy grid is rendered as a grayscale image, where each pixel in the image corresponds to one cell in the
* occupancy grid. The bottom left corner of the image corresponds to the origin of the occupancy grid.
* 
* \param    grid        OccupancyGrid to be drawn
* \param    buffer      Buffer to add the occupancy grid image to
*/
void draw_occupancy_grid(const OccupancyGrid& grid, vx_buffer_t* buffer);

/**
* draw_particles draws a set of particles being maintaining by the SLAM particle filter. Each particle has a pose and
* a weight. The easiest approach for drawing particles is as points. This approach allows for seeing where the particles
* are. Additionally, you can adjust the color of the particles based on their weight. Finally, you can draw each
* particle as an arrow rather than a point so you can see the estimated robot heading.
* 
* \param    particles           Weighted particles to be drawn
* \param    buffer              Buffer to add the particles to
*/
void draw_particles(const particles_t& particles, vx_buffer_t* buffer);

/**
* draw_path draws the robot path as a sequence of lines and waypoints. Lines connect consecutive waypoints. A box
* is drawn for each waypoint in the path.
* 
* \param    path        Path to be drawn
* \param    color       Color to draw the path
* \param    buffer      Buffer to add the path objects to
*/
void draw_path(const robot_path_t& path, const float color[4], vx_buffer_t* buffer);

/**
* draw_distance_grid draws an ObstacleDistanceGrid in similar fashion to an OccupancyGrid. The distance grid is 
* drawn as an RGB image with the following properties:
* 
*   - Obstacles are drawn black
*   - Free space in the configuration space is white
*   - Cells too close to a wall are drawn red
* 
* The configuration space radius is specified using the cspaceDistance parameter.
* 
* \param    grid                ObstacleDistanceGrid to draw
* \param    cspaceDistance      Minimum safe distance from walls (meters)
* \param    buffer              Buffer to add the grid image to
*/
void draw_distance_grid(const ObstacleDistanceGrid& grid, float cspaceDistance, vx_buffer_t* buffer);

/**
* draw_frontiers draws the frontiers in the map with one box located at each of the cells that sits along a frontier
* boundary in the occupancy grid.
* 
* \param    frontiers           Frontiers to draw
* \param    metersPerCell       Scale to draw the frontiers -- each box should be this size
* \param    color               Color to draw the frontiers
* \param    buffer              Buffer to add the frontiers to
*/
void draw_frontiers(const std::vector<frontier_t>& frontiers, 
                    double metersPerCell, 
                    const float* color, 
                    vx_buffer_t* buffer);



#endif // APPS_UTILS_DRAWING_FUNCTIONS_HPP
