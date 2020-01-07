#ifndef SLAM_INTERPOLATION_HPP
#define SLAM_INTERPOLATION_HPP

#include <common/angle_functions.hpp>
#include <utility>
#include <cassert>

/**
* interpolate_pose_by_time interpolates between two poses using time. The motion is assumed to be constant, thus a linear
* interpolation is used.
* 
* Pose requires the following public member variables:
*   - utime
*   - x
*   - y
*   - theta
* 
* \param    time            Desired time for the interpolated pose
* \param    before          Pose before the time
* \param    after           Pose after the time
* \return   Interpolated pose at time.
*/
template <class Pose>
Pose interpolate_pose_by_time(int64_t time, const Pose& before, const Pose& after)
{
//     assert(before.utime <= after.utime);
//     assert(time <= after.utime);
    
    if(before.utime == after.utime)
    {
        Pose timePose  = after;
        timePose.utime = time;
        return timePose;
    }
    
    double interpolateRatio = static_cast<double>(time - before.utime) / static_cast<double>(after.utime - before.utime);
    
    // Assume a constant velocity
    double xStep     = (after.x - before.x) * interpolateRatio;
    double yStep     = (after.y - before.y) * interpolateRatio;
    double thetaStep = angle_diff(after.theta, before.theta) * interpolateRatio;
    
    Pose interpolated;
    interpolated.utime = time;
    interpolated.x     = before.x + xStep;
    interpolated.y     = before.y + yStep;
    interpolated.theta = angle_sum(before.theta, thetaStep);
    
    return interpolated;
}


/**
* interpolate_value_by_time interpolates a floating-point value between two times.
*
* \param    time        Time at which to provide the interpolated value
* \param    before      (time, value) pair before interpolate time
* \param    after       (time, value) pair after interpolate time
* \return   Interpolated value. No check is done that beforeTime < time < afterTime. If that condition doesn't hold
*           then extrapolation will be happening.
*/
inline float interpolate_value_by_time(int64_t time, std::pair<int64_t, float> before, std::pair<int64_t, float> after)
{
    int64_t beforeTime = before.first;
    int64_t afterTime  = after.first;

    if(beforeTime == afterTime)
    {
        return before.second;
    }

    double interpolateRatio = static_cast<double>(time - beforeTime) / static_cast<double>(afterTime - beforeTime);
    return before.second + ((after.second - before.second) * interpolateRatio);
}

#endif // SLAM_INTERPOLATION_HPP
