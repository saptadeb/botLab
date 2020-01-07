#include <slam/slam.hpp>
#include <common/getopt.h>
#include <lcm/lcm-cpp.hpp>
#include <thread>
#include <csignal>


int main(int argc, char** argv)
{
    const char* kNumParticlesArg = "num-particles";
    const char* kHitOddsArg = "hit-odds";
    const char* kMissOddsArg = "miss-odds";
    const char* kUseOptitrackArg = "use-optitrack";
    const char* kMappingOnlyArg = "mapping-only";
    const char* kLocalizationOnlyArg = "localization-only";
    
    // Handle Options
    getopt_t *gopt = getopt_create();
    getopt_add_bool(gopt, 'h', "help", 0, "Show this help"); 
    getopt_add_int(gopt, '\0', kNumParticlesArg, "200", "Number of particles to use in the particle filter");
    getopt_add_int(gopt, '\0', kHitOddsArg, "3", "Amount to increase log-odds when a cell is hit by a laser ray");
    getopt_add_int(gopt, '\0', kMissOddsArg, "1", "Amount to decrease log-odds when a cell is passed through by a laser ray");
    getopt_add_bool(gopt, '\0', kUseOptitrackArg, 0, "Flag indicating if the map reference frame should be set to the Optitrack reference frame.");
    getopt_add_bool(gopt, '\0', kMappingOnlyArg, 0, "Flag indicating if mapping-only mode should be run");
    getopt_add_string(gopt, '\0', kLocalizationOnlyArg, "", "Localization only mode should be run. Name of map to use is provided.");
    
    if (!getopt_parse(gopt, argc, argv, 1) || getopt_get_bool(gopt, "help")) {
        printf("Usage: %s [options]", argv[0]);
        getopt_do_usage(gopt);
        return 1;
    }
    
    int numParticles = getopt_get_int(gopt, kNumParticlesArg);
    int hitOdds = getopt_get_int(gopt, kHitOddsArg);
    int missOdds = getopt_get_int(gopt, kMissOddsArg);
    bool useOptitrack = getopt_get_bool(gopt, kUseOptitrackArg);
    bool mappingOnly = getopt_get_bool(gopt, kMappingOnlyArg);
    std::string localizationMap = getopt_get_string(gopt, kLocalizationOnlyArg);

    signal(SIGINT, exit);  
    
    lcm::LCM lcmConnection(MULTICAST_URL);

    OccupancyGridSLAM slam(numParticles, 
                           hitOdds, 
                           missOdds, 
                           lcmConnection, 
                           useOptitrack, 
                           mappingOnly, 
                           localizationMap);
    
    std::thread slamThread([&slam]() {
        slam.runSLAM();
    });
    
    while(true)
    {
        lcmConnection.handle();
    }
    
    return 0;
}
