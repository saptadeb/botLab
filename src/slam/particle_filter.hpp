#ifndef SLAM_PARTICLE_FILTER_HPP
#define SLAM_PARTICLE_FILTER_HPP

#include <slam/sensor_model.hpp>
#include <slam/action_model.hpp>
#include <lcmtypes/particle_t.hpp>
#include <lcmtypes/particles_t.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <vector>

class lidar_t;
class OccupancyGrid;

/**
* ParticleFilter implements a standard SIR-based particle filter. The set of particles is initialized at some pose. Then
* on subsequent calls to updateFilter, a new pose estimate is computed using the latest odometry and laser measurements
* along with the current map of the environment.
* 
* This implementation of the particle filter uses a fixed number of particles for each iteration. Each filter update is
* a simple set of operations:
* 
*   1) Draw N particles from current set of weighted particles.
*   2) Sample an action from the ActionModel and apply it to each of these particles.
*   3) Compute a weight for each particle using the SensorModel.
*   4) Normalize the weights.
*   5) Use the max-weight or mean-weight pose as the estimated pose for this update.
*/
class ParticleFilter
{
public:
    
    /**
    * Constructor for ParticleFilter.
    *
    * \param    numParticles        Number of particles to use
    * \pre  numParticles > 1
    */
    ParticleFilter(int numParticles);
    
    /**
    * initializeFilterAtPose initializes the particle filter with the samples distributed according
    * to the provided pose estimate.
    *
    * \param    pose            Initial pose of the robot
    */
    void initializeFilterAtPose(const pose_xyt_t& pose);
    
    /**
    * updateFilter increments the state estimated by the particle filter. The filter update uses the most recent
    * odometry estimate and laser scan along with the occupancy grid map to estimate the new pose of the robot.
    *
    * \param    odometry        Calculated odometry at the time of the final ray in the laser scan
    * \param    laser           Most recent laser scan of the environment
    * \param    map             Map built from the maximum likelihood pose estimate
    * \return   Estimated robot pose.
    */
    pose_xyt_t updateFilter(const pose_xyt_t&      odometry,
                            const lidar_t& laser,
                            const OccupancyGrid&   map);/*,
                            const float v, //remove for odo model
                            const float omega,

                            const int64_t utime);*/

    /**
    * poseEstimate retrieves the current pose estimate computed by the filter.
    */
    pose_xyt_t poseEstimate(void) const;
    
    /**
    * particles retrieves the posterior set of particles being used by the algorithm.
    */
    particles_t particles(void) const;
    
private:
    
    std::vector<particle_t> posterior_;     // The posterior distribution of particles at the end of the previous update
    pose_xyt_t posteriorPose_;              // Pose estimate associated with the posterior distribution
    
    ActionModel actionModel_;   // Action model to apply to particles on each update
    SensorModel sensorModel_;   // Sensor model to compute particle weights
    
    int kNumParticles_;         // Number of particles to use for estimating the pose
    
    std::vector<particle_t> resamplePosteriorDistribution(void);
    std::vector<particle_t> computeProposalDistribution(const std::vector<particle_t>& prior);
    std::vector<particle_t> computeNormalizedPosterior(const std::vector<particle_t>& proposal,
                                                       const lidar_t& laser,
                                                       const OccupancyGrid&   map);
    pose_xyt_t estimatePosteriorPose(const std::vector<particle_t>& posterior);
};

#endif // SLAM_PARTICLE_FILTER_HPP
