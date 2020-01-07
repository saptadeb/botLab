#include <slam/moving_laser_scan.hpp>
#include <common/interpolation.hpp>
#include <lcmtypes/lidar_t.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <common/angle_functions.hpp>
#include <cassert>
    
MovingLaserScan::MovingLaserScan(const lidar_t& scan,
                                 const pose_xyt_t&      beginPose,
                                 const pose_xyt_t&      endPose,
                                 int                    rayStride)
{
    // Ensure a valid scan was received before processing the rays
    if(scan.num_ranges > 0)
    {
        // The stride must be at least one, or else can't iterate through the scan
        if(rayStride < 1)
        {
            rayStride = 1;
        }
        
        for(int n = 0; n < scan.num_ranges; n += rayStride)
        {
            if(scan.ranges[n] > 0.15f) //all ranges less than a robot radius are invalid
            {
                pose_xyt_t rayPose = interpolate_pose_by_time(scan.times[n], beginPose, endPose);

                adjusted_ray_t ray;

                ray.origin.x = rayPose.x;
                ray.origin.y = rayPose.y;
                ray.range    = scan.ranges[n];
                ray.theta    = wrap_to_pi(rayPose.theta - scan.thetas[n]);

                adjustedRays_.push_back(ray);
            }
        }
    }
}
