#include <planning/astar.hpp>
#include <planning/obstacle_distance_grid.hpp>
#include <common/grid_utils.hpp>
#include <iostream>
#include <queue>
#include <vector>
using namespace std;

robot_path_t search_for_path(pose_xyt_t start, 
                             pose_xyt_t goal, 
                             const ObstacleDistanceGrid& distances,
                             const SearchParams& params)
{
    ////////////////// TODO: Implement your A* search here //////////////////////////
    
    //defining home and end point
    cell_t home, end;
    home.x = start.x;
    home.y = start.y;
    end.x = goal.x;
    end.y = goal.y;
    cell_t start_cell = global_position_to_grid_cell(home, distances);
    Point<double> goal_point = global_position_to_grid_position(end, distances);
    
    // printf("%d", );

    priority_queue<Node> openList;
    vector<Node> closedList;

    Node firstNode;
    firstNode.cell = start_cell;
    firstNode.gCost = 0;
    firstNode.hCost = 0;
    firstNode.fCost += pow(params.maxDistanceWithCost - distances(firstNode.cell.x, firstNode.cell.y), params.distanceCostExponent);
    openList.push(firstNode);

    robot_path_t path;
    path.utime = start.utime;
    path.path.push_back(start);    

    while (!openList.empty()){
        Node nNode = openList.top();
        openList.pop();
        vector<Node> kids = expand_node(nNode, distances);
        for (auto& kid : kids){
            Node ngbr;      // neighbour
            if(is_member(kid.cell, closedList)){
                ngbr = get_member(kid.cell, closedList);
            }else{
                ngbr = kid;
            }
            if(distances.isCellInGrid(kid.cell.x,kid.cell.y) && distances(kid.cell.x,kid.cell.y) != 0){
                ngbr.gCost = get_gCost(nNode, ngbr.cell);
                ngbr.hCost = get_hCost(goal_point, ngbr.cell);
                if (distances(ngbr.cell.x, ngbr.cell.y) > params.minDistanceToObstacle && distances(ngbr.cell.x, ngbr.cell.y) < params.maxDistanceWithCost)
                    ngbr.fCost += pow(params.maxDistanceWithCost - distances(ngbr.cell.x, ngbr.cell.y), params.distanceCostExponent);
                ngbr.parent = nNode.cell;
                if (is_goal(ngbr.cell, goal_point)){
                    return makePath(ngbr, firstNode, path, distances);
                }
                if(is_member(ngbr.cell, closedList)){
                    openList.push(ngbr);
                }
            }
        }
        closedList.push_back(nNode);
    }
    return path;
}

int get_gCost(Node parent, cell_t current){
    int x = abs(parent.cell.x - current.x); 
    int y = abs(parent.cell.y - current.y);
    if((x == 1) & (y == 1))
        return parent.gCost + 14;
    else
        return parent.gCost + 10;
}

int get_hCost(Point<double> goal, cell_t current){
    int x = abs(goal.x - current.x);
    int y = abs(goal.y - current.y);
    int cost;
    if (x >= y)
        cost = 14*y + 10*(x-y);
    else
        cost = 14*x + 10*(y-x);
    return cost;
}

bool is_member(const cell_t toSearch_cell, vector<Node> givenList){
    for (auto& n : givenList){
        if (n.cell.x == toSearch_cell.x && n.cell.y == toSearch_cell.y){   
            return true;
        }
    }
    return false;
}

Node get_member(const cell_t toSearch_cell, vector<Node> givenList){
    for (auto& n : givenList){
        if (n.cell.x == toSearch_cell.x && n.cell.y == toSearch_cell.y){   
            return n;
        }
    }
}

bool is_goal(const cell_t currCell, const Point<double> goalPt){
    if(currCell.x == goalPt.x && currCell.y == goalPt.y){
        return true;
    }
    return false;
}


vector<Node> expand_node(Node currentNode, const ObstacleDistanceGrid& grid){
    // Perform a expansion of each cell
    const int xDeltas[8] = { 1, -1, 0,  0, 1, 1, -1, -1 };
    const int yDeltas[8] = { 0,  0, 1, -1, 1, -1, 1, -1 };
    
    vector<Node> kids;
    
    for(int n = 0; n < 4; ++n)          //CHANGE TO 8 TO INCLUDE DIAGONALS AS WELL
    {
        cell_t adjacentCell(currentNode.cell.x + xDeltas[n], currentNode.cell.y + yDeltas[n]);

        if(grid.isCellInGrid(adjacentCell.x, adjacentCell.y))
        {
            Node kid;
            kid.parent = currentNode.cell;
            kid.cell = adjacentCell;
            kids.push_back(kid);
        }
    return kids;    
    }
}

robot_path_t makePath(Node node, Node start, robot_path_t initPath, const ObstacleDistanceGrid& distances){
    Node currNode = node;
    int c = 0;
    float futX, futY;
    while (currNode.cell != start.cell){
        Point<double> temp;
        temp.x = currNode.cell.x;
        temp.y = currNode.cell.y;
        Point<double> temp2 = grid_position_to_global_position(temp, distances);
        pose_xyt_t nextPoint;
        nextPoint.x = temp2.x;
        nextPoint.y = temp2.y;
        if (c == 0){
            nextPoint.theta = 0.0;
        }else{
            nextPoint.theta = atan2(futY - temp.y, futX - temp.x);
        }                
        initPath.path.push_back(nextPoint);
        futX = temp.x;
        futY = temp.y;
    }
    initPath.path_length = initPath.path.size();
    return initPath;
}