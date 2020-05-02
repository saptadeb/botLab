#include <planning/frontiers.hpp>
#include <planning/motion_planner.hpp>
#include <common/grid_utils.hpp>
#include <slam/occupancy_grid.hpp>
#include <lcmtypes/robot_path_t.hpp>
#include <queue>
#include <set>
#include <cassert>


bool is_frontier_cell(int x, int y, const OccupancyGrid& map);
frontier_t grow_frontier(Point<int> cell, const OccupancyGrid& map, std::set<Point<int>>& visitedFrontiers);
robot_path_t path_to_frontier(const frontier_t& frontier, 
                              const pose_xyt_t& pose, 
                              const OccupancyGrid& map,
                              const MotionPlanner& planner);
pose_xyt_t nearest_navigable_cell(pose_xyt_t pose, 
                                  Point<float> desiredPosition, 
                                  const OccupancyGrid& map,
                                  const MotionPlanner& planner);
pose_xyt_t search_to_nearest_free_space(Point<float> position, const OccupancyGrid& map, const MotionPlanner& planner);
double path_length(const robot_path_t& path);


std::vector<frontier_t> find_map_frontiers(const OccupancyGrid& map, 
                                           const pose_xyt_t& robotPose,
                                           double minFrontierLength)
{
    /*
    * To find frontiers, we use a connected components search in the occupancy grid. Each connected components consists
    * only of cells where is_frontier_cell returns true. We scan the grid until an unvisited frontier cell is
    * encountered, then we grow that frontier until all connected cells are found. We then continue scanning through the
    * grid. This algorithm can also perform very fast blob detection if you change is_frontier_cell to some other check
    * based on pixel color or another condition amongst pixels.
    */
    std::vector<frontier_t> frontiers;
    std::set<Point<int>> visitedCells;
    
    Point<int> robotCell = global_position_to_grid_cell(Point<float>(robotPose.x, robotPose.y), map);
    std::queue<Point<int>> cellQueue;
    cellQueue.push(robotCell);
    visitedCells.insert(robotCell);
  
    // Use a 4-way connected check for expanding through free space.
    const int kNumNeighbors = 4;
    const int xDeltas[] = { -1, 1, 0, 0 };
    const int yDeltas[] = { 0, 0, 1, -1 };
    
    // Do a simple BFS to find all connected free space cells and thus avoid unreachable frontiers
    while(!cellQueue.empty())
    {
        Point<int> nextCell = cellQueue.front();
        cellQueue.pop();
        
        // Check each neighbor to see if it is also a frontier
        for(int n = 0; n < kNumNeighbors; ++n)
        {
            Point<int> neighbor(nextCell.x + xDeltas[n], nextCell.y + yDeltas[n]);
            
            // If the cell has been visited or isn't in the map, then skip it
            if(visitedCells.find(neighbor) != visitedCells.end() || !map.isCellInGrid(neighbor.x, neighbor.y))
            {
                continue;
            }
            // If it is a frontier cell, then grow that frontier
            else if(is_frontier_cell(neighbor.x, neighbor.y, map))
            {
                frontier_t f = grow_frontier(neighbor, map, visitedCells);
                
                // If the frontier is large enough, then add it to the collection of map frontiers
                if(f.cells.size() * map.metersPerCell() >= minFrontierLength)
                {
                    frontiers.push_back(f);
                }
            }
            // If it is a free space cell, then keep growing the frontiers
            else if(map(neighbor.x, neighbor.y) < 0)
            {
                visitedCells.insert(neighbor);
                cellQueue.push(neighbor);
            }
        }
    }
    
    return frontiers;
}

bool check_valid(const MotionPlanner& planner, float x, float y, pose_xyt_t curr_pose) {
    pose_xyt_t pose;
    pose.x = x;
    pose.y = y;

    if (planner.isValidGoal(pose)) {
        robot_path_t temp_path = planner.planPath(curr_pose, pose);
        if(temp_path.path_length <3) 
            return false;
        else
            return planner.isPathSafe(temp_path);
    } else {
        return false;
    }
}

