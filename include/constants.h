/*
constants.h
*/

#pragma once

#define MAX_FILENAME_LENGTH 100
#define MAX_DOMAIN_NAME_LENGTH 99
#define MAX_MSG_LENGTH 1000
#define LOOP_SLEEP_TIME_MS 10000
#define PACKET_PAYLOAD_MAX 100
#define TIMETOLIVE 10  // 10 * 10ms = 100ms

// Number of digits used in creation of a random request identifier
#define TICKETLEN 4

// The number of payload space available after including a response ticket,
// delimiter, and terminator
#define MAX_RESPONSE_LEN (PACKET_PAYLOAD_MAX - 2 - TICKETLEN)