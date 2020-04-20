#include <planning/obstacle_distance_grid.hpp>
#include <slam/occupancy_grid.hpp>
#include <iostream>
#include <queue>

typedef Point<int> cell_t;

struct DistanceNode
{
    cell_t cell;            // Cell represented by this node
    float distance;        // Distance to the node -- in cells

    bool operator<(const DistanceNode& rhs) const
    {
        return rhs.distance < distance;
    }
};

void enqueue_obstacle_cells(const OccupancyGrid& map,
                            ObstacleDistanceGrid& grid,
                            std::priority_queue<DistanceNode>& searchQueue);
void expand_node(const DistanceNode& node,
                 ObstacleDistanceGrid& grid,
                 std::priority_queue<DistanceNode>& searchQueue);

bool is_cell_free_space(cell_t cell, const OccupancyGrid& map);
bool is_cell_occupied(cell_t cell, const OccupancyGrid& map);



ObstacleDistanceGrid::ObstacleDistanceGrid(void)
: width_(0)
, height_(0)
, metersPerCell_(0.05f)
, cellsPerMeter_(20.0f)
{
}

void ObstacleDistanceGrid::initializeDistances(const OccupancyGrid& map)
{
    int width  = map.widthInCells();
    int height = map.heightInCells();

    // go through all the cells
    // if is an obstacle or unknown, set to 0
    // if is free, set to -1 to indicate unknown distance

    cell_t cell;

    for(cell.y = 0; cell.y < height; ++cell.y)
    {
        for(cell.x = 0; cell.x < width; ++cell.x)
        {
            if(is_cell_free_space(cell, map))
            {
                //set free cells to -1 to indicate we have not set a distance yet
                distance(cell.x, cell.y) = -1;
            }
            else if(is_cell_occupied(cell, map))
            {
                distance(cell.x, cell.y ) = 0;
            }
            else
            {
                distance(cell.x, cell.y = 0);
            }


        }
    }
}

void ObstacleDistanceGrid::setDistances(const OccupancyGrid& map)
{
    //Ensure map and obstacle distance grid are same dimensions
    resetGrid(map);

    ///////////// TODO: Implement an algorithm to mark the distance to the nearest obstacle for every cell in the map.

    initializeDistances(map);

    std::priority_queue<DistanceNode> searchQueue;
    enqueue_obstacle_cells(map, *this, searchQueue);

    while(!searchQueue.empty())
    {
        DistanceNode nextNode = searchQueue.top();
        // std::cout << nextNode.distance << std::endl;
        searchQueue.pop();
        expand_node(nextNode, *this, searchQueue);
    }

}


bool ObstacleDistanceGrid::isCellInGrid(int x, int y) const
{
    return (x >= 0) && (x < width_) && (y >= 0) && (y < height_);
}


void ObstacleDistanceGrid::resetGrid(const OccupancyGrid& map)
{
    // Ensure the same cell sizes for both grid
    metersPerCell_ = map.metersPerCell();
    cellsPerMeter_ = map.cellsPerMeter();
    globalOrigin_ = map.originInGlobalFrame();

    // If the grid is already the correct size, nothing needs to be done
    if((width_ == map.widthInCells()) && (height_ == map.heightInCells()))
    {
        return;
    }

    // Otherwise, resize the vector that is storing the data
    width_ = map.widthInCells();
    height_ = map.heightInCells();

    cells_.resize(width_ * height_);
}

bool is_cell_free_space(cell_t cell, const OccupancyGrid& map)
{
    return map.logOdds(cell.x, cell.y) < 0;
}

bool is_cell_occupied(cell_t cell, const OccupancyGrid& map)
{
    return map.logOdds(cell.x, cell.y) >= 0;
}

void enqueue_obstacle_cells(const OccupancyGrid& map,
                            ObstacleDistanceGrid& grid,
                            std::priority_queue<DistanceNode>& searchQueue)
{
    int width  = map.widthInCells();
    int height = map.heightInCells();

    cell_t cell;

    for(cell.y = 0; cell.y < height; ++cell.y)
    {
        for(cell.x = 0; cell.x < width; ++cell.x)
        {
            if(is_cell_occupied(cell, map))
            {
                DistanceNode currentNode;
                currentNode.cell = cell;
                currentNode.distance = grid(cell.x, cell.y);
                expand_node(currentNode, grid, searchQueue);
            }
        }
    }
}

void expand_node(const DistanceNode& node,
                 ObstacleDistanceGrid& grid,
                 std::priority_queue<DistanceNode>& searchQueue)
{
    // Perform a four-way expansion of each cell
    const int xDeltas[4] = { 1, -1, 0, 0 };
    const int yDeltas[4] = { 0, 0, 1, -1 };

    for(int n = 0; n < 4; ++n)
    {
        cell_t adjacentCell(node.cell.x + xDeltas[n], node.cell.y + yDeltas[n]);

        // A cell will be enqueued if:
        // it is in the grid...
        if(grid.isCellInGrid(adjacentCell.x, adjacentCell.y))
        {
            DistanceNode adjacentNode;
            adjacentNode.cell = adjacentCell;
            if(grid(adjacentCell.x, adjacentCell.y) == -1)
            {
                adjacentNode.distance = node.distance + 0.05f;
                grid(adjacentCell.x, adjacentCell.y) = adjacentNode.distance;
                searchQueue.push(adjacentNode);
            }

        }
    }
}
