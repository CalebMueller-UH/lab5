/*
constants.h
*/

#pragma once

// Maximum expected filename length
#define MAX_FILENAME_LENGTH 100

// Used for sockets
#define MAX_DOMAIN_NAME_LENGTH 99

// Maximum manager message length
#define MAX_MSG_LENGTH 1000

// How much time to sleep between between loop executions to simulate
// asynchronous execution (in microseconds)
#define LOOP_SLEEP_TIME_US 100000

// Largest allowable packet size of packet payload
#define PACKET_PAYLOAD_MAX 100

// Number of wait cycles a typical packet is assigned
// (Not necessarily tied to a measure of time)
#define TIMETOLIVE 10

#define STATIC_DNS_ID 100

// The number of payload space available after including
// a job id, the ':' delimiter, and a null terminator
#define MAX_RESPONSE_LEN (PACKET_PAYLOAD_MAX - 2 - JIDLEN)

// Maximum number of allowable Domain Name aliases
#define MAX_NUM_NAMES 255