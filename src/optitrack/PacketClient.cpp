//=============================================================================
// Copyright © 2014 NaturalPoint, Inc. All Rights Reserved.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall NaturalPoint, Inc. or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//=============================================================================

/*

PacketClient.cpp

Decodes NatNet packets directly.

Usage [optional]:

	PacketClient [ServerIP] [LocalIP]

	[ServerIP]			IP address of server ( defaults to local machine)
	[LocalIP]			IP address of client ( defaults to local machine)

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>

// Linux socket headers
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

// Include the one quadrotor header needed
#include "run_motion_capture.h"

#define SOCKET int  // A sock handle is just an int in linux

// Windows headers - commented out for BBB
// #include <tchar.h>
// #include <conio.h>
// #include <winsock2.h>
// #include <ws2tcpip.h>

#define MAX_NAMELENGTH              256

// NATNET message ids
#define NAT_PING                    0
#define NAT_PINGRESPONSE            1
#define NAT_REQUEST                 2
#define NAT_RESPONSE                3
#define NAT_REQUEST_MODELDEF        4
#define NAT_MODELDEF                5
#define NAT_REQUEST_FRAMEOFDATA     6
#define NAT_FRAMEOFDATA             7
#define NAT_MESSAGESTRING           8
#define NAT_UNRECOGNIZED_REQUEST    100
#define UNDEFINED                   999999.9999

#define MAX_PACKETSIZE	   100000	// max size of packet (actual packet size is dynamic)

// sender
typedef struct {
    char szName[MAX_NAMELENGTH];            // sending app's name
    unsigned char Version[4];               // sending app's version [major.minor.build.revision]
    unsigned char NatNetVersion[4];         // sending app's NatNet version [major.minor.build.revision]

} sSender;

typedef struct {
    unsigned short iMessage;                // message ID (e.g. NAT_FRAMEOFDATA)
    unsigned short nDataBytes;              // Num bytes in payload
    union {
        unsigned char  cData[MAX_PACKETSIZE];
        char           szData[MAX_PACKETSIZE];
        unsigned long  lData[MAX_PACKETSIZE / 4];
        float          fData[MAX_PACKETSIZE / 4];
        sSender        Sender;
    } Data;                                 // Payload

} sPacket;


//bool IPAddress_StringToAddr(char *szNameOrAddress, struct in_addr *ina);
void Unpack(char *pData);
void Unpack_to_code(char *pData, struct optitrack_message *optmsg);

//int GetLocalIPAddresses(unsigned long Addresses[], int nMax);
int SendCommand(char *szCOmmand);

#define MULTICAST_ADDRESS  "239.255.42.99"     // IANA, local network
#define PORT_COMMAND       1510
#define PORT_DATA  	   1511

SOCKET CommandSocket;
SOCKET DataSocket;
in_addr ServerAddress;
sockaddr_in HostAddr;

int NatNetVersion[4] = {0, 0, 0, 0};
int ServerVersion[4] = {0, 0, 0, 0};

#define MAX_PATH 1000 // Not sure why this hasn't been defined...
int gCommandResponse = 0;
int gCommandResponseSize = 0;
unsigned char gCommandResponseString[MAX_PATH];
int gCommandResponseCode = 0;

// command response listener thread
void *CommandListenThread(void * /*dummy*/)
{
    socklen_t addr_len;
    int nDataBytesReceived;
    sockaddr_in TheirAddress;
    sPacket PacketIn;
    addr_len = sizeof(struct sockaddr);

    while (1) {
        // blocking
        nDataBytesReceived = recvfrom(CommandSocket, (char *)&PacketIn,
                                      sizeof(sPacket),
                                      0, (struct sockaddr *)&TheirAddress,
                                      &addr_len);

        if (nDataBytesReceived == 0) // || (nDataBytesReceived == SOCKET_ERROR) )
            continue;

        // debug - print message
        /*        sprintf(str, "[Client] Received command from %d.%d.%d.%d: Command=%d, nDataBytes=%d",
        	TheirAddress.sin_addr.S_un.S_un_b.s_b1, TheirAddress.sin_addr.S_un.S_un_b.s_b2,
        	TheirAddress.sin_addr.S_un.S_un_b.s_b3, TheirAddress.sin_addr.S_un.S_un_b.s_b4,
        	(int)PacketIn.iMessage, (int)PacketIn.nDataBytes);
        */

        // handle command
        switch (PacketIn.iMessage) {
        case NAT_MODELDEF:
            Unpack((char *)&PacketIn);
            break;
        case NAT_FRAMEOFDATA:
            Unpack((char *)&PacketIn);
            break;
        case NAT_PINGRESPONSE:
            for (int i = 0; i < 4; i++) {
                NatNetVersion[i] = (int)PacketIn.Data.Sender.NatNetVersion[i];
                ServerVersion[i] = (int)PacketIn.Data.Sender.Version[i];
            }
            break;
        case NAT_RESPONSE:
            gCommandResponseSize = PacketIn.nDataBytes;
            if (gCommandResponseSize == 4)
                memcpy(&gCommandResponse, &PacketIn.Data.lData[0], gCommandResponseSize);
            else {
                memcpy(&gCommandResponseString[0], &PacketIn.Data.cData[0], gCommandResponseSize);
                printf("Response : %s", gCommandResponseString);
                gCommandResponse = 0;   // ok
            }
            break;
        case NAT_UNRECOGNIZED_REQUEST:
            printf("[Client] received 'unrecognized request'\n");
            gCommandResponseSize = 0;
            gCommandResponse = 1;       // err
            break;
        case NAT_MESSAGESTRING:
            printf("[Client] Received message: %s\n", PacketIn.Data.szData);
            break;
        }
    }

    return NULL;
}

