#include <apps/botgui/botgui.hpp>
#include <apps/utils/drawing_functions.hpp>
#include <common/grid_utils.hpp>
#include <common/timestamp.h>
#include <lcmtypes/mbot_motor_command_t.hpp>
#include <mbot/mbot_channels.h>
#include <optitrack/optitrack_channels.h>
#include <planning/planning_channels.h>
#include <planning/motion_planner.hpp>
#include <slam/slam_channels.h>
#include <vx/gtk/vx_gtk_display_source.h>
#include <vx/vx_colors.h>
#include <gdk/gdk.h>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <cassert>
#include <glib.h>
#include <unistd.h>


void clear_traces_pressed(GtkWidget* button, gpointer gui);
void reset_state_pressed(GtkWidget* button, gpointer gui);


BotGui::BotGui(lcm::LCM* lcmInstance, int argc, char** argv, int widthInPixels, int heightInPixels, int framesPerSecond)
: VxGtkWindowBase(argc, argv, widthInPixels, heightInPixels, framesPerSecond)
, haveLaser_(false)
, havePath_(false)
, haveTruePose_(false)
, shouldResetStateLabels_(false)
, shouldClearTraces_(false)
, nextColorIndex_(0)
, lcmInstance_(lcmInstance)
{
    assert(lcmInstance_);
    
    odometry_.x = odometry_.y = odometry_.theta = 0.0;
    slamPose_.x = slamPose_.y = slamPose_.theta = 0.0;
    
    laser_.num_ranges = 0;
    path_.path_length = 0;
    slamParticles_.num_particles = 0;
    
    // Copy over all the colors for drawing pose traces
    traceColors_.push_back(vx_red);
    traceColors_.push_back(vx_orange);
    traceColors_.push_back(vx_purple);
    traceColors_.push_back(vx_magenta);
    traceColors_.push_back(vx_maroon);
    traceColors_.push_back(vx_forest);
    traceColors_.push_back(vx_navy);
    traceColors_.push_back(vx_olive);
    traceColors_.push_back(vx_plum);
    traceColors_.push_back(vx_teal);
}


void BotGui::clearAllTraces(void)
{
    shouldClearTraces_ = true;
}


void BotGui::resetExplorationStates(void)
{
    shouldResetStateLabels_ = true;
}


int BotGui::onMouseEvent(vx_layer_t* layer, 
                         vx_camera_pos_t* cameraPosition, 
                         vx_mouse_event_t* event, 
                         Point<float> worldPoint)
{
    // If a Ctrl + Left-click, send a robot_path_t with a single position
    if((event->button_mask & VX_BUTTON1_MASK) && (event->modifiers & VX_CTRL_MASK))
    {
        pose_xyt_t odomPose;
        odomPose.x = odometry_.x;
        odomPose.y = odometry_.y;
        odomPose.theta = odometry_.theta;
        
        pose_xyt_t target;
        target.x = worldPoint.x;
        target.y = worldPoint.y;
        target.theta = 0.0f;
        
        // If an odometry trace exists, then we need to transform the Vx reference frame into the odometry frame
        auto odomTraceIt = traces_.find(ODOMETRY_CHANNEL);
        if(odomTraceIt != traces_.end())
        {
            auto odomToVx = odomTraceIt->second.trace.getFrameTransform();
            // Apply an inverse transform to rotate from Vx to odometry
            double xShifted = worldPoint.x - odomToVx.x;
            double yShifted = worldPoint.y - odomToVx.y;
            target.x = (xShifted * std::cos(-odomToVx.theta)) - (yShifted * std::sin(-odomToVx.theta));
            target.y = (xShifted * std::sin(-odomToVx.theta)) + (yShifted * std::cos(-odomToVx.theta));
            
            if(haveTruePose_)
            {
                std::cout << "WARNING: Optitrack data detected. The Ctrl+Click path will not be displayed correctly.\n";
            }
        }
        // Otherwise, just assume points are in the odometry frame
        
        std::cout << "Sending controller path to " << target.x << ',' << target.y << " in odometry frame\n";
        
        robot_path_t path;
        path.path_length = 2;
        path.path.push_back(odomPose);
        path.path.push_back(target);
        lcmInstance_->publish(CONTROLLER_PATH_CHANNEL, &path);
    }
    // If an Right-click, send a target to the A* planner
    else if((event->button_mask & VX_BUTTON3_MASK)) // && (event->modifiers == 0)
    {
        std::cout << "Planning path to " << worldPoint << "...";
        int64_t startTime = utime_now();
        pose_xyt_t target;
        target.x = worldPoint.x;
        target.y = worldPoint.y;
        target.theta = 0.0f;
        
        MotionPlanner planner;
        planner.setMap(map_);
        robot_path_t plannedPath = planner.planPath(slamPose_, target);
        distances_ = planner.obstacleDistances();
        lcmInstance_->publish(CONTROLLER_PATH_CHANNEL, &plannedPath);
        
        std::cout << "completed in " << ((utime_now() - startTime) / 1000) << "ms\n";
    }
    
    
        std::lock_guard<std::mutex> autoLock(vxLock_);
        mouseWorldCoord_ = worldPoint;
        mouseGridCoord_ = global_position_to_grid_cell(worldPoint, map_);
    
    return 0;
}


