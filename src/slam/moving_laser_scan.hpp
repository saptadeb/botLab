#ifndef SLAM_MOVING_LASER_SCAN_HPP
#define SLAM_MOVING_LASER_SCAN_HPP

#include <common/point.hpp>
#include <vector>

class lidar_t;
class pose_xyt_t;

/**
* adjusted_ray_t defines a single ray in a full laser scan. The adjusted ray contains its origin, range, and the angle
* in the global coordinate frame that it measures.
*/
struct adjusted_ray_t
{
    Point<float> origin;        ///< Position of the robot when the ray was measured
    float        range;         ///< Range of the measurement
    float        theta;         ///< Angle of the measurement in global frame, not robot frame
};

/**
* MovingLaserScan is a laser scan where each ray in the scan has a different origin pose based on the motion of the
* robot. The moving laser scan corrects for the motion of the robot which the scan was being gathered. For high-speed
* lasers, this motion is usually insignificant, but for slow lasers, like rplidar, the motion of the robot has a
* significant effect on the measurements and correcting for thismotion is essential for accurate SLAM.
*/
class MovingLaserScan
{
public:
    
    typedef std::vector<adjusted_ray_t>::const_iterator Iter;
    
    /**
    * Constructor for MovingLaserScan.
    * 
    * \param    scan        Scan taken from a moving robot
    * \param    beginPose   Pose of the robot at the start of the scan
    * \param    endPose     Pose of the robot at the end of the scan
    * \param    rayStride   Number of rays to skip in the original scan (default = 1)
    */
    MovingLaserScan(const lidar_t& scan,
                    const pose_xyt_t&      beginPose,
                    const pose_xyt_t&      endPose,
                    int                    rayStride = 1);
    
    std::size_t size(void) const { return adjustedRays_.size(); }
    Iter begin(void) const { return adjustedRays_.begin(); }
    Iter end  (void) const { return adjustedRays_.end();   }
    adjusted_ray_t at(int index) const { return adjustedRays_.at(index); }
    const adjusted_ray_t& operator[](int index) const { return adjustedRays_[index]; }

private:

    std::vector<adjusted_ray_t> adjustedRays_;
};

#endif // SLAM_MOVING_LASER_SCAN_HPP
