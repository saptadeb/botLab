#ifndef SLAM_OCCUPANCY_GRID_SLAM_HPP
#define SLAM_OCCUPANCY_GRID_SLAM_HPP

#include <lcmtypes/lidar_t.hpp>
#include <lcmtypes/odometry_t.hpp>
#include <lcmtypes/pose_xyt_t.hpp>
#include <slam/particle_filter.hpp>
#include <slam/mapping.hpp>
#include <common/pose_trace.hpp>
#include <common/lcm_config.h>
#include <slam/occupancy_grid.hpp>
#include <lcm/lcm-cpp.hpp>
#include <deque>
#include <mutex>

/**
* OccupancyGridSLAM runs on a thread and handles mapping.
* 
* LCM messages are assumed to be arriving asynchronously from the runSLAM thread. Synchronization
* between the two threads is handled internally.
*/
class OccupancyGridSLAM
{
public:
    
    /**
    * Constructor for OccupancyGridSLAM.
    * 
    * \param    numParticles        Number of particles to use in the filter
    * \param    hitOddsIncrease     Amount to increase odds when laser hits a cell
    * \param    missOddsDecrease    Amount to decrease odds when laser passes through a cell
    * \param    lcmComm             LCM instance for establishing subscriptions
    * \param    waitForOptitrack    Don't start performing SLAM until a message establishing the reference frame arrives from the Optitrack
    * \param    mappingOnlyMode     Flag indicating if poses are going to be arriving from elsewhere, so just update the mapping (optional, default = false, don't run mapping-only mode)
    * \param    localizationOnlyMap Name of the map to load for localization-only mode (optional, default = "", don't use localization-only mode)
    * \pre mappingOnly or localizationOnly are mutually exclusive. If mappingOnlyMode == true, then localizationOnlyMap.empty()
    *   and if !localizationOnlyMap.empty(), then mappingOnlyMode == false. They can both be empty/false for full SLAM mode.
    */
    OccupancyGridSLAM(int numParticles, 
                      int8_t hitOddsIncrease, 
                      int8_t missOddsDecrease, 
                      lcm::LCM& lcmComm, 
                      bool waitForOptitrack,
                      bool mappingOnlyMode = false,
                      const std::string localizationOnlyMap = std::string(""));
    
    /**
    * runSLAM enters an infinite loop where SLAM will keep running as long as data is arriving.
    * It will sit and block forever if no data is incoming.
    * 
    * This method should be launched on its own thread.
    */
    void runSLAM(void);
    
    
    // Handlers for LCM messages
    void handleLaser   (const lcm::ReceiveBuffer* rbuf, const std::string& channel, const lidar_t* scan);
    void handleOdometry(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const odometry_t* odometry);
    void handlePose    (const lcm::ReceiveBuffer* rbuf, const std::string& channel, const pose_xyt_t* pose);
    void handleOptitrack(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const pose_xyt_t* pose);

private:
    
    enum Mode
    {
        mapping_only,
        localization_only,
        full_slam,
    };
    
    // State variables for controlling progress of the algorithm
    Mode mode_;     // which mode is currently being used?
    bool haveInitializedPoses_;
    bool waitingForOptitrack_;
    bool haveMap_;
    int  numIgnoredScans_;
    
    // Data from LCM
    std::deque<lidar_t> incomingScans_;
    PoseTrace groundTruthPoses_;
    PoseTrace odometryPoses_;
    
    // Data being used for current SLAM iteration
    lidar_t currentScan_;
    pose_xyt_t      currentOdometry_;
    
    pose_xyt_t initialPose_;
    pose_xyt_t previousPose_;
    pose_xyt_t currentPose_;
    
    ParticleFilter filter_;
    OccupancyGrid map_;
    Mapping mapper_;
    
    lcm::LCM& lcm_;
    int mapUpdateCount_;  // count so we only send the map occasionally, as it takes lots of bandwidth
    
    std::mutex dataMutex_;

    bool isReadyToUpdate      (void);
    void runSLAMIteration     (void);
    void copyDataForSLAMUpdate(void);
    void initializePosesIfNeeded(void);
    void updateLocalization   (void);
    void updateMap            (void);
};

#endif // SLAM_OCCUPANCY_GRID_SLAM_HPP