int BotGui::onKeyEvent(vx_layer_t* layer, vx_key_event_t* event)
{
    // If the key is released, the wheel speed goes to 0, so the robot stops immediately.
    // Otherwise, use the requested velocity, per the command slider.
/*    cmdSpeed_ = gtk_range_get_value(GTK_RANGE(cmdSlider_));
    rightTrim_ = gtk_range_get_value(GTK_RANGE(trimSlider_)) / 100.0;
    double leftSpeed = event->released ? 0.0 : cmdSpeed_;
    double rightSpeed = (1.0 + rightTrim_) * leftSpeed;

    // If one of the arrow keys is being pressed, then send the appropriate command out
    if(event->key_code == VX_KEY_UP)
    {
        // Drive forward
        mbot_motor_command_t cmd;
        cmd.left_motor_enabled = cmd.right_motor_enabled = 1;
        cmd.left_motor_speed = leftSpeed;
        cmd.right_motor_speed = rightSpeed;
        lcmInstance_->publish(MBOT_MOTOR_COMMAND_CHANNEL, &cmd);
//	std::cout << "L: " << cmd.left_motor_speed << " R: " << cmd.right_motor_speed << "\n";
    }
    else if(event->key_code == VX_KEY_DOWN)
    {
        // Drive backward
        mbot_motor_command_t cmd;
        cmd.left_motor_enabled = cmd.right_motor_enabled = 1;
        cmd.left_motor_speed = -leftSpeed;
        cmd.right_motor_speed = -rightSpeed;
        lcmInstance_->publish(MBOT_MOTOR_COMMAND_CHANNEL, &cmd);

//	std::cout << "L: " << cmd.left_motor_speed << " R: " << cmd.right_motor_speed << "\n";
    }
    else if(event->key_code == VX_KEY_LEFT)
    {
        // Turn left
        mbot_motor_command_t cmd;
        cmd.left_motor_enabled = cmd.right_motor_enabled = 1;
        cmd.left_motor_speed = -leftSpeed;
        cmd.right_motor_speed = rightSpeed;
        lcmInstance_->publish(MBOT_MOTOR_COMMAND_CHANNEL, &cmd);
    }
    else if(event->key_code == VX_KEY_RIGHT)
    {
        // Turn right
        mbot_motor_command_t cmd;
        cmd.left_motor_enabled = cmd.right_motor_enabled = 1;
        cmd.left_motor_speed = leftSpeed;
        cmd.right_motor_speed = -rightSpeed;
        lcmInstance_->publish(MBOT_MOTOR_COMMAND_CHANNEL, &cmd);
    }
    */
    return 0;
}


