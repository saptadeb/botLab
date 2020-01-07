#include <lcm/lcm-cpp.hpp>

#include <lcmtypes/oled_message_t.hpp>
#include <common/timestamp.h>
#include <common/lcm_config.h>

#include <ctime>
#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

#include <unistd.h>

int main(int argc, char ** argv)
{
	lcm::LCM lcm(MULTICAST_URL);
	if(!lcm.good())
		return 1;

	oled_message_t message;

	std::stringstream ss;
	time_t t = time(0);
	struct tm * now = localtime(&t);

	ss << std::setfill('0') << std::setw(2);

	while(1){
		t = time(0);
		now = localtime(&t);

		ss << std::setfill('0') << std::setw(2) << (now->tm_hour) 
			    << ':' << (now->tm_min) << ':' << now->tm_sec;
		std::cout << ss.str() << '\n';

		message.utime = utime_now();
		message.line1 = ss.str();
		message.line2 = "I really really really really <3 robots!";

		lcm.publish(OLED_CHAN, &message);

		ss.clear();
		ss.str(std::string());
		usleep(1000000);
	}
//	oled_message_t message;
}
