#include <slam/mapping.hpp>
#include <slam/moving_laser_scan.hpp>
#include <slam/occupancy_grid.hpp>
#include <common/grid_utils.hpp>
#include <numeric>


Mapping::Mapping(float maxLaserDistance, int8_t hitOdds, int8_t missOdds)
: kMaxLaserDistance_(maxLaserDistance)
, kHitOdds_(hitOdds)
, kMissOdds_(missOdds)
, initialized_(false)
{
}


void Mapping::updateMap(const lidar_t& scan, const pose_xyt_t& pose, OccupancyGrid& map)
{
    //////////////// TODO: Implement your occupancy grid algorithm here ///////////////////////
    if(!initialized_)
    {
        previousPose_ = pose;
    }

    MovingLaserScan movingScan(scan, previousPose_, pose);

// mapping boundaries
    for(auto& ray : movingScan)
    {
        scoreEndpoint(ray, map);
    }

// mapping the empty region
    for(auto& ray: movingScan)
    {
      scoreRay(ray,map);
    }


    initialized_ = true;
    previousPose_ = pose;

}

void Mapping::scoreEndpoint(const adjusted_ray_t& ray, OccupancyGrid& map){
  if(ray.range <= kMaxLaserDistance_)
  {
    Point<float> rayStart = global_position_to_grid_position(ray.origin, map);
    Point<int> rayCell;

    rayCell.x = static_cast<int>((ray.range * std::cos(ray.theta) * map.cellsPerMeter()) + rayStart.x);
    rayCell.y = static_cast<int>((ray.range * std::sin(ray.theta) * map.cellsPerMeter()) + rayStart.y);

    // increaseCellOdds(rayCell.x,rayCell.y,map);

    if(map.isCellInGrid(rayCell.x,rayCell.y)){
      increaseCellOdds(rayCell.x,rayCell.y,map);
    }
  }
}

void Mapping::scoreRay(const adjusted_ray_t& ray, OccupancyGrid& map){
  if(ray.range <= kMaxLaserDistance_)
  {
    Point<float> rayStart = global_position_to_grid_position(ray.origin, map);
    Point<int> rayCell;

    rayCell.x = static_cast<int>((ray.range * std::cos(ray.theta) * map.cellsPerMeter()) + rayStart.x);
    rayCell.y = static_cast<int>((ray.range * std::sin(ray.theta) * map.cellsPerMeter()) + rayStart.y);

    bresenham(rayStart.x,rayStart.y,rayCell.x,rayCell.y,map);

    // if(map.isCellInGrid(rayCell.x,rayCell.y)){
    //   bresenham(rayStart.x,rayStart.y,rayCell.x,rayCell.y,map);
    // }
  }
}

void Mapping::increaseCellOdds(int x, int y, OccupancyGrid& map){
  if(!initialized_){
    //do nothing
  }

  else if(std::numeric_limits<CellOdds>::max() - map(x,y) > kHitOdds_){
    map(x,y) += kHitOdds_;
  }

  else{
    map(x,y) = std::numeric_limits<CellOdds>::max();
  }
}

void Mapping::bresenham(int x1,int y1,int x2,int y2,OccupancyGrid& map)
{
	int dx,dy,sx,sy,err,x,y;
  float e2;
	dx=std::abs(x2-x1);
	dy=std::abs(y2-y1);
  sx = x1<x2 ? 1 : -1;
  sy = y1<y2 ? 1 : -1;
	err = (dx) - (dy);
  x = x1;
  y = y1;

	while(x != x2 || y != y2)
	{
    if(map.isCellInGrid(x,y)){
      decreaseCellOdds(x,y,map);
    }
    e2 = 2*err;
    if(e2 >= -dy)
		{
			err -= dy;
      x += sx;
		}
		if(e2 <= dx)
		{
			err += dx;
      y += sy;
		}
	}
}

void Mapping::decreaseCellOdds(int x, int y, OccupancyGrid& map){
  if(!initialized_){
    //do nothing
  }

  else if(map(x,y) - kMissOdds_ > std::numeric_limits<CellOdds>::min()){
    map(x,y) -= kMissOdds_;
  }

  else{
    map(x,y) = std::numeric_limits<CellOdds>::min();
  }
}