// Data listener thread --> copied into run_motion_capture() function (EMA)
void *DataListenThread(void * /*dummy*/)
{
    char  szData[20000];
    socklen_t addr_len = sizeof(struct sockaddr);
    sockaddr_in TheirAddress;

    while (1) {
        // Block until we receive a datagram from the network (from anyone including ourselves)
        recvfrom(DataSocket, szData, sizeof(szData),
                 0, (sockaddr *)&TheirAddress, &addr_len);
        Unpack(szData);
    }

    return NULL;
}

SOCKET CreateCommandSocket(unsigned long IP_Address, unsigned short uPort)
{
    struct sockaddr_in my_addr;
    static unsigned long ivalue;
    SOCKET sockfd;

    // Create a blocking, datagram socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        return -1;
    }

    // bind socket
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(uPort);
    my_addr.sin_addr.s_addr = IP_Address;
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))
            < 0) {
        //close(sockfd);
        return -1;
    }

    // set to broadcast mode
    ivalue = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&ivalue, sizeof(ivalue)) < 0) {
        //close(sockfd);
        return -1;
    }

    return sockfd;
}

/* Commented out - using run_motion_capture() thread instead.
 *
int main(int argc, char* argv[])
{
    int retval;
    char szMyIPAddress[128] = "";
    char szServerIPAddress[128] = "";
    in_addr MyAddress, MultiCastAddress;
    int optval = 0x100000;
    socklen_t optval_size = 4;
    struct ifaddrs *ifAddrStruct=NULL, *ifa=NULL;
    void *tmpAddrPtr=NULL;

    // Thread variables (replaces windows thread handlers)
    pthread_t command_listen_thread;
    pthread_t data_listen_thread;

   // server address
    if(argc>1) {
      strcpy(szServerIPAddress, argv[1]);	// specified on command line
    } else {
      getifaddrs(&ifAddrStruct);
      for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
	if (!ifa->ifa_addr) continue;
	if (ifa->ifa_addr->sa_family == AF_INET) { // IP4
	  tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
	  // char addressBuffer[INET_ADDRSTRLEN];
	  inet_ntop(AF_INET, tmpAddrPtr, szServerIPAddress, INET_ADDRSTRLEN);
	  printf("%s IP Address %s\n", ifa->ifa_name, szServerIPAddress);
  	}
      }
    }
    ServerAddress.s_addr = (uint32_t) inet_addr(szServerIPAddress);

    // client address
    if(argc>2) {
      strcpy(szMyIPAddress, argv[2]);	// specified on command line
    } else {
      getifaddrs(&ifAddrStruct);
      for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
	if (!ifa->ifa_addr) continue;
	if (ifa->ifa_addr->sa_family == AF_INET) { // IP4
	  tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
	  // char addressBuffer[INET_ADDRSTRLEN];
	  inet_ntop(AF_INET, tmpAddrPtr, szMyIPAddress, INET_ADDRSTRLEN);
	  printf("%s IP Address %s\n", ifa->ifa_name, szMyIPAddress);
  	}
      }
    }
    MyAddress.s_addr = inet_addr(szMyIPAddress);

    MultiCastAddress.s_addr = inet_addr(MULTICAST_ADDRESS);
    printf("Client: %s\n", szMyIPAddress);
    printf("Server: %s\n", szServerIPAddress);
    printf("Multicast Group: %s\n", MULTICAST_ADDRESS);

    // create "Command" socket
    int port = 0;
    CommandSocket = CreateCommandSocket(MyAddress.s_addr,port);
    if(CommandSocket != -1) {
      // [optional] set to non-blocking
      //u_long iMode=1;
      //ioctlsocket(CommandSocket,FIONBIO,&iMode);
      // set buffer
      setsockopt(CommandSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, 4);
      getsockopt(CommandSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, &optval_size);
      if (optval != 0x100000) {
	// err - actual size...
      }
      // startup our "Command Listener" thread
      pthread_create(&command_listen_thread, NULL, CommandListenThread, NULL);
   }

    // create a "Data" socket
    DataSocket = socket(AF_INET, SOCK_DGRAM, 0);

    // allow multiple clients on same machine to use address/port
    int value = 1;
    retval = setsockopt(DataSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&value, sizeof(value));
    if (retval < 0)
    {
      //close(DataSocket);
      return -1;
    }

    struct sockaddr_in MySocketAddr;
    memset(&MySocketAddr, 0, sizeof(MySocketAddr));
    MySocketAddr.sin_family = AF_INET;
    MySocketAddr.sin_port = htons(PORT_DATA);
    MySocketAddr.sin_addr = MyAddress;
    if (bind(DataSocket, (struct sockaddr *)&MySocketAddr, sizeof(struct sockaddr)) < 0)
    {
      printf("[PacketClient] bind failed.\n");
      return 0;
    }
    // join multicast group
    struct ip_mreq Mreq;
    Mreq.imr_multiaddr = MultiCastAddress;
    Mreq.imr_interface = MyAddress;
    retval = setsockopt(DataSocket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&Mreq, sizeof(Mreq));
    if (retval < 0)
    {
        printf("[PacketClient] join failed.\n");
        return -1;
    }


	// create a 1MB buffer
    setsockopt(DataSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, 4);
    getsockopt(DataSocket, SOL_SOCKET, SO_RCVBUF, (char *)&optval, &optval_size);
    if (optval != 0x100000)
    {
        printf("[PacketClient] ReceiveBuffer size = %d", optval);
    }
    // startup our "Data Listener" thread
    pthread_create(&data_listen_thread, NULL, DataListenThread, NULL);

    // server address for commands
    memset(&HostAddr, 0, sizeof(HostAddr));
    HostAddr.sin_family = AF_INET;
    HostAddr.sin_port = htons(PORT_COMMAND);
    HostAddr.sin_addr = ServerAddress;

    // send initial ping command
    sPacket PacketOut;
    PacketOut.iMessage = NAT_PING;
    PacketOut.nDataBytes = 0;
    int nTries = 3;
    while (nTries--)
    {
        int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
        if(iRet < 0)
            break;
    }


    printf("Packet Client started\n\n");
    printf("Commands:\ns\tsend data descriptions\nf\tsend frame of data\nt\tsend test request\nq\tquit\n\n");
    int c;
    char szRequest[512];
    bool bExit = false;
    nTries = 3;

    while (!bExit)
    {
        c =getchar();
        switch(c)
        {
        case 's':
            // send NAT_REQUEST_MODELDEF command to server (will respond on the "Command Listener" thread)
            PacketOut.iMessage = NAT_REQUEST_MODELDEF;
            PacketOut.nDataBytes = 0;
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet < 0)
                    break;
            }
            break;
        case 'f':
            // send NAT_REQUEST_FRAMEOFDATA (will respond on the "Command Listener" thread)
            PacketOut.iMessage = NAT_REQUEST_FRAMEOFDATA;
            PacketOut.nDataBytes = 0;
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet < 0)
                    break;
            }
            break;
        case 't':
            // send NAT_MESSAGESTRING (will respond on the "Command Listener" thread)
            strcpy(szRequest, "TestRequest");
            PacketOut.iMessage = NAT_REQUEST;
            PacketOut.nDataBytes = (int)strlen(szRequest) + 1;
            strcpy(PacketOut.Data.szData, szRequest);
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet < 0)
                    break;
            }
            break;
        case 'p':
            // send NAT_MESSAGESTRING (will respond on the "Command Listener" thread)
            strcpy(szRequest, "Ping");
            PacketOut.iMessage = NAT_PING;
            PacketOut.nDataBytes = (int)strlen(szRequest) + 1;
            strcpy(PacketOut.Data.szData, szRequest);
            nTries = 3;
            while (nTries--)
            {
                int iRet = sendto(CommandSocket, (char *)&PacketOut, 4 + PacketOut.nDataBytes, 0, (sockaddr *)&HostAddr, sizeof(HostAddr));
                if(iRet < 0)
                    break;
            }
            break;
        case 'w':
            {
                char szCommand[512];
                long testVal;
                int returnCode;
		// Unused now - EMA
                testVal = -50;
                sprintf(szCommand, "SetPlaybackStartFrame,%ld",testVal);
                returnCode = SendCommand(szCommand);

                testVal = 1500;
                sprintf(szCommand, "SetPlaybackStopFrame,%ld",testVal);
                returnCode = SendCommand(szCommand);

                testVal = 0;
                sprintf(szCommand, "SetPlaybackLooping,%ld",testVal);
                returnCode = SendCommand(szCommand);

                testVal = 100;
                sprintf(szCommand, "SetPlaybackCurrentFrame,%ld",testVal);
                returnCode = SendCommand(szCommand);

            }
            break;
        case 'q':
            bExit = true;
            break;
        default:
            break;
        }
    }
    return 0;
}
*/

