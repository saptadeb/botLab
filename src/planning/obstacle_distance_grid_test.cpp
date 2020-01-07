#include <planning/obstacle_distance_grid.hpp>
#include <slam/occupancy_grid.hpp>
#include <iostream>

/*
* The distance grid test uses a simple square environment with free space in the center of the square. Obstacles one
* cell from the edge, and unknown cells along the edge. The test makes sure that unknown and obstacle cells are 
* distance 0 and a scattering of cells inside the free space have the correct distances.
*/


const int kGridSideLength = 25;
const int obstacleLowIndex = 1;
const int obstacleHighIndex = kGridSideLength - 2;


bool test_unknown_distances(void);
bool test_obstacle_distances(void);
bool test_free_space_distances(void);
float expected_free_distance(int x, int y, const OccupancyGrid& map);

OccupancyGrid generate_grid(void);


int main(int argc, char** argv)
{
    if(test_unknown_distances())
    {
        std::cout << "PASSED: test_unknown_distances\n";
    }
    else
    {
        std::cout << "FAILED: test_unknown_distances\n";
    }
    
    if(test_obstacle_distances())
    {
        std::cout << "PASSED: test_obstacle_distances\n";
    }
    else
    {
        std::cout << "FAILED: test_obstacle_distances\n";
    }
    
    if(test_free_space_distances())
    {
        std::cout << "PASSED: test_free_space_distances\n";
    }
    else
    {
        std::cout << "FAILED: test_free_space_distances\n";
    }
    
    return 0;
}


bool test_unknown_distances(void)
{
    OccupancyGrid grid = generate_grid();
    ObstacleDistanceGrid distances;
    distances.setDistances(grid);
    
    int numUnknownCells = 0;
    int numCorrectUnknownDistances = 0;
    
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            if(grid(x, y) == 0)
            {
                ++numUnknownCells;
                
                if(distances(x, y) == 0.0f)
                {
                    ++numCorrectUnknownDistances;
                }
            }
        }
    }
    
    std::cout << "Unknown test result: Num unknown:" << numUnknownCells << " Num correct dists:" 
        << numCorrectUnknownDistances << '\n';
        
    return numUnknownCells == numCorrectUnknownDistances;
}


bool test_obstacle_distances(void)
{
    OccupancyGrid grid = generate_grid();
    ObstacleDistanceGrid distances;
    distances.setDistances(grid);
    
    int numObstacleCells = 0;
    int numCorrectObstacleDistances = 0;
    
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            if(grid(x, y) > 0)
            {
                ++numObstacleCells;
                
                if(distances(x, y) == 0.0f)
                {
                    ++numCorrectObstacleDistances;
                }
            }
        }
    }
    
    std::cout << "Obstacle test result: Num obstacles:" << numObstacleCells << " Num correct dists:" 
        << numCorrectObstacleDistances << '\n';
        
    return numObstacleCells == numCorrectObstacleDistances;
}


bool test_free_space_distances(void)
{
    OccupancyGrid grid = generate_grid();
    ObstacleDistanceGrid distances;
    distances.setDistances(grid);
    
    int numFreeCells = 0;
    int numCorrectFreeDistances = 0;
    
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            if(grid(x, y) < 0)
            {
                ++numFreeCells;
                
                auto expectedDist = expected_free_distance(x, y, grid);
                
                if(std::abs(distances(x, y) - expectedDist) < 0.0001)
                {
                    ++numCorrectFreeDistances;
                }
                else
                {
                    std::cout << "FAILED: Expected:" << expectedDist << " Stored:" << distances(x, y) << '\n';
                }
            }
        }
    }
    
    std::cout << "Free test result: Num free cells:" << numFreeCells << " Num correct dists:" 
        << numCorrectFreeDistances << '\n';
        
    return numFreeCells == numCorrectFreeDistances;
}


float expected_free_distance(int x, int y, const OccupancyGrid& map)
{
    // Because the grid is a square, the nearest obstacle is always a horizontal or vertical wall. The expected distance
    // is always just the distance to the closest obstacle
    
    int minXDist = std::min(x - obstacleLowIndex, obstacleHighIndex - x);
    int minYDist = std::min(y - obstacleLowIndex, obstacleHighIndex - y);
    
    return std::min(minXDist, minYDist) * map.metersPerCell();
}


OccupancyGrid generate_grid(void)
{
    const float kMetersPerCell = 0.1f;
    
    OccupancyGrid grid(kGridSideLength * kMetersPerCell, kGridSideLength * kMetersPerCell, kMetersPerCell);
    
    for(int y = 0; y < grid.heightInCells(); ++y)
    {
        for(int x = 0; x < grid.widthInCells(); ++x)
        {
            if((x == obstacleLowIndex) || (x == obstacleHighIndex) 
                || (y == obstacleLowIndex) || (y == obstacleHighIndex))
            {
                grid(x, y) = 50;
            }
            else if((x > obstacleLowIndex) && (x < obstacleHighIndex) && (y > obstacleLowIndex) 
                && (y < obstacleHighIndex))
            {
                grid(x, y) = -50;
            }
        }
    }
    
    return grid;
}
