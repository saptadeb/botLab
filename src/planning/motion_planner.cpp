#include <planning/motion_planner.hpp>
#include <planning/astar.hpp>
#include <common/grid_utils.hpp>
#include <common/timestamp.h>
#include <lcmtypes/robot_path_t.hpp>
#include <cmath>


MotionPlanner::MotionPlanner(const MotionPlannerParams& params)
: params_(params)
{
    setParams(params);
}


MotionPlanner::MotionPlanner(const MotionPlannerParams& params, const SearchParams& searchParams)
: params_(params)
, searchParams_(searchParams)
{
}


robot_path_t MotionPlanner::planPath(const pose_xyt_t& start, 
                                     const pose_xyt_t& goal, 
                                     const SearchParams& searchParams) const
{
    // If the goal isn't valid, then no path can actually exist
    if(!isValidGoal(goal))
    {
        robot_path_t failedPath;
        failedPath.utime = utime_now();
        failedPath.path_length = 1;
        failedPath.path.push_back(start);
        // for (auto& point : failedPath.path) {
        //     printf("\npoint x: %f y: %f ", point.x, point.y);
        // }
        std::cout << "\nINFO: path rejected due to invalid goal\n";        

        return failedPath;
    }
    // Otherwise, use A* to find the path
    return search_for_path(start, goal, distances_, searchParams);
}


robot_path_t MotionPlanner::planPath(const pose_xyt_t& start, const pose_xyt_t& goal) const
{
    return planPath(start, goal, searchParams_);
}


bool MotionPlanner::isValidGoal(const pose_xyt_t& goal) const
{
    float dx = goal.x - prev_goal.x, dy = goal.y - prev_goal.y;
    float distanceFromPrev = std::sqrt(dx * dx + dy * dy);

    //if there's more than 1 frontier, don't go to a target that is within a robot diameter of the current pose
    if(num_frontiers != 1 && distanceFromPrev < 2 * searchParams_.minDistanceToObstacle) return false;

    auto goalCell = global_position_to_grid_cell(Point<double>(goal.x, goal.y), distances_);

    // A valid goal is in the grid
    if(distances_.isCellInGrid(goalCell.x, goalCell.y))
    {
        // And is far enough from obstacles that the robot can physically occupy the space
        // Add an extra cell to account for discretization error and make motion a little safer by not trying to
        // completely snuggle up against the walls in the motion plan
        return distances_(goalCell.x, goalCell.y) > params_.robotRadius;
    }
    
    // A goal must be in the map for the robot to reach it
    return false;
}


bool MotionPlanner::isPathSafe(const robot_path_t& path) const
{

    ///////////// TODO: Implement your test for a safe path here //////////////////
    std::cout << "Check if the path is safe!" << std::endl;
    for (unsigned i=0; i< path.path.size(); i++){
        pose_xyt_t pose = path.path[i];
        int x = pose.x/distances_.metersPerCell() + distances_.widthInCells()/2;
        int y = pose.y/distances_.metersPerCell() + distances_.heightInCells()/2;
        if(distances_(x, y) <= searchParams_.minDistanceToObstacle){
            return false;
        }
    }
    return true;

    // if(path.path_length < 2)
    // {
    //     return false;
    // }
    // return true;
}


void MotionPlanner::setMap(const OccupancyGrid& map)
{
    distances_.setDistances(map);
}


void MotionPlanner::setParams(const MotionPlannerParams& params)
{
    searchParams_.minDistanceToObstacle = params_.robotRadius;
    searchParams_.maxDistanceWithCost = 10.0 * searchParams_.minDistanceToObstacle;
    searchParams_.distanceCostExponent = 1.0;
}


robot_path_t MotionPlanner::planPathToFrontier(std::vector<frontier_t> frontier, pose_xyt_t& start, pose_xyt_t& goal)
{
    // frontier_t rand_frontier = frontiers_[rand()%(frontiers_.size())];
    // Point<float> rand_point = rand_frontier.cells[rand()%(rand_frontier.cells.size())];
    // std::cout << "frontier point: " << rand_point.x << " , " << rand_point.y << std::endl;
    
    
    // Point<int> rand_point_idx = global_position_to_grid_cell(frontier_point,distances_);

    // start from the point and search for the nearest free space point
    
    // 1D map storing whether cells have been explored 
    std::vector<int> explored(distances_.widthInCells()*distances_.heightInCells(),0);

    // // Mark frontier point as explored
    // explored[rand_point_idx.y*distances_.widthInCells()+rand_point_idx.x] = 1; 

    int stop = 0;
    float length = 0;
    float increment = distances_.metersPerCell();
    robot_path_t path;

    while(!stop)
    {
        // increment the search radius
        length += increment;

        // loop through all frontier points
        for(int i = 0;i < frontier.size();i++){
            for(int j = 0;j < frontier[i].cells.size();j++){
                Point<int> frontier_point_idx = global_position_to_grid_cell(frontier[i].cells[j],distances_);
                explored[frontier_point_idx.y*distances_.widthInCells()+frontier_point_idx.x] = 1;
                
                // loop through all cells in the square with length 
                for(int y_search = frontier_point_idx.y - int(length/increment); y_search < frontier_point_idx.y + int(length/increment); y_search++)
                {
                    for(int x_search = frontier_point_idx.x - int(length/increment); x_search < frontier_point_idx.x + int(length/increment); x_search++)
                    {   
                        
                        // check if the cell is in grid
                        if (!distances_.isCellInGrid(x_search,y_search)){
                            // std::cout << "not cell in grid" << std::endl;
                            continue;
                        }
                        
                        // check if the cell is explored
                        if (explored[y_search*distances_.widthInCells()+x_search]){
                            // std::cout << "cell explored before" << std::endl;
                            continue;
                        }
                        
                        // check if the cell is inside the circle
                        float distance = distances_.metersPerCell()*sqrt(((x_search-frontier_point_idx.x)*(x_search-frontier_point_idx.x)+ \
                                                                        (y_search-frontier_point_idx.y)*(y_search-frontier_point_idx.y)));
                        if (distance > length){
                            // std::cout << "cell not in search circle" << std::endl;
                            continue;
                        }

                        // check if the cell is in free space 
                        if (distances_(x_search,y_search) > 0.2){
                            
                            // try to plan a path to it 
                            Point<double> goal_point = grid_position_to_global_position(Point<double>(x_search,y_search),distances_);
                            pose_xyt_t goal_try;
                            goal_try.x = goal_point.x;
                            goal_try.y = goal_point.y;

                            path = planPath(start,goal_try);
                            
                            if(path.path_length){
                                goal.x = goal_try.x;
                                goal.y = goal_try.y;
                                stop = 1;
                                std::cout << "find viable path" << std::endl;
                                break;
                            }
                        }
                        explored[y_search*distances_.widthInCells()+x_search] = 1;
                    }
                    if(stop) break;
                }
                if(stop) break;
            }
            if(stop) break;
        }
                
        if (*std::min_element(explored.begin(), explored.end())) stop = 1;
    }

    std::cout << "path length: " << path.path_length << std::endl;
    std::cout << "start: " << start.x << " , " << start.y << std::endl;
    std::cout << "goal: " << goal.x << " , " << goal.y << std::endl;

    return path;
            

            //     // If minDistance is renewed or all cells are explored
            //     if ((std::abs(minDistance-(map.heightInCells()+map.widthInCells())*metersPerCell_) > 0.01) || *std::min_element(explored.begin(), explored.end())) stop = 1;
            // }
}

