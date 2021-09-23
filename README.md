# Slam and Path Planning implementation on MBot

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/e4a8bdc349bb4fb3b6b5d5cc9bfe135b)](https://app.codacy.com/gh/saptadeb/botLab?utm_source=github.com&utm_medium=referral&utm_content=saptadeb/botLab&utm_campaign=Badge_Grade_Settings)

A project for ROB 550: Robotics Systems Lab course taught in University of Michigan, Ann Arbor. Due to the COVID-19 pandemic this project was migrated to an online project. An overview of this project:
- Acting
    - Planar kinematics of a differential-drive ground robot
    - Motion models with uncertainty

- Perception 
    - Quadrature Encoders
    - MEMS Inertial Measurement Unit
    - 2D LIDAR Rangefinders

- Reasoning
    - Monte Carlo Localization
    - Simultaneous Localization and Mapping
    - A* search
    - Path planning



## Running the code

#### `log_mbot_sensors.sh`
- a script to log the sensor data needed for SLAM so you can easily create your own log files for testing

#### `setenv.sh`
- a script for setting environment variables needed for running Vx applications
- run this script before running botgui in a terminal on your laptop
- run via `. setenv.sh` -- note the space

1. Mapping
    - `lcm-logplayer-gui <log_file.log>` and uncheck all SLAM channels but SLAM_POSE
    - `./botgui`
    - `./slam --mapping-only`

2. Action Model
    - `lcm-logplayer-gui <log_file.log>` and uncheck all SLAM channels 
    - `./botgui`
    - `./slam --action-only`

3. Sensor Model
    - `lcm-logplayer-gui <log_file.log>` and uncheck all SLAM channels 
    - `./botgui`
    - `./slam --localization-only <map_file.map>`

4. Full SLAM
    - `lcm-logplayer-gui <log_file.log>` and uncheck all SLAM channels 
    - `./botgui`
    - `./slam`

5. Obstacle Distance Grid
    - `lcm-logplayer-gui <log_file.log>` and uncheck all SLAM channels 
    - `./botgui`
    - View the generated obstacle distance grid by ticking “Show Obstacle Distances” in botgui

6. A* Algorithm
    - `cd <path_to_dir>/src/sim`
    - `python3 sim.py <map_file.map>`
    - `./timesync`
    - `./botgui`
    - `./motion_controller`
    - `./slam --localization-only <map_file.map>`
    - Right-click on the botgui to drive the robot to that location

7. Frontier exploration
    - `cd <path_to_dir>/src/sim`
    - `python3 sim.py <map_file.map>`
    - `./timesync`
    - `./botgui`
    - `./motion_controller`
    - `./slam`
    - `./exploration` 

Check the [final report](https://github.com/saptadeb/botLab/blob/master/report/saptadeb-botlab.pdf) for detailed explanation and results.

## Directories 

#### `bin/`
- where all built binaries are located
    
#### `data/`
- where data needed to run parts of the assignment are located
- log files and target files for SLAM and exploration are here
    
#### `lcmtypes/`
- where the .lcm type definitions are located
- the generated types are stored in src/lcmtypes
    
#### `lib/`
- where static libraries are saved during the build process
    
#### `src/`
- where all source code for botlab is located
- subdirectories have a further description of their contents

