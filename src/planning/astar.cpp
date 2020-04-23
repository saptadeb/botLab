#include <planning/astar.hpp>
#include <planning/obstacle_distance_grid.hpp>
#include <common/grid_utils.hpp>
#include <numeric>
#include <array>
#include <stack>
#include <cfloat>
#include <iostream>
#include <queue>
using namespace std;

typedef Point<int> cell_t;

struct Node
{
    cell_t cell;            // Cell represented by this node
    cell_t parent;
    float gCost;        
    float hCost;
    float fCost = gCost + hCost;


};

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
    cell_t start_cell = global_position_to_grid_position(home, distances);
    cell_t goal_cell = global_position_to_grid_position(end, distances);

    priority_queue<Node> openList;
    vector<Node> closedList;

    Node firstNode;
    firstNode.cell = start_cell;
    firstNode.gCost = 0;
    firstNode.hCost = 0;
    openList.push(firstNode);

    while (!openList.empty()){
        Node nNode = openList.top();
        openList.pop();
        vector<Node> kids = expand_node(nNode);
        for (auto& kid : kids){
            Node ngbr;      // neighbour
            if(is_member(kid.cell, closedList)){
                ngbr = get_member(kid.cell, closedList);
            }else{
                ngbr = create_node(kid.cell);
            }
            if(distances.isCellInGrid(kid.cell.x,kid.cell.y) && distances(kid.cell.x,kid.cell.y) != 0){
                ngbr.gCost = get_gCost(nNode, ngbr.cell);
                ngbr.hCost = get_hCost(goal_cell, ngbr.cell);
                ngbr.parent = nNode;
                if (is_goal(ngbr.cell, goal_cell)){
                    return makePath(ngbr, firstNode);
                }
                if(is_member(ngbr.cell, closedList)){
                    openList.push(ngbr);
                }
            }
        }
        closedList.push_back(nNode);
    }
    



    robot_path_t path;
    path.utime = start.utime;
    path.path.push_back(start);    
    path.path_length = path.path.size();
    return path;
}

int get_gCost(Node parent, cell_t current){
    int x = abs(parent.cell.x - current.x); 
    int y = abs(parent.cell.y - current.y);
    if(x==1 & y==1)
        return parent.gCost + 14;
    else
        return parent.gCost + 10;
}

int get_hCost(cell_t goal, cell_t current){
    int x = abs(goal.x - current.x);
    int y = abs(goal.y - current.y);
    int cost;
    if (x >= y)
        cost = 14*y + 10*(x-y);
    else
        cost = 14*x + 10*(y-x);
    return cost;
}

bool is_member(const cell_t toSearch_cell, const vector<Node> givenList){
    for (auto& n : givenList){
        if (n.cell.x == toSearch_cell.x && n.cell.y == toSearch_cell.y){   
            return True;
        }
    }
    return False;
}

Node get_member(const cell_t toSearch_cell, const vector<Node> givenList){
    for (auto& n : givenList){
        if (n.cell.x == toSearch_cell.x && n.cell.y == toSearch_cell.y){   
            return n;
        }
    }
}

Node create_node(const cell_t ip_cell){
    Node n;
    n.cell = ip_cell;
    n.parent = 0;
    n.gCost = 0;
    n.hCost = 0;
}

bool is_goal(const cell_t currCell, const cell_t goal){
    if(currCell.x == goal.x && currCell.y == goal.y){
        return True;
    }
    return False;
}


vector<Node> expand_node(Node currentNode, cell_t startCell, cell_t goalCell, ObstacleDistanceGrid& grid, ){
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

robot_path_t makePath(Node node, Node start){
    Node currNode = node;
    while (currNode != start)
    {
        /* code */
    }
    
}