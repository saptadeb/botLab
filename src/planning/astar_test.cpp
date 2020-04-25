#include <common/grid_utils.hpp>
#include <common/timestamp.h>
#include <planning/motion_planner.hpp>
#include <slam/occupancy_grid.hpp>
#include <lcm/lcm-cpp.hpp>
#include <common/getopt.h>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <chrono>
#include <thread>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>


// LCM DEFS TO SEND DATA TO A BOTGUI 
#define MULTICAST_URL "udpm://239.255.76.67:7667?ttl=2"
#define MAP_2GUI_CHANNEL "SLAM_MAP"
#define PATH_CHANNEL "CONTROLLER_PATH"

// ARGUMENT VARIABLES
bool useGui;                // Global Variable for using GUI 
bool animatePath;
int repeatTimes;            // Global Variable that sets number of repeat times, obtained from 
int pauseTime;           // Time to wait between executions of different cases
int selected_test;

// Setup Lcm
lcm::LCM lcmConnection(MULTICAST_URL); 

/*
* timing_info_t stores the timing information for the tests that are run.
*/
typedef std::map<std::string, std::vector<int64_t>> timing_info_t;
timing_info_t gSuccess;     // Time to successfully find paths  HACK -- don't put global variables in your own code!
timing_info_t gFail;        // Time to find failures  HACK -- don't put global variables in your own code!


bool test_empty_grid(void);
bool test_filled_grid(void);
bool test_narrow_constriction_grid(void);
bool test_wide_constriction_grid(void);
bool test_convex_grid(void);
bool test_maze_grid(void);
bool test_saved_poses(const std::string& mapFile, const std::string& posesFile, const std::string& testName);

robot_path_t timed_find_path(const pose_xyt_t& start, 
                             const pose_xyt_t& end, 
                             const MotionPlanner& planner, 
                             const std::string& testName);

bool is_valid_path(const robot_path_t& path, double robotRadius, const OccupancyGrid& map);
bool is_safe_cell(int x, int y, double robotRadius, const OccupancyGrid& map);

std::ostream& operator<<(std::ostream& out, const pose_xyt_t& pose);

void print_timing_info(timing_info_t& info);


int main(int argc, char** argv)
{

    const char* numRepeatsArg = "num-repeats";
    const char* useGuiArg = "use-gui";
    const char* pauseTimeArg = "pause-time";
    const char* animatePathArg = "animate-path";
    const char* testSelectArg = "test-num";
    
    // Handle Options
    getopt_t *gopt = getopt_create();
    getopt_add_bool(gopt, 'h', "help", 0, "Show this help"); 
    getopt_add_bool(gopt, '\0', useGuiArg, 0, "Flag to send lcm messages with pose and grid data. Have ./botgui running to display them.");
    getopt_add_bool(gopt, '\0', animatePathArg, 1, "Flag to animate the path formation in the order that is_valid_cell follows");
    getopt_add_int(gopt, '\0', numRepeatsArg, "1", "Number of times to repeat the A* .");
    getopt_add_int(gopt, '\0', pauseTimeArg, "2", "Time [s] to pause the A* test for each tested pair of start and goal in each map");
    getopt_add_int(gopt, '\0', testSelectArg, "6", "[0-6] Number corresponding of case to test. Leave at 6 to test all cases");

    // PRINT HELP IF FAILED TO PARSE STRING, OR IF SENT --help ARGUMENT
    if (!getopt_parse(gopt, argc, argv, 1)  || getopt_get_bool(gopt, "help")) {
        printf("Usage: %s [options]", argv[0]);
        getopt_do_usage(gopt);
        printf("\nMake sure you provide the required Gui flag\n");
        return 1;
    }

    useGui = getopt_get_bool(gopt, useGuiArg);
    animatePath = getopt_get_bool(gopt, animatePathArg);    
    repeatTimes = getopt_get_int(gopt, numRepeatsArg);
    pauseTime = getopt_get_int(gopt, pauseTimeArg);
    selected_test = getopt_get_int(gopt, testSelectArg);

    printf("\n%s",std::string(70,'=').c_str());
    printf("\nTesting your A* with the following settings :\n");
    printf("Displaying to a botlab gui : %s\n", useGui ? "true" : "false");
    printf("Displaying path animation : %s\n", animatePath ? "true" : "false");
    printf("Number of repeats : %d\n", repeatTimes);
    printf("Pause time in [s] : %d\n", pauseTime);
    printf("Working on test case : %d\n", selected_test);
    printf("Call the binary with --help argument passed for options\n");
    printf("%s\n",std::string(70,'=').c_str());
    // printf("="*20);

    if(useGui)printf("\n ~~ MAKE SURE YOU ZOOM-OUT IN YOUR GUI TO SEE THE MAP ~~\n");
    typedef bool (*test_func) (void);

    std::vector<test_func> tests = { test_empty_grid,
                                                 test_filled_grid,
                                                 test_narrow_constriction_grid,
                                                 test_wide_constriction_grid,
                                                 test_convex_grid,
                                                 test_maze_grid};
    std::vector<test_func> selected_func_vec;
    if(selected_test != 6)
    {

        selected_func_vec = {tests[selected_test]};
    }     
    else
    {
        selected_func_vec = tests;
    }
    
    std::size_t numPassed = 0;
    for(auto& t : selected_func_vec)
    {
        if(t())
        {
            ++numPassed;
        }
    }
    
    std::cout << "\nTiming information for successful planning attempts:\n";
    print_timing_info(gSuccess);
    
    std::cout << "\nTiming information for failed planning attempts:\n";
    print_timing_info(gFail);
    
    if(numPassed != selected_func_vec.size())
    {
        std::cout << "\n\nINCOMPLETE: Passed " << numPassed << " of " << selected_func_vec.size() 
        << " tests. Keep debugging and testing!\n";
    }
    else
    {
        std::cout << "\n\nCOMPLETE! All " << selected_func_vec.size() << " were passed! Good job!\n";
    }
    
    return 0;
}


