#include <planning/astar.hpp>
#include <planning/obstacle_distance_grid.hpp>
#include <common/grid_utils.hpp>
#include <iostream>
#include <queue>
#include <vector>
#include <stack>

robot_path_t search_for_path(pose_xyt_t start, 
                             pose_xyt_t goal, 
                             const ObstacleDistanceGrid& distances,
                             const SearchParams& params)
{  
    printf("\nin global Start x: %f, y: %f --- Goal x: %f,y: %f\n",start.x,start.y,goal.x,goal.y);
    // initializing open and closed list
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> openList;
    std::vector<Node> closedList;

    robot_path_t path;
    path.utime = start.utime;
    path.path.push_back(start);  
    
    Point<double> goalPoint;
    goalPoint.x = goal.x;
    goalPoint.y = goal.y;
    cell_t end = global_position_to_grid_cell(goalPoint, distances);


    Point<double> startPoint;
    startPoint.x = start.x;
    startPoint.y = start.y;
    double initialtheta = start.theta;
    cell_t startCell = global_position_to_grid_cell(startPoint, distances);

    printf("in cell Start x: %d, y: %d --- Goal x: %d,y: %d\n",startCell.x,startCell.y,end.x,end.y);


    // check conditions!!
    // VALID GOAL
    if (!isValid(end, distances, params.minDistanceToObstacle)){
		printf("Destination is cannot be reached\n");
		path.path_length = path.path.size();
		return path;
    }  
    // VALID HOME	
	if (!isValid(startCell, distances, params.minDistanceToObstacle)){
		printf("Origin is invalid\n");
		path.path_length = path.path.size();
		return path;
    } 
    // If Home == Goal
	if(isDestination(startCell, end)){
        printf("Already at Destination\n");
		path.path_length = path.path.size();
        return path;
    }
	
	if(!distances.isCellInGrid(end.x, end.y) || !distances.isCellInGrid(startCell.x, startCell.y)){
		printf("Start or Destination not in grid %f\n");
		path.path_length = path.path.size();
        return path;
	}


    Node firstNode;
    firstNode.cell = startCell;
    firstNode.gCost = 0;
    firstNode.hCost = 0;
    firstNode.fCost = 0;
    openList.push(firstNode);

    bool destinationFound = false;
      

    while (!openList.empty()){
        int breakcount = 0;
        Node nNode = openList.top();
        closedList.push_back(nNode);
        openList.pop();
        vector<Node> kids = expand_node(nNode, distances);
        vector<Node> templist = closedList;
        Node tempnode;
        // printf("\nClosedList \n");
        for (auto& element : templist) {
            // printf("node x: %d y: %d -- parent x: %d y: %d\n", element.cell.x, element.cell.y, element.parent.x, element.parent.y);
        }
        // printf("Node ----- x: %d, y: %d\n", nNode.cell.x, nNode.cell.y);   

        priority_queue<Node, vector<Node>, greater<Node>> tempopen = openList;
        Node opennode;
        // printf("\nOPENLIST \n");
        while (!tempopen.empty()){
			opennode = tempopen.top();
            // printf("x: %d, y: %d, fcost: %d, gcost: %d\n", opennode.cell.x, opennode.cell.y, opennode.fCost, opennode.gCost);
			tempopen.pop();
		}
        for (auto& kid : kids){
            Node ngbr;      // neighbour
            if(is_member(kid.cell, closedList)){
                ngbr = get_member(kid.cell, closedList);
            }else{
                ngbr = kid;
                ngbr.fCost = INT16_MAX;
            }
            // printf("Neighbour ----- x: %d, y: %d\n", ngbr.cell.x, ngbr.cell.y);
            if(isValid(ngbr.cell, distances, params.minDistanceToObstacle)){
                if (is_goal(ngbr.cell, end)){
                    robot_path_t usablePath;
                    usablePath.utime = start.utime;
                    usablePath.path.push_back(start);  
                    destinationFound = true;
                    // printf("Found Path \n");
                    return makePath(ngbr, firstNode, initialtheta, usablePath, distances, closedList);
                }
                
                int gNew, hNew, fNew, obstacleCost = 0;
                gNew = get_gCost(nNode, ngbr.cell);
                hNew = get_hCost(end, ngbr.cell);
                obstacleCost = get_oCost(ngbr.cell, params, distances);
                fNew = gNew + hNew + obstacleCost;

                // printf("gnew: %d, hnew: %d, fnew: %d, gCost: %d, hCost: %d, fCost: %d\n", gNew, hNew, fNew, ngbr.gCost, ngbr.hCost, ngbr.fCost);
                
                if(!is_member(ngbr.cell, closedList)){
                    if(ngbr.fCost > fNew){
                        ngbr.gCost = gNew;
                        ngbr.hCost = hNew;
                        ngbr.fCost = fNew;
                        ngbr.parent = nNode.cell;
                        openList.push(ngbr);
                    }
                } 
            }
        }
    }

}

