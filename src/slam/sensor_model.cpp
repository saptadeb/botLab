#include <slam/sensor_model.hpp>
#include <slam/moving_laser_scan.hpp>
#include <slam/occupancy_grid.hpp>
#include <lcmtypes/particle_t.hpp>
#include <common/grid_utils.hpp>


SensorModel::SensorModel(void)
: initialized_(false)
{
    ///////// TODO: Handle any initialization needed for your sensor model
}


double SensorModel::likelihood(const particle_t& sample, const lidar_t& scan, const OccupancyGrid& map)
{
    ///////////// TODO: Implement your sensor model for calculating the likelihood of a particle given a laser scan //////////

    double scanScore = 0.0;

    // if(!initialized_){
    //     sample.parent_pose = sample.pose;
    // }
    MovingLaserScan movingScan(scan, sample.parent_pose, sample.pose);


    for(auto& ray : movingScan){
        double rayScore = scoreRay(ray,map);
        scanScore += rayScore;
    }
    return scanScore;
}


double SensorModel::scoreRay(const adjusted_ray_t& ray, const OccupancyGrid& map){
    Point<float> rayStart = global_position_to_grid_position(ray.origin, map);
    Point<int> rayEnd;
    Point<int> rayExtended;
    double fraction = 0.5;

    rayEnd.x = (ray.range * std::cos(ray.theta) * map.cellsPerMeter()) + rayStart.x;
    rayEnd.y = (ray.range * std::sin(ray.theta) * map.cellsPerMeter()) + rayStart.y;

    rayExtended.x = (2 * ray.range * std::cos(ray.theta) * map.cellsPerMeter()) + rayStart.x;
    rayExtended.y = (2 * ray.range * std::sin(ray.theta) * map.cellsPerMeter()) + rayStart.y;

    //one iteration breshenham on float of endpoint
    // printf("\nend cell %d %d ----- ", rayEnd.x, rayEnd.y);
    double odds = map.logOdds(static_cast<int>(rayEnd.x), static_cast<int>(rayEnd.y));

    if (odds > 0) { // only > 0
        // return odds;  // no adding
    }
    else{
        odds = 0;
        // printf("going in else function");
        int o1 = getCellodds(rayEnd.x, rayEnd.y, rayStart.x, rayStart.y, map);
        // printf("cell up odds %d ----- ", o1);
        int o2 = getCellodds(rayEnd.x, rayEnd.y, rayExtended.x, rayExtended.y, map);
        // printf("cell down odds %d ----- ", o2);


        if (o1 > 0) {
            odds += fraction * o1;
        }
        else if (o2 > 0){
            odds += fraction * o2;
        }

        // return double instead of an integer
    }
    // printf("cell odds parsed %f -----", odds);

    return odds;

}

int SensorModel::getCellodds(int x1, int y1, int x2, int y2, const OccupancyGrid& map)
{
    int dx,dy,sx,sy,err,x,y;
    double e2;
    dx=std::abs(x2-x1);
    dy=std::abs(y2-y1);
    sx = x1<x2 ? 1 : -1;
    sy = y1<y2 ? 1 : -1;
    err = (dx) - (dy);
    x = x1;
    y = y1;
    int i = 0;
	while(i < 1){
        i ++;
        e2 = 2*err;
        if(e2 >= -dy){
            err -= dy;
            x += sx;
    	}
        if(e2 <= dx){
    		err += dx;
            y += sy;
    	}
	}
    // printf("\ncell %d %d -----", x, y);
    return map.logOdds(x,y);
}
