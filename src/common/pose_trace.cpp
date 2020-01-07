#include <common/pose_trace.hpp>
#include <common/interpolation.hpp>
#include <algorithm>
#include <iostream>
#include <cassert>


pose_xyt_t apply_frame_transform(const pose_xyt_t& pose, const pose_xyt_t& transform);


PoseTrace::PoseTrace(void)
{
    frameTransform_.x = 0.0f;
    frameTransform_.y = 0.0f;
    frameTransform_.theta = 0.0f;
}

    
void PoseTrace::addPose(const pose_xyt_t& pose)
{
    trace_.push_back(apply_frame_transform(pose, frameTransform_));
}
    

int PoseTrace::eraseTraceUntil(int64_t time)
{
    auto startRemovedIt = std::remove_if(trace_.begin(), trace_.end(), [time](const pose_xyt_t& pose) {
        return pose.utime < time;
    });
    
    int numRemoved = std::distance(startRemovedIt, trace_.end());
    trace_.erase(startRemovedIt, trace_.end());
    
    return numRemoved;
}


pose_xyt_t PoseTrace::poseAt(int64_t time) const
{
    if(trace_.empty())
    {
        std::cerr << "ERROR: PoseTrace::poseAt: No odometry measurements to interpolate.\n";
        return pose_xyt_t();
    }
    else if(time < trace_.front().utime)
    {
        std::cerr << "ERROR: PoseTrace::poseAt: No odometry measurements before " << time << " Closest time:" << trace_.front().utime << " Returning that pose.\n";
        return trace_.front();
    }
    else if(time > trace_.back().utime)
    {
        std::cerr << "ERROR: PoseTrace::poseAt: No odometry measurements after " << time << " Closest time:" << trace_.back().utime << " Returning that pose.\n";
        return trace_.back();
    }
    
    pose_xyt_t interpolated;
    
    for(std::size_t i = 1; i < trace_.size(); ++i)
    {
        if((trace_[i-1].utime <= time) && (time <= trace_[i].utime))
        {
            interpolated = interpolate_pose_by_time(time, trace_[i-1], trace_[i]);
            break;
        }
    }

    assert(interpolated.utime == time);

    return interpolated;
}


bool PoseTrace::containsPoseAtTime(int64_t time) const
{
    if(trace_.empty())
    {
        return false;
    }
    
    return (front().utime <= time) && (time <= back().utime);
}


void PoseTrace::setReferencePose(const pose_xyt_t& initialInReferenceFrame)
{
    pose_xyt_t initialPose;
    
    if(trace_.empty())
    {
        std::cerr << "WARNING: Initial frame transform expects at least one pose in the trace to establish the correct "
            << " coordinate transform for the initial pose.\n";
        initialPose.x = 0.0f;
        initialPose.y = 0.0f;
        initialPose.theta = 0.0f;
    }
    else 
    {
        initialPose.x = trace_.front().x;
        initialPose.y = trace_.front().y;
        initialPose.theta = trace_.front().theta;
    }
    
    double deltaTheta = initialInReferenceFrame.theta - initialPose.theta;
    double xRotated = initialPose.x * std::cos(deltaTheta) - initialPose.y * std::sin(deltaTheta);
    double yRotated = initialPose.x * std::sin(deltaTheta) + initialPose.y * std::cos(deltaTheta);
    
    frameTransform_.x = initialInReferenceFrame.x - xRotated;
    frameTransform_.y = initialInReferenceFrame.y - yRotated;
    frameTransform_.theta = deltaTheta;
    
    for(auto& p : trace_)
    {
        p = apply_frame_transform(p, frameTransform_);
    }
}


pose_xyt_t apply_frame_transform(const pose_xyt_t& pose, const pose_xyt_t& transform)
{
    pose_xyt_t newPose;
    newPose.utime = pose.utime;
    
    newPose.x = (pose.x * std::cos(transform.theta) - pose.y * std::sin(transform.theta)) + transform.x;
    newPose.y = (pose.x * std::sin(transform.theta) + pose.y * std::cos(transform.theta)) + transform.y;
    newPose.theta = wrap_to_pi(pose.theta + transform.theta);
    
    return newPose;
}