// convert ip address string to addr
/* Not used
bool IPAddress_StringToAddr(char *server_name, struct in_addr *address)
{
  int retVal;
  struct sockaddr_in saGNI;
  struct hostent *hp;

  // Attempt to detect if we should call gethostbyname() or
  // gethostbyaddr()
  if (isalpha(server_name[0])) {
    hp = gethostbyname(server_name);
  }  else  {
    hp = gethostbyaddr((char *)&addr,4,AF_INET);
  }
  if (hp == NULL ) {
    fprintf(stderr,"Client: Cannot resolve address [%s]\n",
            server_name);
    exit(1);
  }

  // Length:  hp->h_length, sin_family = hp->h_addrtype;
  return true;
}
*/

// get ip addresses on local host
/* Windows
int GetLocalIPAddresses(unsigned long Addresses[], int nMax)
{
  unsigned long  NameLength = 128;
  char szMyName[1024];
  struct addrinfo aiHints;
  struct addrinfo *aiList = NULL;
  struct sockaddr_in addr;
  int retVal = 0;
  char* port = "0";

  if(GetComputerName(szMyName, &NameLength) != TRUE)
    {
      printf("[PacketClient] get computer name  failed.\n");
      return 0;
    };

  memset(&aiHints, 0, sizeof(aiHints));
  aiHints.ai_family = AF_INET;
  aiHints.ai_socktype = SOCK_DGRAM;
  aiHints.ai_protocol = IPPROTO_UDP;
  if ((retVal = getaddrinfo(szMyName, port, &aiHints, &aiList)) != 0)
    {
      printf("[PacketClient] getaddrinfo failed.\n");
      return 0;
    }

  memcpy(&addr, aiList->ai_addr, aiList->ai_addrlen);
  freeaddrinfo(aiList);
  Addresses[0] = addr.sin_addr.S_un.S_addr;

  return 1;
}
*/