void BotGui::onDisplayStart(vx_display_t* display)
{
    VxGtkWindowBase::onDisplayStart(display);
    lcmInstance_->subscribe(SLAM_MAP_CHANNEL, &BotGui::handleOccupancyGrid, this);
    lcmInstance_->subscribe(SLAM_PARTICLES_CHANNEL, &BotGui::handleParticles, this);
    lcmInstance_->subscribe(CONTROLLER_PATH_CHANNEL, &BotGui::handlePath, this);
    lcmInstance_->subscribe(LIDAR_CHANNEL, &BotGui::handleLaser, this);
    lcmInstance_->subscribe(".*_POSE", &BotGui::handlePose, this);  // NOTE: Subscribe to ALL _POSE channels!
    lcmInstance_->subscribe(".*ODOMETRY", &BotGui::handleOdometry, this); // NOTE: Subscribe to all channels with odometry in the name
    lcmInstance_->subscribe(EXPLORATION_STATUS_CHANNEL, &BotGui::handleExplorationStatus, this);
}


void BotGui::render(void)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    
    // Draw the occupancy grid if requested
    vx_buffer_t* mapBuf = vx_world_get_buffer(world_, "map");
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showMapCheck_)))
    {
        draw_occupancy_grid(map_, mapBuf);
    }
    vx_buffer_swap(mapBuf);
    

    // Draw the distance grid if requested
    vx_buffer_t* distBuf = vx_world_get_buffer(world_, "distances");
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showDistancesCheck_)))
    {
        MotionPlanner planner;
        planner.setMap(map_);
        distances_ = planner.obstacleDistances();

        MotionPlannerParams params;
        draw_distance_grid(distances_,  params.robotRadius, distBuf);
    }
    vx_buffer_swap(distBuf);
    
    // Draw the frontiers if requested
    vx_buffer_t* frontierBuf = vx_world_get_buffer(world_, "frontiers");
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showFrontiersCheck_)))
    {
        draw_frontiers(frontiers_, map_.metersPerCell(), vx_magenta, frontierBuf);
    }
    vx_buffer_swap(frontierBuf);
    
    // Draw laser if requested
    vx_buffer_t* laserBuf = vx_world_get_buffer(world_, "laser");
    if(haveLaser_ && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showLaserCheck_)))
    {
        draw_laser_scan(laser_, slamPose_, vx_green, laserBuf);
    }
    vx_buffer_swap(laserBuf);


     // Draw the current path if requested
    vx_buffer_t* pathBuf = vx_world_get_buffer(world_, "path");
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showPathCheck_)))
    {
        draw_path(path_, vx_forest, pathBuf);
    }
    vx_buffer_swap(pathBuf);

    
    // Draw all active poses
    vx_buffer_t* poseBuf = vx_world_get_buffer(world_, "poses");
    for(auto& t : traces_)
    {
        Trace& trace = t.second;
        
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(trace.checkbox)))
        {
            draw_pose_trace(trace.trace, trace.color, poseBuf);
            
            if(!trace.trace.empty())
            {
                draw_robot(trace.trace.back(), trace.color, trace.body_color, poseBuf);
            }
        }
    }
    
    // If the SLAM_POSE has been assigned a color, then draw it using that color
    if(traces_.find(SLAM_POSE_CHANNEL) != traces_.end())
    {
        draw_robot(slamPose_, traces_[SLAM_POSE_CHANNEL].color, traces_[SLAM_POSE_CHANNEL].body_color, poseBuf);
    }
    vx_buffer_swap(poseBuf);
   
    
    // Draw the particles if requested
    vx_buffer_t* particleBuf = vx_world_get_buffer(world_, "particles");
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(showParticlesCheck_)))
    {
        draw_particles(slamParticles_, particleBuf);
    }
    vx_buffer_swap(particleBuf);
    
    gdk_threads_enter();        // lock the GTK loop to avoid race conditions during modification
    populateNewTraceBoxes();
    destroyTracesIfRequested();
    updateExplorationStatusIfNeeded();
    resetExplorationStatusIfRequested();
    updateGridStatusBarText();
    gdk_threads_leave();    // no more modifications to the window are happening, so unlock
}


