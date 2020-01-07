//================================================================================
// NOTE: This code is adapted from PacketClient.cpp, which remains in this folder.
// The license for the code is located in that file and duplicated below:
//
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


#ifndef OPTITRACK_OPTITRACK_HPP
#define OPTITRACK_OPTITRACK_HPP

#include <string>
#include <vector>

#define SOCKET int  // A sock handle is just an int in linux

#define MULTICAST_ADDRESS   "239.255.42.99"     // IANA, local network
#define PORT_COMMAND        1510
#define PORT_DATA           1511

/**
* optitrack_message_t defines the data for a rigid body transmitted by the Optitrack server. A rigid body has a unique
* id, a 3D position, and a quaternion defining the orientation.
*/
struct optitrack_message_t
{
    int id;     ///< Unique id for the rigid body being described
    float x;    ///< x-position in the Optitrack frame
    float y;    ///< y-position in the Optitrack frame
    float z;    ///< z-position in the Optitrack frame
    float qx;   ///< qx of quaternion
    float qy;   ///< qy of quaternion
    float qz;   ///< qz of quaternion
    float qw;   ///< qw of quaternion
};

/**
* guess_optitrack_network_interface tries to find the IP address of the interface to use for receiving Optitrack data.
* The strategy used will:
* 
*   1) Enumerate all possible network interfaces.
*   2) Ignore 'lo'
*   3) Either:
*       - Find the first interface that starts with the letter "w", which most likely indicates a wireless network.
*       - Or, return any interface found if no interface starts with "w".
* 
* \return   IP address of the multicast interface to use with optitrack
*/
std::string guess_optitrack_network_interface(void);

/**
* create_optitrack_data_socket creates a socket for receiving Optitrack data from the Optitrack server.
* 
* The data socket will receive data on the predefined MULTICAST_ADDRESS.
* 
* \param    interfaceIp         IP address of the interface that will be receiving Optitrack data
* \param    port                Port to which optitrack data is being sent
* \return   SOCKET handle to use for receiving data from the Optitrack server.
*/
SOCKET create_optitrack_data_socket(const std::string& interfaceIp, unsigned short port);

/**
* parse_optitrack_packet_into_messages parses the contents of a datagram received from the Optitrack server
* into a vector containing the 3D position + quaternion for every rigid body
*/
std::vector<optitrack_message_t> parse_optitrack_packet_into_messages(const char* packet, int size);

/**
* quaternion_to_yaw computes the yaw encoded by the quaternion returned by optitrack. The quaternion assumes XYZ 
* ordering and a Y-up configuration.
* 
* \param    msg         Optitrack message containing the rigid body quaternion
* \return   The yaw of the object being tracked.
*/
double quaternion_to_yaw(const optitrack_message_t& msg);


#endif // OPTITRACK_OPTITRACK_HPP
