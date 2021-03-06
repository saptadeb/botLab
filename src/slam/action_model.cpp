#include <slam/action_model.hpp>
#include <lcmtypes/particle_t.hpp>
#include <common/angle_functions.hpp>
#include <cassert>
#include <cmath>
#include <iostream>


ActionModel::ActionModel(void)
: k1_(0.8f)
, k2_(0.3f)
, alpha1_(0.6f) //0.1
, alpha2_(0.07f) //0.0001
, alpha3_(0.8f) //0.03
, alpha4_(0.0002f) //0.0001
, initialized_(false)
{
    moved_ = false;
}


bool ActionModel::updateAction(const pose_xyt_t& odometry)
{
    if(!initialized_){
        previousOdometry_ = odometry;
        initialized_ = true;
    }

    float deltaX = odometry.x - previousOdometry_.x;
    float deltaY = odometry.y - previousOdometry_.y;
    float deltaTheta = angle_diff(odometry.theta, previousOdometry_.theta);
    float dir = 1.0;

    rot1_   = angle_diff(std::atan2(deltaY,deltaX), previousOdometry_.theta);
    trans_  = std::sqrt(deltaX*deltaX + deltaY*deltaY);

    if(std::abs(trans_) < 0.0001){
        rot1_ = 0.0f;
    }
    else if(std::abs(rot1_) > M_PI/2.0){
        rot1_ = -angle_diff(M_PI, rot1_);
        dir   = -1.0;
    }
    else if(std::abs(rot1_) < -M_PI/2.0){
        rot1_ = -angle_diff(-M_PI, rot1_);
        dir   = -1.0;
    }

    trans_ *= dir;
    rot2_   = angle_diff(deltaTheta, rot1_);
    // if(fabs(trans_) < 0.0005f || fabs(rot2_) < 0.0001f){
    if((fabs(trans_) + fabs(rot2_)) < 0.00001f){    //0.0001 -- tolerance can be changed for slow or fast motion
        moved_ = false;
    } else{
        moved_ = true;
    }


    // rot1Std_    = k1_ * rot1_;
    // transStd_   = k2_ * trans_;
    // rot2Std_    = k1_ * rot2_; 

    // hard coding decreases extra computation needed to calculate standard deviations
    rot1Std_    = 0.05;
    transStd_   = 0.005;
    rot2Std_    = 0.05;


    previousOdometry_ = odometry;

    if (moved_){
        return true;
    }
    return false;
}


particle_t ActionModel::applyAction(const particle_t& sample)
{
    if(moved_)
    {
        particle_t newSample = sample;

        float sampledRot1 = std::normal_distribution<>(rot1_, rot1Std_)(numberGenerator_);
        float sampledTrans = std::normal_distribution<>(trans_, transStd_)(numberGenerator_);
        float sampledRot2 = std::normal_distribution<>(rot2_, rot2Std_)(numberGenerator_);

        newSample.pose.x += sampledTrans*cos(sample.pose.theta + sampledRot1);
        newSample.pose.y += sampledTrans*sin(sample.pose.theta + sampledRot1);
        newSample.pose.theta = wrap_to_pi(sample.pose.theta + sampledRot1 + sampledRot2);

        newSample.pose.utime = utime_;
        newSample.parent_pose = sample.pose;
        return newSample;
    }
    else
    {
        particle_t newSample = sample;
        newSample.pose.utime = utime_;
        newSample.parent_pose = sample.pose;
        return newSample;
    }
}