bool DecodeTimecode(unsigned int inTimecode, unsigned int inTimecodeSubframe, int *hour, int *minute, int *second, int *frame, int *subframe)
{
    bool bValid = true;

    *hour = (inTimecode >> 24) & 255;
    *minute = (inTimecode >> 16) & 255;
    *second = (inTimecode >> 8) & 255;
    *frame = inTimecode & 255;
    *subframe = inTimecodeSubframe;

    return bValid;
}

bool TimecodeStringify(unsigned int inTimecode, unsigned int inTimecodeSubframe, char *Buffer, int /*BufferSize*/)
{
    bool bValid;
    int hour, minute, second, frame, subframe;
    bValid = DecodeTimecode(inTimecode, inTimecodeSubframe, &hour, &minute, &second, &frame, &subframe);

    sprintf(Buffer, "%2d:%2d:%2d:%2d.%d",
            hour, minute, second, frame, subframe);
    for (unsigned int i = 0; i < strlen(Buffer); i++)
        if (Buffer[i] == ' ')
            Buffer[i] = '0';

    return bValid;
}


void Unpack(char *pData)
{
    int major = NatNetVersion[0];
    int minor = NatNetVersion[1];

    char *ptr = pData;
    printf("Begin Packet\n-------\n");

    // message ID
    int MessageID = 0;
    memcpy(&MessageID, ptr, 2);
    ptr += 2;
    printf("Message ID : %d\n", MessageID);

    // size
    int nBytes = 0;
    memcpy(&nBytes, ptr, 2);
    ptr += 2;
    printf("Byte count : %d\n", nBytes);

    if (MessageID == 7) {   // FRAME OF MOCAP DATA packet
        // frame number
        int frameNumber = 0;
        memcpy(&frameNumber, ptr, 4);
        ptr += 4;
        //printf("Frame # : %d\n", frameNumber);

        // number of data sets (markersets, rigidbodies, etc)
        int nMarkerSets = 0;
        memcpy(&nMarkerSets, ptr, 4);
        ptr += 4;
        //printf("Marker Set Count : %d\n", nMarkerSets);

        for (int i = 0; i < nMarkerSets; i++) {
            // Markerset name
            char szName[256];
            strcpy(szName, ptr);
            int nDataBytes = (int) strlen(szName) + 1;
            ptr += nDataBytes;
            //printf("Model Name: %s\n", szName);

            // marker data
            int nMarkers = 0;
            memcpy(&nMarkers, ptr, 4);
            ptr += 4;
            //printf("Marker Count : %d\n", nMarkers);

            for (int j = 0; j < nMarkers; j++) {
                float x = 0;
                memcpy(&x, ptr, 4);
                ptr += 4;
                float y = 0;
                memcpy(&y, ptr, 4);
                ptr += 4;
                float z = 0;
                memcpy(&z, ptr, 4);
                ptr += 4;
                //printf("\tMarker %d : [x=%3.2f,y=%3.2f,z=%3.2f]\n",j,x,y,z);
            }
        }

        // unidentified markers
        int nOtherMarkers = 0;
        memcpy(&nOtherMarkers, ptr, 4);
        ptr += 4;
        //printf("Unidentified Marker Count : %d\n", nOtherMarkers);
        for (int j = 0; j < nOtherMarkers; j++) {
            float x = 0.0f;
            memcpy(&x, ptr, 4);
            ptr += 4;
            float y = 0.0f;
            memcpy(&y, ptr, 4);
            ptr += 4;
            float z = 0.0f;
            memcpy(&z, ptr, 4);
            ptr += 4;
            //printf("\tMarker %d : pos = [%3.2f,%3.2f,%3.2f]\n",j,x,y,z);
        }

        // rigid bodies
        int nRigidBodies = 0;
        memcpy(&nRigidBodies, ptr, 4);
        ptr += 4;
        // printf("Rigid Body Count : %d\n", nRigidBodies);
        if (nRigidBodies > 1) {
            printf("Error:  Number of rigid bodies = %d\n", nRigidBodies);
        }

        // ** Just grab one rigid body for ROB 550 / Quadrotor positioning
        for (int j = 0; j < nRigidBodies; j++) {
            // rigid body position/orientation
            int ID;
            memcpy(&ID, ptr, 4);
            ptr += 4;
            float x = 0.0f;
            memcpy(&x, ptr, 4);
            ptr += 4;
            float y = 0.0f;
            memcpy(&y, ptr, 4);
            ptr += 4;
            float z = 0.0f;
            memcpy(&z, ptr, 4);
            ptr += 4;
            float qx = 0;
            memcpy(&qx, ptr, 4);
            ptr += 4;
            float qy = 0;
            memcpy(&qy, ptr, 4);
            ptr += 4;
            float qz = 0;
            memcpy(&qz, ptr, 4);
            ptr += 4;
            float qw = 0;
            memcpy(&qw, ptr, 4);
            ptr += 4;
            printf("ID : %d\n", ID);
            printf("pos: [%3.2f,%3.2f,%3.2f]\n", x, y, z);
            printf("ori: [%3.2f,%3.2f,%3.2f,%3.2f]\n", qx, qy,
                   qz, qw);

            // associated marker positions
            int nRigidMarkers = 0;
            memcpy(&nRigidMarkers, ptr, 4);
            ptr += 4;
            //printf("Marker Count: %d\n", nRigidMarkers);
            int nBytes = nRigidMarkers * 3 * sizeof(float);
            float *markerData = (float *)malloc(nBytes);
            memcpy(markerData, ptr, nBytes);
            ptr += nBytes;

            if (major >= 2) {
                // associated marker IDs
                nBytes = nRigidMarkers * sizeof(int);
                int *markerIDs = (int *)malloc(nBytes);
                memcpy(markerIDs, ptr, nBytes);
                ptr += nBytes;

                // associated marker sizes
                nBytes = nRigidMarkers * sizeof(float);
                float *markerSizes = (float *)malloc(nBytes);
                memcpy(markerSizes, ptr, nBytes);
                ptr += nBytes;

                /*
                for(int k=0; k < nRigidMarkers; k++)
                      {
                printf("\tMarker %d: id=%d\tsize=%3.1f\tpos=[%3.2f,%3.2f,%3.2f]\n", k, markerIDs[k], markerSizes[k], markerData[k*3], markerData[k*3+1],markerData[k*3+2]);
                      }
                */
                if (markerIDs)
                    free(markerIDs);
                if (markerSizes)
                    free(markerSizes);

            } else {
                for (int k = 0; k < nRigidMarkers; k++) {
                    printf("\tMarker %d: pos = [%3.2f,%3.2f,%3.2f]\n", k, markerData[k * 3], markerData[k * 3 + 1], markerData[k * 3 + 2]);
                }
            }
            if (markerData)
                free(markerData);

            if (major >= 2) {
                // Mean marker error
                float fError = 0.0f;
                memcpy(&fError, ptr, 4);
                ptr += 4;
                printf("Mean marker error: %3.2f\n", fError);
            }

            // 2.6 and later
            if (((major == 2) && (minor >= 6)) || (major > 2) || (major == 0)) {
                // params
                short params = 0;
                memcpy(&params, ptr, 2);
                ptr += 2;
            }

        } // next rigid body


        // skeletons (version 2.1 and later)
        if (((major == 2) && (minor > 0)) || (major > 2)) {
            int nSkeletons = 0;
            memcpy(&nSkeletons, ptr, 4);
            ptr += 4;
            printf("Skeleton Count : %d\n", nSkeletons);
            for (int j = 0; j < nSkeletons; j++) {
                // skeleton id
                int skeletonID = 0;
                memcpy(&skeletonID, ptr, 4);
                ptr += 4;
                // # of rigid bodies (bones) in skeleton
                int nRigidBodies = 0;
                memcpy(&nRigidBodies, ptr, 4);
                ptr += 4;
                printf("Rigid Body Count : %d\n", nRigidBodies);
                for (int j = 0; j < nRigidBodies; j++) {
                    // rigid body pos/ori
                    int ID = 0;
                    memcpy(&ID, ptr, 4);
                    ptr += 4;
                    float x = 0.0f;
                    memcpy(&x, ptr, 4);
                    ptr += 4;
                    float y = 0.0f;
                    memcpy(&y, ptr, 4);
                    ptr += 4;
                    float z = 0.0f;
                    memcpy(&z, ptr, 4);
                    ptr += 4;
                    float qx = 0;
                    memcpy(&qx, ptr, 4);
                    ptr += 4;
                    float qy = 0;
                    memcpy(&qy, ptr, 4);
                    ptr += 4;
                    float qz = 0;
                    memcpy(&qz, ptr, 4);
                    ptr += 4;
                    float qw = 0;
                    memcpy(&qw, ptr, 4);
                    ptr += 4;
                    printf("ID : %d\n", ID);
                    printf("pos: [%3.2f,%3.2f,%3.2f]\n", x, y, z);
                    printf("ori: [%3.2f,%3.2f,%3.2f,%3.2f]\n", qx, qy, qz, qw);

                    // associated marker positions
                    int nRigidMarkers = 0;
                    memcpy(&nRigidMarkers, ptr, 4);
                    ptr += 4;
                    printf("Marker Count: %d\n", nRigidMarkers);
                    int nBytes = nRigidMarkers * 3 * sizeof(float);
                    float *markerData = (float *)malloc(nBytes);
                    memcpy(markerData, ptr, nBytes);
                    ptr += nBytes;

                    // associated marker IDs
                    nBytes = nRigidMarkers * sizeof(int);
                    int *markerIDs = (int *)malloc(nBytes);
                    memcpy(markerIDs, ptr, nBytes);
                    ptr += nBytes;

                    // associated marker sizes
                    nBytes = nRigidMarkers * sizeof(float);
                    float *markerSizes = (float *)malloc(nBytes);
                    memcpy(markerSizes, ptr, nBytes);
                    ptr += nBytes;

                    for (int k = 0; k < nRigidMarkers; k++) {
                        printf("\tMarker %d: id=%d\tsize=%3.1f\tpos=[%3.2f,%3.2f,%3.2f]\n", k, markerIDs[k], markerSizes[k], markerData[k * 3], markerData[k * 3 + 1], markerData[k * 3 + 2]);
                    }

                    // Mean marker error (2.0 and later)
                    if (major >= 2) {
                        float fError = 0.0f;
                        memcpy(&fError, ptr, 4);
                        ptr += 4;
                        printf("Mean marker error: %3.2f\n", fError);
                    }

                    // Tracking flags (2.6 and later)
                    if (((major == 2) && (minor >= 6)) || (major > 2) || (major == 0)) {
                        // params
                        short params = 0;
                        memcpy(&params, ptr, 2);
                        ptr += 2;
                    }

                    // release resources
                    if (markerIDs)
                        free(markerIDs);
                    if (markerSizes)
                        free(markerSizes);
                    if (markerData)
                        free(markerData);
                } // next rigid body

            } // next skeleton
        }

        // labeled markers (version 2.3 and later)
        if (((major == 2) && (minor >= 3)) || (major > 2)) {
            int nLabeledMarkers = 0;
            memcpy(&nLabeledMarkers, ptr, 4);
            ptr += 4;
            printf("Labeled Marker Count : %d\n", nLabeledMarkers);
            for (int j = 0; j < nLabeledMarkers; j++) {
                int ID = 0;
                memcpy(&ID, ptr, 4);
                ptr += 4;
                float x = 0.0f;
                memcpy(&x, ptr, 4);
                ptr += 4;
                float y = 0.0f;
                memcpy(&y, ptr, 4);
                ptr += 4;
                float z = 0.0f;
                memcpy(&z, ptr, 4);
                ptr += 4;
                float size = 0.0f;
                memcpy(&size, ptr, 4);
                ptr += 4;

                // 2.6 and later
                if (((major == 2) && (minor >= 6)) || (major > 2) || (major == 0)) {
                    // marker params
                    short params = 0;
                    memcpy(&params, ptr, 2);
                    ptr += 2;
                }

                printf("ID  : %d\n", ID);
                printf("pos : [%3.2f,%3.2f,%3.2f]\n", x, y, z);
                printf("size: [%3.2f]\n", size);
            }
        }

        // Force Plate data (version 2.9 and later)
        if (((major == 2) && (minor >= 9)) || (major > 2)) {
            int nForcePlates;
            memcpy(&nForcePlates, ptr, 4);
            ptr += 4;
            for (int iForcePlate = 0; iForcePlate < nForcePlates; iForcePlate++) {
                // ID
                int ID = 0;
                memcpy(&ID, ptr, 4);
                ptr += 4;
                printf("Force Plate : %d\n", ID);

                // Channel Count
                int nChannels = 0;
                memcpy(&nChannels, ptr, 4);
                ptr += 4;

                // Channel Data
                for (int i = 0; i < nChannels; i++) {
                    printf(" Channel %d : ", i);
                    int nFrames = 0;
                    memcpy(&nFrames, ptr, 4);
                    ptr += 4;
                    for (int j = 0; j < nFrames; j++) {
                        float val = 0.0f;
                        memcpy(&val, ptr, 4);
                        ptr += 4;
                        printf("%3.2f   ", val);
                    }
                    printf("\n");
                }
            }
        }

        // latency
        float latency = 0.0f;
        memcpy(&latency, ptr, 4);
        ptr += 4;
        printf("latency : %3.3f\n", latency);

        // timecode
        unsigned int timecode = 0;
        memcpy(&timecode, ptr, 4);
        ptr += 4;
        unsigned int timecodeSub = 0;
        memcpy(&timecodeSub, ptr, 4);
        ptr += 4;
        char szTimecode[128] = "";
        TimecodeStringify(timecode, timecodeSub, szTimecode, 128);

        // timestamp
        double timestamp = 0.0f;
        // 2.7 and later - increased from single to double precision
        if (((major == 2) && (minor >= 7)) || (major > 2)) {
            memcpy(&timestamp, ptr, 8);
            ptr += 8;
        } else {
            float fTemp = 0.0f;
            memcpy(&fTemp, ptr, 4);
            ptr += 4;
            timestamp = (double)fTemp;
        }

        // frame params
        short params = 0;
        memcpy(&params, ptr, 2);
        ptr += 2;
        // end of data tag
        int eod = 0;
        memcpy(&eod, ptr, 4);
        ptr += 4;
        printf("End Packet\n-------------\n");

    } else if (MessageID == 5) { // Data Descriptions
        // number of datasets
        int nDatasets = 0;
        memcpy(&nDatasets, ptr, 4);
        ptr += 4;
        printf("Dataset Count : %d\n", nDatasets);

        for (int i = 0; i < nDatasets; i++) {
            printf("Dataset %d\n", i);

            int type = 0;
            memcpy(&type, ptr, 4);
            ptr += 4;
            printf("Type : %d\n", type);

            if (type == 0) { // markerset
                // name
                char szName[256];
                strcpy(szName, ptr);
                int nDataBytes = (int) strlen(szName) + 1;
                ptr += nDataBytes;
                printf("Markerset Name: %s\n", szName);

                // marker data
                int nMarkers = 0;
                memcpy(&nMarkers, ptr, 4);
                ptr += 4;
                printf("Marker Count : %d\n", nMarkers);

                for (int j = 0; j < nMarkers; j++) {
                    char szName[256];
                    strcpy(szName, ptr);
                    int nDataBytes = (int) strlen(szName) + 1;
                    ptr += nDataBytes;
                    printf("Marker Name: %s\n", szName);
                }
            } else if (type == 1) { // rigid body
                if (major >= 2) {
                    // name
                    char szName[MAX_NAMELENGTH];
                    strcpy(szName, ptr);
                    ptr += strlen(ptr) + 1;
                    printf("Name: %s\n", szName);
                }

                int ID = 0;
                memcpy(&ID, ptr, 4);
                ptr += 4;
                printf("ID : %d\n", ID);

                int parentID = 0;
                memcpy(&parentID, ptr, 4);
                ptr += 4;
                printf("Parent ID : %d\n", parentID);

                float xoffset = 0;
                memcpy(&xoffset, ptr, 4);
                ptr += 4;
                printf("X Offset : %3.2f\n", xoffset);

                float yoffset = 0;
                memcpy(&yoffset, ptr, 4);
                ptr += 4;
                printf("Y Offset : %3.2f\n", yoffset);

                float zoffset = 0;
                memcpy(&zoffset, ptr, 4);
                ptr += 4;
                printf("Z Offset : %3.2f\n", zoffset);

            } else if (type == 2) { // skeleton
                char szName[MAX_NAMELENGTH];
                strcpy(szName, ptr);
                ptr += strlen(ptr) + 1;
                printf("Name: %s\n", szName);

                int ID = 0;
                memcpy(&ID, ptr, 4);
                ptr += 4;
                printf("ID : %d\n", ID);

                int nRigidBodies = 0;
                memcpy(&nRigidBodies, ptr, 4);
                ptr += 4;
                printf("RigidBody (Bone) Count : %d\n", nRigidBodies);

                for (int i = 0; i < nRigidBodies; i++) {
                    if (major >= 2) {
                        // RB name
                        char szName[MAX_NAMELENGTH];
                        strcpy(szName, ptr);
                        ptr += strlen(ptr) + 1;
                        printf("Rigid Body Name: %s\n", szName);
                    }

                    int ID = 0;
                    memcpy(&ID, ptr, 4);
                    ptr += 4;
                    printf("RigidBody ID : %d\n", ID);

                    int parentID = 0;
                    memcpy(&parentID, ptr, 4);
                    ptr += 4;
                    printf("Parent ID : %d\n", parentID);

                    float xoffset = 0;
                    memcpy(&xoffset, ptr, 4);
                    ptr += 4;
                    printf("X Offset : %3.2f\n", xoffset);

                    float yoffset = 0;
                    memcpy(&yoffset, ptr, 4);
                    ptr += 4;
                    printf("Y Offset : %3.2f\n", yoffset);

                    float zoffset = 0;
                    memcpy(&zoffset, ptr, 4);
                    ptr += 4;
                    printf("Z Offset : %3.2f\n", zoffset);
                }
            }

        }   // next dataset

        printf("End Packet\n-------------\n");

    } else {
        printf("Unrecognized Packet Type.\n");
    }
}