bool test_empty_grid(void)
{
    return test_saved_poses("../data/astar/empty.map", "../data/astar/empty_poses.txt", __FUNCTION__);
}


bool test_filled_grid(void)
{
    return test_saved_poses("../data/astar/filled.map", "../data/astar/filled_poses.txt", __FUNCTION__);
}


bool test_narrow_constriction_grid(void)
{
    return test_saved_poses("../data/astar/narrow.map", "../data/astar/narrow_poses.txt", __FUNCTION__);
}


bool test_wide_constriction_grid(void)
{
    return test_saved_poses("../data/astar/wide.map", "../data/astar/wide_poses.txt", __FUNCTION__);
}


bool test_convex_grid(void)
{
    return test_saved_poses("../data/astar/convex.map", "../data/astar/convex_poses.txt", __FUNCTION__);
}


bool test_maze_grid(void)
{
    return test_saved_poses("../data/astar/maze.map", "../data/astar/maze_poses.txt", __FUNCTION__);
}


bool test_saved_poses(const std::string& mapFile, const std::string& posesFile, const std::string& testName)
{
    std::cout << "\nSTARTING: " << testName << '\n';
    
    OccupancyGrid grid;
    grid.loadFromFile(mapFile);

    // SEND MAP OVER LCM IF FLAG IS SENT IN
    if(useGui)
    {
        auto mapMessage = grid.toLCM();
        lcmConnection.publish(MAP_2GUI_CHANNEL, &mapMessage);
    }

    std::ifstream poseIn(posesFile);
    if(!poseIn.is_open())
    {
        std::cerr << "ERROR: No maze poses located in " << posesFile 
            << " Please run astar_test directly from the bin/ directory.\n";
        exit(-1);
    }
    
    int numGoals;
    poseIn >> numGoals;
    
    pose_xyt_t start;
    pose_xyt_t goal;
    start.theta = 0.0;
    goal.theta = 0.0;
    bool shouldExist;
    
    MotionPlannerParams plannerParams;
    plannerParams.robotRadius = 0.1;
    
    MotionPlanner planner(plannerParams);
    planner.setMap(grid);
    
    int numCorrect = 0;
    
    for(int n = 0; n < numGoals; ++n)
    {
        poseIn >> start.x >> start.y >> goal.x >> goal.y >> shouldExist;
        
        robot_path_t path = timed_find_path(start, goal, planner, testName);
        if(!animatePath && useGui) lcmConnection.publish(PATH_CHANNEL, &path); // Immediately print out path if no animation flag is sent in
        // See if the generated path was valid
        bool foundPath = path.path_length > 1;
        // The goal must be the same position as the end of the path if there was success
        if(!path.path.empty())
        {
            auto goalCell = global_position_to_grid_cell(Point<float>(goal.x, goal.y), grid);
            auto endCell = global_position_to_grid_cell(Point<float>(path.path.back().x, path.path.back().y), grid);
            foundPath &= goalCell == endCell;
        }
        
        if(foundPath)
        {
            if(shouldExist && is_valid_path(path, plannerParams.robotRadius, grid))
            {
                std::cout << "Correctly found path between start and goal: " << start << " -> " << goal << "\n";
                ++numCorrect;
            }
            else if(!shouldExist && is_valid_path(path, plannerParams.robotRadius, grid))
            {
                std::cout << "Incorrectly found valid path between start and goal: " << start << " -> " << goal << "\n";
            }
            else if(shouldExist && !is_valid_path(path, plannerParams.robotRadius, grid))
            {
                std::cout << "Incorrectly found unsafe path between start and goal: " << start << " -> " << goal 
                    << " Too close to obstacle!\n";
            }
            else
            {
                std::cout << "Incorrectly found unsafe path between start and goal: " << start << " -> " << goal << "\n";
            }
        }
        else
        {
            if(shouldExist)
            {
                std::cout << "Incorrectly found no path between start and goal: " << start << " -> " << goal << "\n";
            }
            else
            {
                std::cout << "Correctly found no path between start and goal: " << start << " -> " << goal << "\n";
                ++numCorrect;
            }
            if(useGui) lcmConnection.publish(PATH_CHANNEL, &path); 
        }

        // THIS IS WHERE WE PAUSE
        std::chrono::seconds sleep_duration(pauseTime);
        std::this_thread::sleep_for(sleep_duration);
    }
    
    if(numCorrect == numGoals)
    {
        std::cout << "PASSED! " << testName << '\n';
    }
    else
    {
        std::cout << "FAILED! " << testName << '\n';
    }
    
    return numCorrect == numGoals;
}


