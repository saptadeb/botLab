#include <slam/action_model.hpp>
#include <lcmtypes/particle_t.hpp>
#include <common/angle_functions.hpp>
#include <cassert>
#include <cmath>
#include <iostream>


ActionModel::ActionModel(void)
: k1_(0.1f)
, k2_(0.03f)
, initialized_(false)
{
    //////////////// TODO: Handle any initialization for your ActionModel /////////////////////////
    moved_ = false;
}


bool ActionModel::updateAction(const pose_xyt_t& odometry)
{
    ////////////// TODO: Implement code here to compute a new distribution of the motion of the robot ////////////////
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

    if(fabs(trans_) < 0.0001f || fabs(rot2_) < 0.0001f){
        moved_ = false;
    } else{
        moved_ = true;
    }

    // rot1Std_    = alpha1_*(rot1_*rot1_) + alpha2_*(trans_*trans_);
    // transStd_   = alpha3_*(trans_*trans_) + alpha4_*(rot1_*rot1_) + alpha4_*(rot2_*rot2_);
    // rot2Std_    = alpha1_*(rot2_*rot2_) + alpha2_*(trans_*trans_);

    rot1Std_    = k1_*fabs(rot1_);
    transStd_   = k2_*fabs(trans_);
    rot2Std_    = k1_*fabs(rot2_);

    // particle_t sample;
    // sample.pose = odometry;
    // sample.parent_pose = previousOdometry_;
    // utime_ = odometry.utime;
    // particle_t newSample = applyAction(sample);

    previousOdometry_ = odometry;

    if (moved_){
        return true;
    }
    return false;
}


particle_t ActionModel::applyAction(const particle_t& sample)
{
    ////////////// TODO: Implement your code for sampling new poses from the distribution computed in updateAction //////////////////////
    // Make sure you create a new valid particle_t. Don't forget to set the new time and new parent_pose.

    if(moved_)
    {
        particle_t newSample = sample;

        float sampledRot1 = std::normal_distribution<>(rot1_, rot1Std_)(numberGenerator_);
        float sampledTrans = std::normal_distribution<>(trans_, transStd_)(numberGenerator_);
        float sampledRot2 = std::normal_distribution<>(rot2_, rot2Std_)(numberGenerator_);

        // float e1 = std::normal_distribution<>(0, rot1Std_)(numberGenerator_);
        // float e2 = std::normal_distribution<>(0, transStd_)(numberGenerator_);
        // float e3 = std::normal_distribution<>(0, rot2Std_)(numberGenerator_);

        newSample.pose.x += sampledTrans*cos(sample.pose.theta + sampledRot1);
        newSample.pose.y += sampledTrans*sin(sample.pose.theta + sampledRot1);
        newSample.pose.theta = wrap_to_pi(sample.pose.theta + sampledRot1 + sampledRot2);

        // newSample.pose.x += (trans_+e2)*cos(sample.pose.theta + rot1_ + e1);
        // newSample.pose.y += (trans_+e2)*sin(sample.pose.theta + rot1_ + e1);
        // newSample.pose.theta = wrap_to_pi(deltaTheta_ + e1 + e3);

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
