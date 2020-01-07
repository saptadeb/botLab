#ifndef PLANNING_FRONTIERS_HPP
#define PLANNING_FRONTIERS_HPP

#include <common/point.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <lcmtypes/robot_path_t.hpp>
#include <vector>

class MotionPlanner;
class OccupancyGrid;

/**
* frontier_t represents a frontier in the map. A frontier is a collection of contiguous cells in the occupancy grid that
* sits on the border of known free space and unknown space.
*/
struct frontier_t
{
    std::vector<Point<float>> cells;  // The global coordinate of cells that make up the frontier
};


/**
* find_map_frontiers locates all frontiers in the provided map. A frontier cell is an unknown cell (log-odds == 0) that
* borders a free space cell (log-odds < 0). A frontier is a contiguous region of frontier cells.
* 
* The frontiers found by this function are all frontiers that are reachable through free space from the current robot
* pose. By defining frontiers in this way, you don't have to worry about a small mapping error creating an unreachable
* frontier that's on the opposite side of a wall. All frontiers returned by this function are connected by free space
* and therefore reachable -- potentially, as the exact reachability depends on the configuration space of the robot.
* 
* \param    map                     Map in which to find the frontiers
* \param    robotPose               Pose of the robot at the start
* \param    minFrontierLength       Minimum length of a valid frontier (meters) (optional, default = 0.1m)
* \return   All frontiers found in the map. A fully-explored map will have no frontiers, so the returned vector will be
*   empty in that case.
*/
std::vector<frontier_t> find_map_frontiers(const OccupancyGrid& map, 
                                           const pose_xyt_t& robotPose,
                                           double minFrontierLength = 0.35);


/**
* plan_path_to_frontier selects amongst the available frontiers and plans a path to one of them. The path to the
* frontier is returned. If no frontiers exist or there are no valid paths to any of the frontiers, then a path of length
* 1, with the only pose being the robot pose should be returned indicating an error.
* 
* \param    frontiers           Frontiers in the environment
* \param    robotPose           Pose of the robot from which to plan
* \param    map                 Map being explored
* \param    planner             Planner to use for finding the next frontier
* \return   Path to the selected frontier or a path indicating failure, as described above.
*/
robot_path_t plan_path_to_frontier(const std::vector<frontier_t>& frontiers, 
                                   const pose_xyt_t& robotPose,
                                   const OccupancyGrid& map,
                                   const MotionPlanner& planner);

#endif // PLANNING_FRONTIERS_HPP
