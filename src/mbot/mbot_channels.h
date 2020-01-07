#ifndef MBOT_CHANNELS_H
#define MBOT_CHANNELS_H

/////// Data channels //////

#define MBOT_STATUS_CHANNEL "MBOT_STATUS"
#define MBOT_IMU_CHANNEL "MBOT_IMU"
#define MBOT_ENCODERS_CHANNEL "MBOT_ENCODERS"
#define LIDAR_CHANNEL "LIDAR"
#define WIFI_READINGS_CHANNEL "WIFI"

//////// Additional channels for processes that run on the Mbot -- odometry and motion_controller.
#define ODOMETRY_CHANNEL "ODOMETRY"
#define CONTROLLER_PATH_CHANNEL "CONTROLLER_PATH"

/////// Command channels ///////

#define MBOT_MOTOR_COMMAND_CHANNEL "MBOT_MOTOR_COMMAND"

#define MBOT_TIMESYNC_CHANNEL "MBOT_TIMESYNC"
#define MESSAGE_CONFIRMATION_CHANNEL "MSG_CONFIRM"

#endif // MBOT_CHANNELS_H
