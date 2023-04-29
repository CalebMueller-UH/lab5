/*
    debug.h
    central file for turning on/off debug statements for individual elements
*/

#ifdef DEBUG

// #define HOST_DEBUG
// #define HOST_MANMSG_DEBUG
#define HOST_DEBUG_PACKET_RECEIPT

#define JOB_DEBUG

// #define NAMESERVER_DEBUG

#define PACKET_DEBUG

// #define SOCKET_DEBUG

// #define SWITCH_DEBUG
#define SWITCH_DEBUG_CONTROL
#define SWITCH_DEBUG_CONTROL_MSG
#define SWITCH_DEBUG_CONTROL_UPDATE
// #define SWITCH_DEBUG_ROUTINGTABLE
#define SWITCH_DEBUG_PACKET_RECEIPT

// #define NAMESERVER_DEBUG
#define NAMESERVER_DEBUG_PACKET_RECEIPT

#endif