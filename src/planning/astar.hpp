#ifndef PLANNING_ASTAR_HPP
#define PLANNING_ASTAR_HPP

#include <lcmtypes/robot_path_t.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <common/point.hpp>
#include <vector>
using namespace std;

class ObstacleDistanceGrid;

/**
* SearchParams defines the parameters to use when searching for a path. See associated comments for details
*/
struct SearchParams
{
    double minDistanceToObstacle;   ///< The minimum distance a robot can be from an obstacle before
                                    ///< a collision occurs
                                    
    double maxDistanceWithCost;     ///< The maximum distance from an obstacle that has an associated cost. The planned
                                    ///< path will attempt to stay at least this distance from obstacles unless it must
                                    ///< travel closer to actually find a path
                                    
    double distanceCostExponent;    ///< The exponent to apply to the distance cost, whose function is:
                                    ///<   pow(maxDistanceWithCost - cellDistance, distanceCostExponent)
                                    ///< for cellDistance > minDistanceToObstacle && cellDistance < maxDistanceWithCost
};

typedef Point<int> cell_t;

struct Node{
    cell_t cell;            // Cell represented by this node
    cell_t parent;
    float gCost;        
    float hCost;
    float fCost = gCost + hCost;
    bool operator<(const Node& rhs) const
    {
        return rhs.fCost < fCost;
    }
};


/**
* search_for_path uses an A* search to find a path from the start to goal poses. The search assumes a circular robot
* 
* \param    start           Starting pose of the robot
* \param    goal            Desired goal pose of the robot
* \param    distances       Distance to the nearest obstacle for each cell in the grid
* \param    params          Parameters specifying the behavior of the A* search
* \return   The path found to the goal, if one exists. If the goal is unreachable, then a path with just the initial
*   pose is returned, per the robot_path_t specification.
*/
robot_path_t search_for_path(pose_xyt_t start, 
                             pose_xyt_t goal, 
                             const ObstacleDistanceGrid& distances,
                             const SearchParams& params);

int get_gCost(Node parent, cell_t current);
int get_hCost(Point<double> goal, cell_t current);
bool is_member(const cell_t toSearch_cell, vector<Node> givenList);
Node get_member(const cell_t toSearch_cell, vector<Node> givenList);
bool is_goal(const cell_t currCell, const Point<double> goalPt);
vector<Node> expand_node(Node currentNode, const ObstacleDistanceGrid& grid);
robot_path_t makePath(Node node, Node start, robot_path_t initPath, const ObstacleDistanceGrid& distances);


#endif // PLANNING_ASTAR_HPP