void BotGui::handleOccupancyGrid(const lcm::ReceiveBuffer* rbuf, 
                                 const std::string& channel, 
                                 const occupancy_grid_t* map)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    map_.fromLCM(*map);
    frontiers_ = find_map_frontiers(map_, slamPose_);
}


void BotGui::handleParticles(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const particles_t* particles)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    slamParticles_ = *particles;
}


void BotGui::handlePose(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const pose_xyt_t* pose)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    addPose(*pose, channel);
    
    if(channel == SLAM_POSE_CHANNEL)
    {
        slamPose_ = *pose;
    }
}


void BotGui::handleOdometry(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const odometry_t* odom)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    
    odometry_ = *odom;
    
    // Also, save a PoseTrace for the pure odometry output
    pose_xyt_t odomPose;
    odomPose.utime = odom->utime;
    odomPose.x = odom->x;
    odomPose.y = odom->y;
    odomPose.theta = odom->theta;
    addPose(odomPose, channel);
}


void BotGui::handleLaser(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const lidar_t* laser)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    laser_ = *laser;
    haveLaser_ = true;
}


void BotGui::handlePath(const lcm::ReceiveBuffer* rbuf, const std::string& channel, const robot_path_t* path)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    path_ = *path;
    havePath_ = true;
}


void BotGui::handleExplorationStatus(const lcm::ReceiveBuffer* rbuf, 
                                     const std::string& channel, 
                                     const exploration_status_t* status)
{
    std::lock_guard<std::mutex> autoLock(vxLock_);
    exploreStatus_.push_back(*status);
}


void BotGui::addPose(const pose_xyt_t& pose, const std::string& channel)
{
    auto traceIt = traces_.find(channel);
    
    // If no trace exists yet for this channel, then create one
    if(traceIt == traces_.end())
    {
        std::cout << "Adding new PoseTrace:" << channel << '\n';
        Trace trace;
        trace.trace.addPose(pose);
        trace.checkbox = gtk_check_button_new_with_label(channel.c_str());
        
        
        //Color Selection per Channel
        if(channel == SLAM_POSE_CHANNEL) {trace.body_color = vx_yellow; trace.color = vx_blue;}
        else if(channel == ODOMETRY_CHANNEL){trace.body_color = vx_red; trace.color = vx_olive;}
        else if(channel == TRUE_POSE_CHANNEL){trace.body_color = vx_gray; trace.color = vx_orange;}
        else {
            trace.color = traceColors_[nextColorIndex_];
            trace.body_color = traceColors_[(nextColorIndex_ + 1) % traceColors_.size()];
            nextColorIndex_ = (nextColorIndex_ + 1) % traceColors_.size();
        }
        // If the new trace is TRUE_POSE_CHANNEL, then the frame transform of odometry needs to be updated
        if(channel == TRUE_POSE_CHANNEL)
        {
            haveTruePose_ = true;
            initialTruePose_ = pose;
            
            // Go through every existing trace and add a new frame transform if it corresponds to odometry
            for(auto& t : traces_)
            {
                if(t.first.find(ODOMETRY_CHANNEL) != std::string::npos)
                {
                    t.second.trace.setReferencePose(initialTruePose_);
                    std::cout << "Applying TRUE_POSE frame transform to trace on channel: " << t.first << '\n';
                }
            }
        }
        // If a true pose is specified, then we need to change reference frame of odometry if it is being added
        else if(haveTruePose_ && (channel.find(ODOMETRY_CHANNEL) != std::string::npos))
        {
            trace.trace.setReferencePose(initialTruePose_);
            std::cout << "Applying TRUE_POSE frame transform to trace on channel: " << channel << '\n';
        }
        
        // Set the color of the text to match the color of the trace
        GtkWidget* checkLabel = gtk_bin_get_child(GTK_BIN(trace.checkbox));
        
        GdkColor textColor;
        textColor.red = trace.color[0] * 65535;
        textColor.green = trace.color[1] * 65535;
        textColor.blue = trace.color[2] * 65535;
        
        GdkColor bgColor;
        gdk_color_parse("white", &bgColor);
        
        gtk_widget_modify_bg(checkLabel, GTK_STATE_NORMAL, &bgColor);
        gtk_widget_modify_bg(checkLabel, GTK_STATE_PRELIGHT, &bgColor);
        gtk_widget_modify_bg(checkLabel, GTK_STATE_ACTIVE, &bgColor);
        gtk_widget_modify_fg(checkLabel, GTK_STATE_NORMAL, &textColor);
        gtk_widget_modify_fg(checkLabel, GTK_STATE_PRELIGHT, &textColor);
        gtk_widget_modify_fg(checkLabel, GTK_STATE_ACTIVE, &textColor);
        
        traceBoxesToAdd_.push_back(trace.checkbox);
        traces_[channel] = trace;
    }
    // Otherwise, just add a pose to the existing trace
    else
    {
        traceIt->second.trace.addPose(pose);
    }
}


