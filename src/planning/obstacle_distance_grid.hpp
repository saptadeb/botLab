#ifndef PLANNING_OBSTACLE_DISTANCE_GRID_HPP
#define PLANNING_OBSTACLE_DISTANCE_GRID_HPP

#include <common/point.hpp>
#include <vector>

class OccupancyGrid;


/**
* ObstacleDistanceGrid stores the distance to the nearest obstacle for each cell in the occupancy grid.
* 
*  - An obstacle is any cell with logOdds > 0.
*  - The size of the grid is identical to the occupancy grid whose obstacle distances the distance grid stores.
* 
* To update the grid, simply pass an OccupancyGrid to the setDistances method.
*/
class ObstacleDistanceGrid
{
public:
    
    /**
    * Default constructor for ObstacleDistanceGrid.
    * 
    * Create an ObstacleDistanceGrid with a width and height of 0. metersPerCell is set to 0.05.
    * The global origin is (0,0).
    */
    ObstacleDistanceGrid(void);
    
    // Accessors for the properties of the grid
    int   widthInCells (void) const { return width_; }
    float widthInMeters(void) const { return width_ * metersPerCell_; }
    
    int   heightInCells (void) const { return height_; }
    float heightInMeters(void) const { return height_ * metersPerCell_; }
    
    float metersPerCell(void) const { return metersPerCell_; }
    float cellsPerMeter(void) const { return cellsPerMeter_; }
    
    Point<float> originInGlobalFrame(void) const { return globalOrigin_; }
    
    /**
    * setDistances sets the obstacle distances stored in the grid based on the provided occupancy grid map of the
    * environment.
    */
    void setDistances(const OccupancyGrid& map);
    
    /**
    * isCellInGrid checks to see if the specified cell is within the boundary of the ObstacleDistanceGrid.
    * 
    * This test is equivalent to:
    * 
    *   (0 <= x < widthInCells) && (0 <= y < heightInCells)
    * 
    * \param    x           x-coordinate of the cell
    * \param    y           y-coordinate of the cell
    * \return   True if the cell is in the grid boundary as specified above.
    */
    bool isCellInGrid(int x, int y) const;
    
    /**
    * operator() provides unchecked access to the cell located at (x,y). If the cell isn't contained in the grid,
    * expect fireworks or a slow, ponderous death.
    * 
    * \param    x           x-coordinate of the cell
    * \param    y           y-coordinate of the cell
    * \return   The distance to the nearest obstacle to cell (x, y).
    */
    float operator()(int x, int y) const { return cells_[cellIndex(x, y)]; }
    float& operator()(int x, int y) { return cells_[cellIndex(x, y)]; }
    
private:
    
    std::vector<float> cells_;          ///< The actual grid -- stored in row-major order
    
    int width_;                 ///< Width of the grid in cells
    int height_;                ///< Height of the grid in cells
    float metersPerCell_;       ///< Side length of a cell
    float cellsPerMeter_;       ///< Number of cells in a meter
    
    Point<float> globalOrigin_;         ///< Origin of the grid in global coordinates

    void resetGrid(const OccupancyGrid& map);
    
    // Convert between cells and the underlying vector index
    int cellIndex(int x, int y) const { return y*width_ + x; }
    
    // Allow private write-access to cells
    float& distance(int x, int y) { return cells_[cellIndex(x, y)]; }
};

#endif // PLANNING_OBSTACLE_DISTANCE_GRID_HPP
