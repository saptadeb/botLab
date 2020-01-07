#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <lcm/lcm.h>
#include <lcmtypes/wifi_data_t.h>

#include <common/lcm_config.h>

lcm_t * lcm = NULL;
wifi_data_t wifi_data;
struct timeval tv;
static const char WIFI_CHAN[] = "WIFI_DATA";
int ssid_size = 9;
char ssid[64] = "MWireless"; 
char buff[512], tmp_mac[17];
char interface[8] = "wlp4s0";
char cmd[64] = "sudo iw ";

int parse_wifi_scan(){
	FILE *in;
	extern FILE *popen();
	uint8_t first_line = 1;

	if(!(in = popen(cmd, "r"))){
		return 1;
	}

	while(fgets(buff, sizeof(buff), in)!=NULL){
		if(first_line && strncmp(buff, "BSS ", 4) != 0){
			printf("error with iw command\n");
			return 1;
		}else if(first_line){
			first_line = 0;
		}

		if(strncmp(buff, "BSS ", 4) == 0){
			strncpy(tmp_mac, &buff[4], 17);
			tmp_mac[16] = '\0';
			//printf("%s\n", tmp_mac);
		}

		if(strncmp(buff, "\tsignal: ", 9) == 0){
			wifi_data.strength = strtof(&buff[9], NULL);
			//printf("%2.2f\n", wifi_data.strength);
		}

		if((strncmp(buff, "\tSSID: ", 7) == 0) && (strncmp(&buff[7], ssid, ssid_size) == 0)){
			wifi_data.mac_address = tmp_mac;
			printf("Sending to %s channel:\nssid: %s\nmac: %s\nstrength: %2.2f\n\n",
			 WIFI_CHAN,
			 wifi_data.ssid,
			 wifi_data.mac_address,
		 	 wifi_data.strength);

			gettimeofday(&tv, NULL);
            wifi_data.timestamp = (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;

			wifi_data_t_publish(lcm, WIFI_CHAN, &wifi_data);
		}

	}

	return WEXITSTATUS(pclose(in));
}

int main(void) {
	int rc;

	strcat(cmd, interface);
	strcat(cmd, " scan");

	lcm = lcm_create(MULTICAST_URL);

	wifi_data.ssid = ssid;

	while(1){
		rc = parse_wifi_scan();
		if(rc != 0){
			printf("error in parse_wifi_scan(), iw return code: %d\n", rc);
			usleep(5000);
		}
		usleep(1000);
	}
}
