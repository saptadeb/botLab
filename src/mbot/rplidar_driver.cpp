#include <iostream>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include <lcm/lcm-cpp.hpp>
#include <lcmtypes/lidar_t.hpp>

#include <common/rplidar.h>
#include <common/lcm_config.h> 
#include <common/timestamp.h>

#ifndef _countof
#define _countof(_Array) (int)(sizeof(_Array) / sizeof(_Array[0]))
#endif

using namespace rp::standalone::rplidar;

bool checkRPLIDARHealth(RPlidarDriver * drv)
{
    u_result     op_result;
    rplidar_response_device_health_t healthinfo;


    op_result = drv->getHealth(healthinfo);
    if (IS_OK(op_result)) { // the macro IS_OK is the preperred way to judge whether the operation is succeed.
        printf("RPLidar health status : %d\n", healthinfo.status);
        if (healthinfo.status == RPLIDAR_STATUS_ERROR) {
            fprintf(stderr, "Error, rplidar internal error detected. Please reboot the device to retry.\n");
            // enable the following code if you want rplidar to be reboot by software
            // drv->reset();
            return false;
        } else {
            return true;
        }

    } else {
        fprintf(stderr, "Error, cannot retrieve the lidar health code: %x\n", op_result);
        return false;
    }
}

#include <signal.h>
bool ctrl_c_pressed;
void ctrlc(int)
{
    ctrl_c_pressed = true;
}