// to check if cell is not obstacle and is in the cellgrid
bool isValid(cell_t givenCell, const ObstacleDistanceGrid& distances, const double minDist) { 
	if (distances(givenCell.x, givenCell.y) >  minDist*1.000001) {
		if (givenCell.x < 0 || givenCell.y < 0 || givenCell.x >= (distances.widthInCells()) || 
            givenCell.y >= (distances.heightInCells())) {
            return false;
        }
        return true;
    } 
    return false;
}

bool isDestination(cell_t home, cell_t goal) 
{
    if (home.x == goal.x && home.y == goal.y) {
        return true;
    }
    return false;
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

int get_oCost(cell_t cell, const SearchParams& params, const ObstacleDistanceGrid& distances){
    int obstacleCost = 0;
    if (distances(cell.x, cell.y) > params.minDistanceToObstacle && distances(cell.x, cell.y) < params.maxDistanceWithCost)
        obstacleCost = static_cast<int>(pow(params.maxDistanceWithCost - distances(cell.x, cell.y)*2000, params.distanceCostExponent));
    return obstacleCost;
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
    }
    return kids;    
}

robot_path_t makePath(Node goal, Node start, double initTheta, robot_path_t usablePath, const ObstacleDistanceGrid& distances, vector<Node> CL){
    // printf("-------MAKE PATH------");
    stack<pose_xyt_t> initPath;
    int c = 0;
    float prevX, prevY;

    Node tempnode = goal;

    while (!(tempnode.cell.x == start.cell.x && tempnode.cell.y == start.cell.y)){
        Point<double> temp;
        temp.x = tempnode.cell.x;
        temp.y = tempnode.cell.y;
        int ocost = tempnode.fCost - tempnode.gCost - tempnode.hCost;
        // printf("PATH POINT -- x: %f y: %f fcost: %d gcost: %d hcost: %d ocost: %d\n", temp.x, temp.y, tempnode.fCost, tempnode.gCost, tempnode.hCost, ocost);

        Point<double> temp2 = grid_position_to_global_position(temp, distances);
        pose_xyt_t nextPoint;
        nextPoint.x = temp2.x;
        nextPoint.y = temp2.y;
        if (c == 0){
            nextPoint.theta = initTheta;
            c++;
        }else
            nextPoint.theta = atan2(prevY - temp.y, prevX - temp.x);          
        initPath.push(nextPoint);
        prevX = temp.x;
        prevY = temp.y;
        // printf("PARENT x: %f y: %f ---- \n", node.parent.x, node.parent.y);
        tempnode = get_member(tempnode.parent, CL);
    }

    while (!initPath.empty())
    {
        pose_xyt_t top = initPath.top();
        initPath.pop();
        usablePath.path.emplace_back(top);
    }
    usablePath.path_length = usablePath.path.size();
    return usablePath;
}