robot_path_t timed_find_path(const pose_xyt_t& start, 
                             const pose_xyt_t& end, 
                             const MotionPlanner& planner, 
                             const std::string& testName)
{
    // Perform each search many times to get better timing information
    robot_path_t path;
    for(int n = 0; n < repeatTimes; ++n)
    {
        int64_t startTime = utime_now();
        path = planner.planPath(start, end);
        int64_t endTime = utime_now();
        
        if(path.path_length > 1)
        {
            gSuccess[testName].push_back(endTime - startTime);
        }
        else
        {
            gFail[testName].push_back(endTime - startTime);
        }
    }

    

    return path;
}


bool is_valid_path(const robot_path_t& path, double robotRadius, const OccupancyGrid& map)
{
    // If there's only a single entry, then it isn't a valid path
    if(path.path_length < 2)
    {
        return false;
    }
    
    robot_path_t printpath;
    printpath.path_length=1;

    // Look at each position in the path, along with any intermediate points between the positions to make sure they are
    // far enough from walls in the occupancy grid to be safe
    std::chrono::milliseconds sleep_duration(200);
    for(auto p : path.path)
    {   
        // Displaying animated path while looping over its cells that are being checked
        auto cell = global_position_to_grid_cell(Point<float>(p.x, p.y), map);
        if(!is_safe_cell(cell.x, cell.y, robotRadius, map))
        {
            return false;
        }
        if(animatePath && useGui) 
        {
            printpath.path.push_back(p);
            printpath.path_length = printpath.path.size();
            lcmConnection.publish(PATH_CHANNEL, &printpath);
            std::this_thread::sleep_for(sleep_duration);
        }

    }
    
    return true;
}


bool is_safe_cell(int x, int y, double robotRadius, const OccupancyGrid& map)
{
    // Search a circular region around (x, y). If any of the cells within the robot radius are occupied, then the
    // cell isn't safe.
    const int kSafeCellRadius = std::lrint(std::ceil(robotRadius * map.cellsPerMeter()));
    
    for(int dy = -kSafeCellRadius; dy <= kSafeCellRadius; ++dy)
    {
        for(int dx = -kSafeCellRadius; dx <= kSafeCellRadius; ++dx)
        {
            // Ignore the corners of the square region, where outside the radius of the robot
            if(std::sqrt(dx*dx + dy*dy) * map.metersPerCell() > robotRadius)
            {
                continue;
            }
            
            // If the odds at the cells are greater than 0, then there's a collision, so the cell isn't safe
            if(map.logOdds(x + dx, y + dy) > 0)
            {
                return false;
            }
        }
    }
    
    // The area around the cell is free of obstacles, so all is well
    return true;
}


std::ostream& operator<<(std::ostream& out, const pose_xyt_t& pose)
{
    out << '(' << pose.x << ',' << pose.y << ',' << pose.theta << ')';
    return out;
}


void print_timing_info(timing_info_t& info)
{
    using namespace boost::accumulators;
    typedef accumulator_set<double, stats<tag::mean, tag::variance, tag::median, tag::max, tag::min>> TimingAcc;
    
    for(auto& times : info)
    {
        assert(!times.second.empty());
        
        TimingAcc acc;
        std::for_each(times.second.begin(), times.second.end(), std::ref(acc));
        
        // std::cout << times.first << " :: (us)\n"
        //     << "\tMin :    " << min(acc) << '\n'
        //     << "\tMean:    " << mean(acc) << '\n'
        //     << "\tMax:     " << max(acc) << '\n'
        //     << "\tMedian:  " << median(acc) << '\n'
        //     << "\tStd dev: " << std::sqrt(variance(acc)) << '\n'; 
    }
}