robot_path_t MotionPlanner::planPathBackHome(pose_xyt_t& start, pose_xyt_t& goal)
{
    // frontier_t rand_frontier = frontiers_[rand()%(frontiers_.size())];
    // Point<float> rand_point = rand_frontier.cells[rand()%(rand_frontier.cells.size())];
    // std::cout << "frontier point: " << rand_point.x << " , " << rand_point.y << std::endl;
    
    
    // Point<int> rand_point_idx = global_position_to_grid_cell(frontier_point,distances_);

    // start from the point and search for the nearest free space point
    
    // 1D map storing whether cells have been explored 
    std::vector<int> explored(distances_.widthInCells()*distances_.heightInCells(),0);

    // // Mark frontier point as explored
    // explored[rand_point_idx.y*distances_.widthInCells()+rand_point_idx.x] = 1; 

    int stop = 0;
    float length = 0;
    float increment = distances_.metersPerCell();
    robot_path_t path;

    Point<int> goal_idx = global_position_to_grid_cell(Point<double>(goal.x,goal.y),distances_);

    // check if goal point is reachable
    if (distances_(goal.x,goal.y) > 0.2){

        path = planPath(start,goal);
        
        std::cout << "find viable path" << std::endl;

        std::cout << "path length: " << path.path_length << std::endl;
        std::cout << "start: " << start.x << " , " << start.y << std::endl;
        std::cout << "goal: " << goal.x << " , " << goal.y << std::endl;

        return path;
    }
    explored[goal_idx.y*distances_.widthInCells()+goal_idx.x] = 1;


    while(!stop)
    {
        // increment the search radius
        length += increment;
                
                // loop through all cells in the square with length 
        for(int y_search = goal_idx.y - int(length/increment); y_search < goal_idx.y + int(length/increment); y_search++)
        {
            for(int x_search = goal_idx.x - int(length/increment); x_search < goal_idx.x + int(length/increment); x_search++)
            {   
                
                // check if the cell is in grid
                if (!distances_.isCellInGrid(x_search,y_search)){
                    // std::cout << "not cell in grid" << std::endl;
                    continue;
                }
                
                // check if the cell is explored
                if (explored[y_search*distances_.widthInCells()+x_search]){
                    // std::cout << "cell explored before" << std::endl;
                    continue;
                }
                
                // check if the cell is inside the circle
                float distance = distances_.metersPerCell()*sqrt(((x_search-goal_idx.x)*(x_search-goal_idx.x)+ \
                                                                (y_search-goal_idx.y)*(y_search-goal_idx.y)));
                if (distance > length){
                    // std::cout << "cell not in search circle" << std::endl;
                    continue;
                }

                // check if the cell is in free space 
                if (distances_(x_search,y_search) > 0.2){
                    
                    // try to plan a path to it 
                    Point<double> goal_point = grid_position_to_global_position(Point<double>(x_search,y_search),distances_);
                    pose_xyt_t goal_try;
                    goal_try.x = goal_point.x;
                    goal_try.y = goal_point.y;

                    path = planPath(start,goal_try);
                    
                    if(path.path_length){
                        goal.x = goal_try.x;
                        goal.y = goal_try.y;
                        stop = 1;
                        std::cout << "find viable path" << std::endl;
                        break;
                    }
                }
                explored[y_search*distances_.widthInCells()+x_search] = 1;
            }
            if(stop) break;
        }
                
        if (*std::min_element(explored.begin(), explored.end())) stop = 1;
    }

    std::cout << "path length: " << path.path_length << std::endl;
    std::cout << "start: " << start.x << " , " << start.y << std::endl;
    std::cout << "goal: " << goal.x << " , " << goal.y << std::endl;

    return path;
            

            //     // If minDistance is renewed or all cells are explored
            //     if ((std::abs(minDistance-(map.heightInCells()+map.widthInCells())*metersPerCell_) > 0.01) || *std::min_element(explored.begin(), explored.end())) stop = 1;
            // }
}