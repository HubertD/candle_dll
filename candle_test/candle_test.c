// CandleTest.cpp : Defines the entry point for the console application.
//

#include <candle.h>

#include "cmd_line.h"

#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <stdlib.h>
#include <Windows.h>

// Get rid of annoying sscanf warnings
#pragma warning (disable: 4996)

double hi_res_time()
{
	static double period = 0;
	static LARGE_INTEGER start_count;
	LARGE_INTEGER count;

	QueryPerformanceCounter(&count);

	if (period == 0){
		LARGE_INTEGER freq;
		if (QueryPerformanceFrequency(&freq)){
			period = 1.0 / (double)freq.QuadPart;
		}
		start_count = count;
	}

	return (double)(count.QuadPart - start_count.QuadPart) * period;
}

typedef struct{
	double now;
	double last_time;
	double time_constant;
	double dt;
	double rate;
	double filtered_rate;
} filtered_rate_t;


double calc_filtered_rate(filtered_rate_t *filt,double now,unsigned num)
{
	filt->now = now;

	if (filt->last_time == 0){
		filt->last_time = now;
		filt->filtered_rate = 0;

		if (filt->time_constant <= 0)
			filt->time_constant = 1;

		return filt->filtered_rate;
	}

	filt->dt = filt->now - filt->last_time;
	filt->last_time = filt->now;

	if (filt->dt <= 0)
		return filt->filtered_rate;

	filt->rate = num / filt->dt;

	double time_constant = filt->time_constant;
	// Prevent filter from going unstable if dt is too large
	if (filt->dt > time_constant)
		time_constant = filt->dt;

	// Single pole low pass iir filter
	filt->filtered_rate += (filt->rate - filt->filtered_rate) * filt->dt / time_constant;

	return filt->filtered_rate;
}


void print_can_frame(candle_frame_t *pframe,bool timestamp)
{
	int i,count;

	if (timestamp)
		printf("%9.4f", pframe->timestamp_us / 1000000.0);
	
	printf("%8x %1d ",pframe->can_id,pframe->can_dlc);

	count = pframe->can_dlc;
	if (count > 8)
		count = 8;
	for (i=0; i < count; ++i){
		printf("%02x ", pframe->data[i]);
	}
}

void close_candle(candle_list_handle list, candle_handle hdev)
{
	if (hdev)
		candle_dev_close(hdev);

	if (list)
		candle_list_free(list);
}



bool frame_id_match(candle_frame_t *frame, cmd_line_args_t *args)
{
	int i;

	if (args->num_canbus_ids == 0)
		return true;

	for (i = 0; i < args->num_canbus_ids; ++i){
		if (frame->can_id == args->canbus_ids[i])
			return true;
	}

	return false;
}

int main(int argc, char* argv[])
{
	bool rval = 1;
	candle_list_handle list = NULL;
	candle_handle hdev = NULL;
	unsigned count;
	filtered_rate_t tx_rate, rx_rate;
	double last_print_time = 0;
	double last_send_time = 0;
	cmd_line_args_t args;
	candle_err_t err;

	memset(&tx_rate, 0, sizeof(tx_rate));
	memset(&rx_rate, 0, sizeof(rx_rate));

	set_default_args(&args);

	if (!parse_args(argc, argv, &args))
		return 1;

	candle_device_mode_flags_t device_mode_flags = 0;

	if (args.timestamp)
		device_mode_flags |= CANDLE_MODE_HW_TIMESTAMP;

	if (args.pad_pkts_to_max_pkt_size)
		device_mode_flags |= CANDLE_MODE_PAD_PKTS_TO_MAX_PKT_SIZE;

	printf("Starting can device %u channel %u at bitrate %d with print rate %.3f secs\n",
		args.device_number, args.device_channel, args.print_period);

	printf("Device mode flags: ");

	if (device_mode_flags & CANDLE_MODE_HW_TIMESTAMP)
		printf("timestamp ");

	if (device_mode_flags & CANDLE_MODE_PAD_PKTS_TO_MAX_PKT_SIZE)
		printf("pad_pkts_to_max_pkt_size");

	printf("\n");

	err = candle_init_single_device(args.device_number, args.device_channel, args.bitrate,
		device_mode_flags, &list, &hdev);

	if (err != CANDLE_ERR_OK)
		goto done;

	if (args.num_send_frames > 0){
		int i;
		printf("Send Frames:\n");
		for (i = 0; i < args.num_send_frames; ++i){
			printf("#%2d: ",i);
			print_can_frame(&args.send_frames[i], false);
			printf("\n");
		}
	}

	printf("Hit esc to exit, s to send frame\n");
	
	bool got_frame = false;
	last_send_time = hi_res_time();
	int next_send_frame = 0;

	for (count = 0; 1 ;++count){
		candle_frame_t frame,match_frame;
		double now;

		if (_kbhit())
		{
			char c = _getch();
			if (tolower(c) == 0x1b)
				break;

			if (tolower(c) == 's'){
				if (args.num_send_frames > 0){
					printf("Sending:\n");
					for (int i = 0; i < args.num_send_frames; ++i){
						candle_frame_send(hdev, args.device_channel, &args.send_frames[i]);
						now = hi_res_time();
						calc_filtered_rate(&tx_rate, now, 1);

						printf("#%2d: ",i);
						print_can_frame(&args.send_frames[i], false);
						printf("\n");
					}
				}
				else{
					printf("\nNo send packet on command line\n");
				}
			}
		}

		err = CANDLE_ERR_OK;
		if (candle_frame_read(hdev, &frame, (args.num_send_frames > 0 ? 1 : 100))){
			if (frame_id_match(&frame,&args)){
				now = hi_res_time();
				calc_filtered_rate(&rx_rate, now, 1);
				match_frame = frame;
				got_frame = 1;
			}
		}
		else
		{
			err = candle_dev_last_error(hdev);
			if (err != CANDLE_ERR_READ_TIMEOUT){
				printf("Can't read frame on device\n");
				goto done;
			}
		}

		if (args.send_rate > 0 && args.num_send_frames > 0){
			double dt;
			double period = 1.0 / args.send_rate;
			int i;

			for (i = 0; i < 2;++i)
			{
				now = hi_res_time();

				dt = now - last_send_time;

				if (dt > period){
					if (next_send_frame >= args.num_send_frames - 1)
						next_send_frame = 0;

					if (candle_frame_send(hdev, args.device_channel, &args.send_frames[next_send_frame])){
						calc_filtered_rate(&tx_rate, now, 1);
						next_send_frame++;
					}
					last_send_time += period;
				}
				else{
					break;
				}
			}
		}

		now = hi_res_time();
		if ((now - last_print_time) >= args.print_period)
		{
			if (got_frame || args.print_period > 0)
				printf("rx: %6.1f tx:%6.1f ", rx_rate.filtered_rate, tx_rate.filtered_rate);

			if (got_frame)
				print_can_frame(&frame, args.timestamp);

			if (got_frame || args.print_period > 0){
				printf("\n");
				last_print_time = now;
			}
			got_frame = false;
		}

		now = hi_res_time();
		calc_filtered_rate(&rx_rate, now, 0);
		calc_filtered_rate(&tx_rate, now, 0);
	}

	err = CANDLE_ERR_OK;

done:
	if (err == CANDLE_ERR_OK)
		printf("\nEverything OK\n");
	else
		printf("\nCandle usage: %s\n", candle_error_text(err));

	close_candle(list, hdev);
	return (err == CANDLE_ERR_OK ? 0 : 1);
}

