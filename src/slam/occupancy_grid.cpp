#include <slam/occupancy_grid.hpp>
#include <fstream>
#include <cassert>

#include <iostream>
using namespace std;


OccupancyGrid::OccupancyGrid(void)
: width_(0)
, height_(0)
, metersPerCell_(0.05f)
, cellsPerMeter_(1.0 / metersPerCell_)
, globalOrigin_(0, 0)
{
}


OccupancyGrid::OccupancyGrid(float widthInMeters,
                             float heightInMeters,
                             float metersPerCell)
: metersPerCell_(metersPerCell)
, globalOrigin_(-widthInMeters/2.0f, -heightInMeters/2.0f)
{
    assert(widthInMeters  > 0.0f);
    assert(heightInMeters > 0.0f);
    assert(metersPerCell_ <= widthInMeters);
    assert(metersPerCell_ <= heightInMeters);
    
    cellsPerMeter_ = 1.0f / metersPerCell_;
    width_         = widthInMeters * cellsPerMeter_;
    height_        = heightInMeters * cellsPerMeter_;
    
    cells_.resize(width_ * height_);
    reset();
}

void OccupancyGrid::setOrigin(float x, float y){

    reset();

    globalOrigin_.x -= x;
    globalOrigin_.y -= y;

  //  cout << "set global origin to: " << x << ", " << y << "\n";
}

void OccupancyGrid::reset(void)
{
//    cout << "reset!\n";
    std::fill(cells_.begin(), cells_.end(), 0);
}


bool OccupancyGrid::isCellInGrid(int x, int y) const
{ 
    bool xCoordIsValid = (x >= 0) && (x < width_);
    bool yCoordIsValid = (y >= 0) && (y < height_);
    return xCoordIsValid && yCoordIsValid;
}


CellOdds OccupancyGrid::logOdds(int x, int y) const
{
    if(isCellInGrid(x, y))
    {
        return operator()(x, y);
    }
    
    return 0;
}


void OccupancyGrid::setLogOdds(int x, int y, CellOdds value)
{
    if(isCellInGrid(x, y))
    {
        operator()(x, y) = value;
    }
}


occupancy_grid_t OccupancyGrid::toLCM(void) const
{
    occupancy_grid_t grid;

    grid.origin_x        = globalOrigin_.x;
    grid.origin_y        = globalOrigin_.y;
    grid.meters_per_cell = metersPerCell_;
    grid.width           = width_;
    grid.height          = height_;
    grid.num_cells       = cells_.size();
    grid.cells           = cells_;
    
    return grid;
}


void OccupancyGrid::fromLCM(const occupancy_grid_t& gridMessage)
{
    globalOrigin_.x = gridMessage.origin_x;
    globalOrigin_.y = gridMessage.origin_y;
    metersPerCell_  = gridMessage.meters_per_cell;
    cellsPerMeter_  = 1.0f / gridMessage.meters_per_cell;
    height_         = gridMessage.height;
    width_          = gridMessage.width;
    cells_          = gridMessage.cells;
}


bool OccupancyGrid::saveToFile(const std::string& filename) const
{
    std::ofstream out(filename);
    if(!out.is_open())
    {
        std::cerr << "ERROR: OccupancyGrid::saveToFile: Failed to save to " << filename << '\n';
        return false;
    }
    
    // Write header
    out << globalOrigin_.x << ' ' << globalOrigin_.y << ' ' << width_ << ' ' << height_ << ' ' << metersPerCell_ << '\n';
    
    // Write out each cell value
    for(int y = 0; y < height_; ++y)
    {
        for(int x = 0; x < width_; ++x)
        {
            // Unary plus forces output to be a a number rather than a character
             out << +logOdds(x, y) << ' ';
        }
        out << '\n';
    }
    
    return out.good();
}


bool OccupancyGrid::loadFromFile(const std::string& filename)
{
    std::ifstream in(filename);
    if(!in.is_open())
    {
        std::cerr << "ERROR: OccupancyGrid::loadFromFile: Failed to load from " << filename << '\n';
        return false;
    }
    
    width_ = -1;
    height_ = -1;
    
    // Read header
    in >> globalOrigin_.x >> globalOrigin_.y >> width_ >> height_ >> metersPerCell_;
    
    // Check sanity of values
    assert(width_ > 0);
    assert(height_ > 0);
    assert(metersPerCell_ > 0.0f);
    
    // Allocate new memory for the grid
    cells_.resize(width_ * height_);
    
    // Allocate new memory for the grid
    cells_.resize(width_ * height_);
    // Read in each cell value
    int odds = 0; // read in as an int so it doesn't convert the number to the corresponding ASCII code
    for(int y = 0; y < height_; ++y)
    {
        for(int x = 0; x < width_; ++x)
        {
            in >> odds;
            setLogOdds(x, y, odds);
        }
    }
    
    return true;
}