int main(int argc, const char * argv[]) {

    //Shouldn't need to adjust these with the exception of pwm
    const char * opt_com_path = NULL;
    _u32         opt_com_baudrate = 115200;
    u_result     op_result;
    uint16_t pwm = 700;

/** speed stabilization tests
int updateHZ_count = 0;
int speeds[] = {300, 700, 250, 850};
int speed_at = 0;
float stab_time = 0.0f;
uint64_t changed_time = 0;
uint prev_size[] = {0, 0, 0};
bool stabilized = false;
**/

    lcm::LCM lcmConnection(MULTICAST_URL);

    if(!lcmConnection.good()){ return 1; }

    printf("LIDAR data grabber for RPLIDAR.\n"
           "Version: %s\n", RPLIDAR_SDK_VERSION);

    // read pwm from command line if specified...
    if (argc>1) pwm = atoi(argv[1]);

    // read serial port from the command line if specified...
    if (argc>2) opt_com_path = argv[2]; // or set to a fixed value: e.g. "com3" 

    // read baud rate from the command line if specified...
    if (argc>3) opt_com_baudrate = strtoul(argv[3], NULL, 10);


    if (!opt_com_path) {
#ifdef _WIN32
        // use default com port
        opt_com_path = "\\\\.\\com3";
#else
        opt_com_path = "/dev/ttyUSB0";
#endif
    }

    // create the driver instance
    RPlidarDriver * drv = RPlidarDriver::CreateDriver(RPlidarDriver::DRIVER_TYPE_SERIALPORT);
    
    if (!drv) {
        fprintf(stderr, "insufficent memory, exit\n");
        exit(-2);
    }


    // make connection...
    if (IS_FAIL(drv->connect(opt_com_path, opt_com_baudrate))) {
        fprintf(stderr, "Error, cannot bind to the specified serial port %s.\n"
            , opt_com_path);
        goto on_finished;
    }

    rplidar_response_device_info_t devinfo;

	// retrieving the device info
    ////////////////////////////////////////
    op_result = drv->getDeviceInfo(devinfo);

    if (IS_FAIL(op_result)) {
        fprintf(stderr, "Error, cannot get device info.\n");
        goto on_finished;
    }

    // print out the device serial number, firmware and hardware version number..
    printf("RPLIDAR S/N: ");
    for (int pos = 0; pos < 16 ;++pos) {
        printf("%02X", devinfo.serialnum[pos]);
    }

    printf("\n"
            "Firmware Ver: %d.%02d\n"
            "Hardware Rev: %d\n"
            , devinfo.firmware_version>>8
            , devinfo.firmware_version & 0xFF
            , (int)devinfo.hardware_version);

    // check health...
    if (!checkRPLIDARHealth(drv)) {
        goto on_finished;
    }

	signal(SIGINT, ctrlc);
    signal(SIGTERM, ctrlc);
    
	drv->startMotor();
    // start scan...
    drv->setMotorPWM(pwm);
    drv->startScan();

    // fetech result and print it out...
    while (1) {

        rplidar_response_measurement_node_t nodes[360*2];
        size_t   count = _countof(nodes);

        op_result = drv->grabScanData(nodes, count);

           
        //gettimeofday(&tv, NULL);
	    int64_t now = utime_now();//(int64_t) tv.tv_sec * 1000000 + tv.tv_usec; //get current timestamp in milliseconds

/**speed stabilization tests
        updateHZ_count++;


        if(updateHZ_count > 100){
            updateHZ_count = 0;

            drv->setMotorPWM(speeds[speed_at]);

            std::cout << "set speed to: " << speeds[speed_at] << "\n";

            speed_at++;
            if(speed_at > 3) speed_at = 0;

            prev_size[0] = 0;
            prev_size[1] = 0;
            prev_size[2] = 0;
            changed_time = now;

            stabilized = false;
        }
**/

        if (IS_OK(op_result)) {
            drv->ascendScanData(nodes, count);

            lidar_t newLidar;

            newLidar.utime = now;
            newLidar.num_ranges = count;

            newLidar.ranges.resize(count);
            newLidar.thetas.resize(count);
            newLidar.intensities.resize(count);
            newLidar.times.resize(count);

/**speed stabilization tests

            if(!stabilized && count == prev_size[0] && prev_size[0] == prev_size[1] && prev_size[1] == prev_size[2]){
                prev_size[0] = 0;
                prev_size[1] = 0;
                prev_size[2] = 0;

                stab_time = float(now - changed_time) / 1000.0f;

                std::cout << "stabilization time: " << stab_time << "\n";

                stabilized = true;
            }else{
                prev_size[2] = prev_size[1];
                prev_size[1] = prev_size[0];
                prev_size[0] = count;
            }
*/

            //	    std::cout << count << "\n";
            //		printf("theta,dist,Q\n");

            for (int pos = 0; pos < (int)count ; ++pos) {
            	//gettimeofday(&tv, NULL);
		        now = utime_now();//(int64_t) tv.tv_sec * 1000000 + tv.tv_usec; //get current timestamp in milliseconds
            	newLidar.ranges[pos] = nodes[pos].distance_q2/4000.0f;
            	newLidar.thetas[pos] = (nodes[pos].angle_q6_checkbit >> RPLIDAR_RESP_MEASUREMENT_ANGLE_SHIFT)*3.1415926535f/11520.0f;
            	newLidar.intensities[pos] = nodes[pos].sync_quality >> RPLIDAR_RESP_MEASUREMENT_QUALITY_SHIFT;
            	newLidar.times[pos] = now;

//                printf("%s theta: %03.2f Dist: %08.2f Q: %f Time: %" PRIu64 "\n", 
//                    (nodes[pos].sync_quality & RPLIDAR_RESP_MEASUREMENT_SYNCBIT) ?"S ":"  ", 
//                   	newLidar.thetas[pos],
//                    newLidar.ranges[pos],
//                    newLidar.intensities[pos],
//                    newLidar.times[pos]);
//		printf("%03.2f,%08.2f,%f\n", newLidar.thetas[pos], newLidar.ranges[pos], newLidar.intensities[pos]);
            }

            lcmConnection.publish("LIDAR", &newLidar);
        }

        if (ctrl_c_pressed){ 
			break;
		}
    }

    drv->stop();
    drv->stopMotor();
    // done!
on_finished:
    RPlidarDriver::DisposeDriver(drv);
    return 0;
}