void Unpack_to_code(char *pData, struct optitrack_message *optmsg)
{
    char *ptr = pData;
    printf("Begin Packet\n-------\n");

    // message ID
    int MessageID = 0;
    memcpy(&MessageID, ptr, 2);
    ptr += 2;
    printf("Message ID : %d\n", MessageID);

    // size
    int nBytes = 0;
    memcpy(&nBytes, ptr, 2);
    ptr += 2;
    printf("Byte count : %d\n", nBytes);

    if (MessageID == 7) {   // FRAME OF MOCAP DATA packet
        // frame number
        int frameNumber = 0;
        memcpy(&frameNumber, ptr, 4);
        ptr += 4;
        //printf("Frame # : %d\n", frameNumber);

        // number of data sets (markersets, rigidbodies, etc)
        int nMarkerSets = 0;
        memcpy(&nMarkerSets, ptr, 4);
        ptr += 4;
        //printf("Marker Set Count : %d\n", nMarkerSets);

        for (int i = 0; i < nMarkerSets; i++) {
            // Markerset name
            char szName[256];
            strcpy(szName, ptr);
            int nDataBytes = (int) strlen(szName) + 1;
            ptr += nDataBytes;
            //printf("Model Name: %s\n", szName);

            // marker data
            int nMarkers = 0;
            memcpy(&nMarkers, ptr, 4);
            ptr += 4;
            //printf("Marker Count : %d\n", nMarkers);

            for (int j = 0; j < nMarkers; j++) {
                float x = 0;
                memcpy(&x, ptr, 4);
                ptr += 4;
                float y = 0;
                memcpy(&y, ptr, 4);
                ptr += 4;
                float z = 0;
                memcpy(&z, ptr, 4);
                ptr += 4;
                //printf("\tMarker %d : [x=%3.2f,y=%3.2f,z=%3.2f]\n",j,x,y,z);
            }
        }

        // unidentified markers
        int nOtherMarkers = 0;
        memcpy(&nOtherMarkers, ptr, 4);
        ptr += 4;
        //printf("Unidentified Marker Count : %d\n", nOtherMarkers);
        for (int j = 0; j < nOtherMarkers; j++) {
            float x = 0.0f;
            memcpy(&x, ptr, 4);
            ptr += 4;
            float y = 0.0f;
            memcpy(&y, ptr, 4);
            ptr += 4;
            float z = 0.0f;
            memcpy(&z, ptr, 4);
            ptr += 4;
            //printf("\tMarker %d : pos = [%3.2f,%3.2f,%3.2f]\n",j,x,y,z);
        }

        // rigid bodies
        int nRigidBodies = 0;
        memcpy(&nRigidBodies, ptr, 4);
        ptr += 4;
        // printf("Rigid Body Count : %d\n", nRigidBodies);
        if (nRigidBodies > 1) {
            printf("Error:  Number of rigid bodies = %d\n", nRigidBodies);
        }

        // ** Just grab one rigid body for ROB 550 / Quadrotor positioning
        for (int j = 0; j < nRigidBodies; j++) {
            // rigid body position/orientation
            memcpy(&(optmsg->ID), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->x), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->y), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->z), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->qx), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->qy), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->qz), ptr, 4);
            ptr += 4;
            memcpy(&(optmsg->qw), ptr, 4);
            ptr += 4;
            /*float x = 0.0f; memcpy(&x, ptr, 4); ptr += 4;
            float y = 0.0f; memcpy(&y, ptr, 4); ptr += 4;
            float z = 0.0f; memcpy(&z, ptr, 4); ptr += 4;
            float qx = 0; memcpy(&qx, ptr, 4); ptr += 4;
            float qy = 0; memcpy(&qy, ptr, 4); ptr += 4;
            float qz = 0; memcpy(&qz, ptr, 4); ptr += 4;
            float qw = 0; memcpy(&qw, ptr, 4); ptr += 4; */
            printf("ID : %d\n", optmsg->ID);
            printf("pos: [%3.2f,%3.2f,%3.2f]\n", optmsg->x, optmsg->y, optmsg->z);
            printf("ori: [%3.2f,%3.2f,%3.2f,%3.2f]\n", optmsg->qx, optmsg->qy,
                   optmsg->qz, optmsg->qw);
        }
    } else {
        printf("Unrecognized Packet Type.\n");
    }
}
