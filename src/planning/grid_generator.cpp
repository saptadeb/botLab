#include <slam/occupancy_grid.hpp>
#include <algorithm>


const float kGridWidth = 15.0f;
const float kGridHeight = 15.0f;
const float kMetersPerCell = 0.05f;

OccupancyGrid generate_uniform_grid(int8_t logOdds);
OccupancyGrid generate_constricted_grid(double openingWidth);


int main(int argc, char** argv)
{
    // Create four grids needed for astar_test:
    
    // An empty grid
    OccupancyGrid emptyGrid = generate_uniform_grid(-10);
    emptyGrid.saveToFile("../data/empty.map");
    
    // A filled grid
    OccupancyGrid filledGrid = generate_uniform_grid(10);
    filledGrid.saveToFile("../data/filled.map");
    
    // A narrow constriction that the robot can't fit through
    OccupancyGrid narrowGrid = generate_constricted_grid(0.1);
    narrowGrid.saveToFile("../data/narrow.map");
    
    // A wide constriction that the robot can fit through
    OccupancyGrid wideGrid = generate_constricted_grid(0.5);
    wideGrid.saveToFile("../data/wide.map");
    
    return 0;
}


OccupancyGrid generate_uniform_grid(int8_t logOdds)
{
    OccupancyGrid grid(kGridWidth, kGridHeight, kMetersPerCell);
    
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            grid(x, y) = logOdds;
        }
    }
    
    return grid;
}


OccupancyGrid generate_constricted_grid(double openingWidth)
{
    OccupancyGrid grid = generate_uniform_grid(-10);
    
    // Draw a line right through the middle of the grid. Have the line start after the opening.
    int openingCellY = grid.heightInCells() / 2;
    int openingWidthInCells = std::max(static_cast<int>(openingWidth * grid.cellsPerMeter()), 1);
    
    for(int x = openingWidthInCells; x < grid.widthInCells(); ++x)
    {
        grid(x, openingCellY) = 10;
    }
    
    return grid;
}
