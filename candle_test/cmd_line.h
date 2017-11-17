#pragma once

#include <candle.h>

#define ARG_ARRAY_SIZE 100

typedef struct{
	unsigned device_number;
	unsigned device_channel;
	unsigned bitrate;

	double print_period;

	int canbus_ids[ARG_ARRAY_SIZE];
	int num_canbus_ids;

	candle_frame_t send_frames[ARG_ARRAY_SIZE];
	int num_send_frames;

	double send_rate;

	bool timestamp;
	bool pad_pkts_to_max_pkt_size;
}cmd_line_args_t;


void set_default_args(cmd_line_args_t *args);

bool parse_args(int argc, char *argv[], cmd_line_args_t *args);


