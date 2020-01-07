#ifndef COMMON_POSE_TRACE_HPP
#define COMMON_POSE_TRACE_HPP

#include <lcmtypes/pose_xyt_t.hpp>
#include <cstdint>
#include <vector>

/**
* PoseTrace is a time sequence of pose estimates. The trace allows for easily finding the exact pose estimate for the
* robot at a specific time. Poses are added to the trace using addPose(). The pose of the robot can then be queried
* using poseAt(). The nearest pose before and after the specified time are found and then linear interpolation is used
* to determine the estimated pose of the robot at the given time.
* 
* In general, the PoseTrace accumulates pose information forever. While you can erase poses using eraseTraceUntil, you
* shouldn't need to do that because the amount of pose information is so small relative to the memory of the system and
* the amount of time one of your programs runs.
* 
* A note about frame of reference:
* 
* PoseTrace assumes that all poses added via addPose are specified in the same reference frame. An optional transform
* can be applied to these poses to change them to a new frame of reference. However, even after setFrameTransform is
* called, it is assumed the new poses being added are in the original reference frame. You can set multiple transforms
* and they are assumed to be relative to the previous transform. Even in this case, the data is assumed to be added in
* the original reference frame. The reason for this behavior is the input poses are assumed to be coming from some
* source that can't be modified to change its reference frame, thus those poses will always have one frame which then
* has to be updated with the latest 
*/
class PoseTrace
{
public:
    
    typedef std::vector<pose_xyt_t>::const_iterator const_iterator;

    /**
    * Constructor for PoseTrace.
    */
    PoseTrace(void);

    /**
    * addPose adds a new pose measurement to the trace.
    * 
    * \param    pose            Pose to add to the trace
    */
    void addPose(const pose_xyt_t& pose);
    
    /**
    * eraseTraceUntil erases all measurements in the trace up until the time specified.
    * 
    * \param    time            Time before which all measurements should be erased
    * \return   Number of measurements erased.
    */
    int eraseTraceUntil(int64_t time);
    
    /**
    * poseAt finds the estimated pose at the specified time. The trace will find measurements with times before and
    * after the specified time. It will interpolate between the two measurements. If the time is outside the range of 
    * the trace, then it will simply return the first or last measurement and raise a warning, rather than extrapolate, 
    * which rarely works.
    * 
    * \param    time            Time at which to find the estimated pose
    * \return   Interpolated pose measurement at the specified time.
    */
    pose_xyt_t poseAt(int64_t time) const;
    
    /**
    * containsPoseAtTime checks to see if a pose at the given time will be computed using interpolation or not, i.e.
    * the requested time is in the range [front().utime, back().utime].
    * 
    * \param    time            Time to query if it is in the trace
    * \return   True if front().utime <= time <= back().utime.
    */
    bool containsPoseAtTime(int64_t time) const;
    
    /**
    * setReferencePose transforms the current reference frame of the PoseTrace. The initial pose in the PoseTrace is
    * assumed to be the same as the set reference pose. This defines a transform to get from the trace frame to the
    * reference frame. This transform is computed and then applied to all future poses added to the trace.
    * 
    * NOTE: If you call setReferencePose twice, the transform set in the second call is assumed to be relative to the
    * transform from the first call. This is unlikely to be what you want. Just set the reference pose once and 
    * leave it be.
    * 
    * IMPORTANT: Setting a frame transform is destructive because all original poses in the trace are overwritten with
    * the transformed poses. To retrieve the original poses, you need to call getFrameTransform to get the currently
    * applied transform and then call setReferencePose with the inverse of this transform, i.e. negative x,y,theta.
    * 
    * \param    initialInReferenceFrame     The initial pose of the robot in the reference coordinate frame
    */
    void setReferencePose(const pose_xyt_t& initialInReferenceFrame);

    /**
    * getFrameTransform retrieves the frame transform currently being applied to all incoming poses. This transform is
    * the result of applying each frame transform given via addFrameTransform to the previously stored frame transform.
    */
    pose_xyt_t getFrameTransform(void) const { return frameTransform_; }
    
    /**
    * clear erases all poses from the trace.
    */
    void clear(void) { trace_.clear(); }
    
    // Support for iteration and random access
    bool              empty(void)           const { return trace_.empty(); }
    std::size_t       size(void)            const { return trace_.size(); }
    const_iterator    begin(void)           const { return trace_.begin(); }
    const_iterator    end(void)             const { return trace_.end(); }
    const pose_xyt_t& operator[](int index) const { return trace_[index]; }
    const pose_xyt_t& at(int index)         const { return trace_.at(index); }
    const pose_xyt_t& front(void)           const { return trace_.front(); }
    const pose_xyt_t& back(void)            const { return trace_.back(); }

private:

    std::vector<pose_xyt_t> trace_;
    pose_xyt_t frameTransform_;
};

#endif // COMMON_POSE_TRACE_HPP