void BotGui::destroyTracesIfRequested(void)
{
    if(shouldClearTraces_)
    {
        // Cleanup all existing trace boxes.
        for(auto& t : traces_)
        {
            gtk_widget_destroy(t.second.checkbox);
        }
        
        traces_.clear();
        shouldClearTraces_ = false;
        haveTruePose_ = false;
    }
}


void BotGui::populateNewTraceBoxes(void)
{
    if(!traceBoxesToAdd_.empty())
    {
        for(auto& box : traceBoxesToAdd_)
        {
            gtk_box_pack_start(GTK_BOX(tracesBox_), box, FALSE, FALSE, 0);
            gtk_widget_show(box);
            gtk_container_resize_children(GTK_CONTAINER(tracesBox_)); // resize to make sure the full label is visible
        }
        
        gtk_widget_show(tracesBox_);
        
        traceBoxesToAdd_.clear();
    }
}


void BotGui::updateExplorationStatusIfNeeded(void)
{
    for(auto& status : exploreStatus_)
    {
        assert(status.state >= 0 && status.state < static_cast<int>(explorationStateLabels_.size()));
        
        GtkWidget* stateWidget = explorationStateLabels_[status.state];
        
        GdkColor color;
        switch(status.status)
        {
            case exploration_status_t::STATUS_IN_PROGRESS:
                gdk_color_parse("gold", &color);
                break;
            case exploration_status_t::STATUS_COMPLETE:
                gdk_color_parse("spring green", &color);
                break;
            case exploration_status_t::STATUS_FAILED:
                gdk_color_parse("red", &color);
                break;
            default:
                std::cerr << "ERROR: BotGui: Unknown exploration status: " << status.status << '\n';
                gdk_color_parse("light gray", &color);
                break;
        }
        
        gtk_widget_modify_bg(stateWidget, GTK_STATE_NORMAL, &color);
    }
    
    // Clear all messages because they have been processed
    exploreStatus_.clear();
}


void BotGui::resetExplorationStatusIfRequested(void)
{
    if(shouldResetStateLabels_)
    {
        GdkColor resetColor;
        gdk_color_parse("light gray", &resetColor);
        for(auto& widget : explorationStateLabels_)
        {
            gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &resetColor);
        }
        
        shouldResetStateLabels_ = false;
    }
}


void BotGui::updateGridStatusBarText(void)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << "Global: " << mouseWorldCoord_ << " Cell: " << mouseGridCoord_
        << " Log-odds: " << static_cast<int>(map_.logOdds(mouseGridCoord_.x, mouseGridCoord_.y));
    
    // For each active trace, write the current pose
    out << "    ";
    for(auto& t : traces_)
    {
        Trace& trace = t.second;
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(trace.checkbox)))
        {
            out << t.first << ": (" << trace.trace.back().x << ',' << trace.trace.back().y << ',' 
                << trace.trace.back().theta << ")   ";
        }
    }
        
    gtk_statusbar_pop(GTK_STATUSBAR(gridStatusBar_), 0);
    gtk_statusbar_push(GTK_STATUSBAR(gridStatusBar_), 0, out.str().c_str());
}