robot_path_t plan_path_to_frontier(const std::vector<frontier_t>& frontiers, 
                                   const pose_xyt_t& robotPose,
                                   const OccupancyGrid& map,
                                   const MotionPlanner& planner)
{
    ///////////// TODO: Implement your strategy to select the next frontier to explore here //////////////////
    /*
    * NOTES:
    *   - If there's multiple frontiers, you'll need to decide which to drive to.
    *   - A frontier is a collection of cells, you'll need to decide which one to attempt to drive to.
    *   - The cells along the frontier might not be in the configuration space of the robot, so you won't necessarily
    *       be able to drive straight to a frontier cell, but will need to drive somewhere close.
    */
    robot_path_t emptyPath;

    // if no frontiers, return empty path
    if (frontiers.size() == 0) return emptyPath;
    
    float min_dist = 99999999999999;
    frontier_t closest_frontier;
    Point<float> closest_point;

    // step 1: figure out which of the frontiers to drive to
    // use shortest euclidian distance frontier, effectively making breadth first search
    for (auto frontier : frontiers) {
        for (auto point : frontier.cells) {
            float distance_sq = (robotPose.x - point.x)*(robotPose.x - point.x) + (robotPose.y - point.y)*(robotPose.y - point.y) ;// euclidian distance
            if (distance_sq < min_dist) {
                closest_frontier = frontier;
                closest_point = point;
                min_dist = distance_sq;
            }
        }
    }

    closest_point = closest_frontier.cells[int((closest_frontier.cells.size()-1)/2)];


    // Search around the closest frontier until you find the closest point that you can get to
    bool foundPose = false;
    float square_radius = .025; // .05
    float sq_len = .025; // .05;
    pose_xyt_t goal_pose;
    while (!foundPose) {
        std::cout << "Looking for free spot to find path to!\n";
        // find the white point        //check top and bottom
        float top_height = closest_point.y + square_radius;
        float bot_height = closest_point.y - square_radius;
        for (float i = -square_radius; i <= square_radius; i+=sq_len ) {
            bool valid_point_top = check_valid(planner, closest_point.x + i, top_height, robotPose); // absolute x, absolute y
            bool valid_point_bot = check_valid(planner, closest_point.x + i, bot_height, robotPose);
            /*
            Point<double> check_top;
            check_top.x = closest_point.x + i;
            check_top.y = top_height;
            printf("Check top X: %d, Y: %d\n", global_position_to_grid_cell(check_top, map).x, global_position_to_grid_cell(check_top, map).y);
            Point<double> check_bot;
            check_bot.x = closest_point.x + i;
            check_bot.y = bot_height;
            printf("Check bot X: %d, Y: %d\n", global_position_to_grid_cell(check_bot, map).x, global_position_to_grid_cell(check_bot, map).y);
            */
            if (valid_point_top) {
                foundPose = true;
                goal_pose.x = closest_point.x + i;
                goal_pose.y = top_height;
            } else if (valid_point_bot) {
                foundPose = true;
                goal_pose.x = closest_point.x + i;
                goal_pose.y = bot_height;
            }
        }        // check left and right
        float left_bound = closest_point.y + square_radius;
        float right_bound = closest_point.y - square_radius;
        for (float i = -square_radius; i <= square_radius; i+=sq_len ) {
            bool valid_point_right = check_valid(planner, right_bound, closest_point.y + i, robotPose);
            bool valid_point_left  = check_valid(planner, left_bound, closest_point.y + i, robotPose);
            /*
            Point<double> check_left;
            check_left.x = left_bound;
            check_left.y = closest_point.y + i;
            printf("Check left X: %d, Y: %d\n", global_position_to_grid_cell(check_left, map).x, global_position_to_grid_cell(check_left, map).y);
            Point<double> check_right;
            check_right.x = right_bound;
            check_right.y = closest_point.y + i;
            printf("Check right X: %d, Y: %d\n", global_position_to_grid_cell(check_right, map).x, global_position_to_grid_cell(check_right, map).y);
            */
            if (valid_point_right) {
                foundPose = true;
                goal_pose.x = right_bound;
                goal_pose.y = closest_point.y + i;
            } else if (valid_point_left) {
                foundPose = true;
                goal_pose.x = left_bound;
                goal_pose.y = closest_point.y + i;
            }
        }        
        if (square_radius < 0.5)    
            square_radius += sq_len;
        else 
            square_radius = 0.05;
    }    /*
    pose_xyt_t goal_pose;
    goal_pose.x = closest_point.x;
    goal_pose.y = closest_point.y;
    */


   // Plan a path to that point that is closest to the frontier and you can get to
    goal_pose.theta = robotPose.theta;    // step 2: call motion planner to get a path to that frontier with astar
    return planner.planPath(robotPose, goal_pose);
}


bool is_frontier_cell(int x, int y, const OccupancyGrid& map)
{
    // A cell is a frontier if it has log-odds 0 and a neighbor has log-odds < 0
    
    // A cell must be in the grid and must have log-odds 0 to even be considered as a frontier
    /*
    if(!map.isCellInGrid(x, y) || (map(x, y) != 0))
    {
        return false;
    }*/

    // Made requirements for being a frontier less strict because sometimes there
    // were no frontiers when there should have been
    if(!map.isCellInGrid(x, y) || (map(x, y) > .1 || map(x,y) < -5))
    {
        return false;
    }
    
    const int kNumNeighbors = 4;
    const int xDeltas[] = { -1, 1, 0, 0 };
    const int yDeltas[] = { 0, 0, 1, -1 };
    
    for(int n = 0; n < kNumNeighbors; ++n)
    {
        // If any of the neighbors are free, then it's a frontier
        // Note that logOdds returns 0 for out-of-map cells, so no explicit check is needed.
        if(map.logOdds(x + xDeltas[n], y + yDeltas[n]) < 0) // change this to -30 from 0
        {
            return true;
        }
    }
    
    return false;
}


frontier_t grow_frontier(Point<int> cell, const OccupancyGrid& map, std::set<Point<int>>& visitedFrontiers)
{
    // Every cell in cellQueue is assumed to be in visitedFrontiers as well
    std::queue<Point<int>> cellQueue;
    cellQueue.push(cell);
    visitedFrontiers.insert(cell);
    
    // Use an 8-way connected search for growing a frontier
    const int kNumNeighbors = 8;
    const int xDeltas[] = { -1, -1, -1, 1, 1, 1, 0, 0 };
    const int yDeltas[] = {  0,  1, -1, 0, 1,-1, 1,-1 };
 
    frontier_t frontier;
    
    // Do a simple BFS to find all connected frontier cells to the starting cell
    while(!cellQueue.empty())
    {
        Point<int> nextCell = cellQueue.front();
        cellQueue.pop();
        
        // The frontier stores the global coordinate of the cells, so convert it first
        frontier.cells.push_back(grid_position_to_global_position(nextCell, map));
        
        // Check each neighbor to see if it is also a frontier
        for(int n = 0; n < kNumNeighbors; ++n)
        {
            Point<int> neighbor(nextCell.x + xDeltas[n], nextCell.y + yDeltas[n]);
            if((visitedFrontiers.find(neighbor) == visitedFrontiers.end()) 
                && (is_frontier_cell(neighbor.x, neighbor.y, map)))
            {
                visitedFrontiers.insert(neighbor);
                cellQueue.push(neighbor);
            }
        }
    }
    
    return frontier;
}