void BotGui::createGuiLayout(GtkWidget* window, GtkWidget* vxCanvas)
{
    GtkWidget* mainBox = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(window), mainBox);
    
    // Create a horizontal pane. Options/command on the right. Vx canvas and sensor data on the left
    GtkWidget* mainPane = gtk_hpaned_new();
//     gtk_container_add(GTK_CONTAINER(window), mainPane);
    gtk_box_pack_start(GTK_BOX(mainBox), mainPane, TRUE, TRUE, 0);
    
    // Create the Vx and live scoring pane as a vertical box
    GtkWidget* vxBox = gtk_vbox_new(FALSE, 5);
    gtk_paned_pack1(GTK_PANED(mainPane), vxBox, TRUE, FALSE);
    gtk_box_pack_end(GTK_BOX(vxBox), vxCanvas, TRUE, TRUE, 0);
    gtk_widget_show(vxCanvas);    // XXX Show all causes errors!
    
    // Add the widgets to the right 
    optionsBox_ = gtk_vbox_new(FALSE, 10);
    gtk_paned_pack2(GTK_PANED(mainPane), optionsBox_, FALSE, FALSE);
    
    ////////  Checkboxes for controlling which data is to be rendered  /////////////
    GtkWidget* dataLabel = gtk_label_new("Data to Show:");
    gtk_box_pack_start(GTK_BOX(optionsBox_), dataLabel, FALSE, TRUE, 0);
    
    showMapCheck_ = gtk_check_button_new_with_label("Show Map");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showMapCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showMapCheck_), TRUE);
    
    showLaserCheck_ = gtk_check_button_new_with_label("Show Laser");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showLaserCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showLaserCheck_), TRUE);
    
    showParticlesCheck_ = gtk_check_button_new_with_label("Show Particles");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showParticlesCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showParticlesCheck_), TRUE);
    
    showPathCheck_ = gtk_check_button_new_with_label("Show Path");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showPathCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showPathCheck_), TRUE);
    
    showDistancesCheck_ = gtk_check_button_new_with_label("Show Obstacle Distances");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showDistancesCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showDistancesCheck_), FALSE);
    
    showFrontiersCheck_ = gtk_check_button_new_with_label("Show Frontiers");
    gtk_box_pack_start(GTK_BOX(optionsBox_), showFrontiersCheck_, FALSE, TRUE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(showFrontiersCheck_), TRUE);
    
    GtkWidget* dataSeparator = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(optionsBox_), dataSeparator, FALSE, TRUE, 0);

    //////////////   Area where pose traces will be added   ////////////////
    GtkWidget* traceLabel = gtk_label_new("Available Pose Traces:");
    gtk_box_pack_start(GTK_BOX(optionsBox_), traceLabel, FALSE, TRUE, 0);
    
    tracesBox_ = gtk_vbox_new(FALSE, 10);
    gtk_box_pack_start(GTK_BOX(optionsBox_), tracesBox_, FALSE, TRUE, 0);
    
    clearTracesButton_ = gtk_button_new_with_label("Clear Traces");
    gtk_box_pack_start(GTK_BOX(optionsBox_), clearTracesButton_, FALSE, TRUE, 0);
    
    g_signal_connect(clearTracesButton_, 
                     "clicked", 
                     G_CALLBACK(clear_traces_pressed), 
                     static_cast<gpointer>(this));
    
    ///////////////////   Exploration state machine labels  /////////////////////
    
    GtkWidget* stateSeparator = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(optionsBox_), stateSeparator, FALSE, TRUE, 0);
    
    GtkWidget* stateMachineLabel = gtk_label_new("Exploration State:");
    gtk_box_pack_start(GTK_BOX(optionsBox_), stateMachineLabel, FALSE, FALSE, 0);
    
    GdkColor bgColor;
    gdk_color_parse("light gray", &bgColor);
    
    GdkColor textColor;
    gdk_color_parse("black", &textColor);
    
    explorationStateLabels_.push_back(gtk_event_box_new());
    GtkWidget* initializeText = gtk_label_new("Initializing");
    gtk_container_add(GTK_CONTAINER(explorationStateLabels_.back()), initializeText);
    gtk_widget_modify_fg(initializeText, GTK_STATE_NORMAL, &textColor);
    gtk_widget_modify_bg(explorationStateLabels_.back(), GTK_STATE_NORMAL, &bgColor);
    gtk_box_pack_start(GTK_BOX(optionsBox_), explorationStateLabels_.back(), FALSE, FALSE, 0);
    
    explorationStateLabels_.push_back(gtk_event_box_new());
    GtkWidget* exploreText = gtk_label_new("Exploring Map");
    gtk_container_add(GTK_CONTAINER(explorationStateLabels_.back()), exploreText);
    gtk_widget_modify_fg(exploreText, GTK_STATE_NORMAL, &textColor);
    gtk_widget_modify_bg(explorationStateLabels_.back(), GTK_STATE_NORMAL, &bgColor);
    gtk_box_pack_start(GTK_BOX(optionsBox_), explorationStateLabels_.back(), FALSE, FALSE, 0);
    
    explorationStateLabels_.push_back(gtk_event_box_new());
    GtkWidget* returnText = gtk_label_new("Returning Home");
    gtk_container_add(GTK_CONTAINER(explorationStateLabels_.back()), returnText);
    gtk_widget_modify_fg(returnText, GTK_STATE_NORMAL, &textColor);
    gtk_widget_modify_bg(explorationStateLabels_.back(), GTK_STATE_NORMAL, &bgColor);
    gtk_box_pack_start(GTK_BOX(optionsBox_), explorationStateLabels_.back(), FALSE, FALSE, 0);
    
    explorationStateLabels_.push_back(gtk_event_box_new());
    GtkWidget* completedText = gtk_label_new("Completed Exploration");
    gtk_container_add(GTK_CONTAINER(explorationStateLabels_.back()), completedText);
    gtk_widget_modify_fg(completedText, GTK_STATE_NORMAL, &textColor);
    gtk_widget_modify_bg(explorationStateLabels_.back(), GTK_STATE_NORMAL, &bgColor);
    gtk_box_pack_start(GTK_BOX(optionsBox_), explorationStateLabels_.back(), FALSE, FALSE, 0);
    
    explorationStateLabels_.push_back(gtk_event_box_new());
    GtkWidget* failedText = gtk_label_new("Failed Exploration");
    gtk_container_add(GTK_CONTAINER(explorationStateLabels_.back()), failedText);
    gtk_widget_modify_fg(failedText, GTK_STATE_NORMAL, &textColor);
    gtk_widget_modify_bg(explorationStateLabels_.back(), GTK_STATE_NORMAL, &bgColor);
    gtk_box_pack_start(GTK_BOX(optionsBox_), explorationStateLabels_.back(), FALSE, FALSE, 0);
    
    GtkWidget* resetStateButton = gtk_button_new_with_label("Reset Exploration States");
    gtk_box_pack_start(GTK_BOX(optionsBox_), resetStateButton, FALSE, TRUE, 0);
    
    g_signal_connect(resetStateButton, 
                     "clicked", 
                     G_CALLBACK(reset_state_pressed), 
                     static_cast<gpointer>(this));
    
    gridStatusBar_ = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(mainBox), gridStatusBar_, FALSE, TRUE, 0);
    
    gtk_widget_show(vxCanvas);    // XXX Show all causes errors!
    gtk_widget_show_all(window);
}


void clear_traces_pressed(GtkWidget* button, gpointer gui)
{
    BotGui* botGui = static_cast<BotGui*>(gui);
    botGui->clearAllTraces();
}


void reset_state_pressed(GtkWidget* button, gpointer gui)
{
    BotGui* botGui = static_cast<BotGui*>(gui);
    botGui->resetExplorationStates();
}
