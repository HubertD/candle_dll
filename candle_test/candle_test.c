// CandleTest.cpp : Defines the entry point for the console application.
//

#include <candle.h>
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


void print_can_frame(candle_frame_t *pframe)
{
	int i,count;
	printf("%d %8x %1d ",pframe->channel, pframe->can_id,pframe->can_dlc);

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


void usage(const char *prog,const char *err)
{
}

typedef struct{
	unsigned device_number;
	unsigned device_channel;
	unsigned bitrate;

	int canbus_id;

	double print_period;

	candle_frame_t send_frame;
	bool do_send_frame;
	double send_rate;
}cmd_args_t;

bool parse_arg(int argc, char *argv[], int i, void *pval, char *format,char *name)
{
	if (i == argc - 1){
		printf("Missing %s\n", name);
		return false;
	}
	i++;
	if (sscanf(argv[i], format, pval) != 1){
		printf("Invalid %s: %s\n", name, argv[i]);
		return false;
	}
	return true;
}

bool parse_frame_arg(int argc, char *argv[], int *ip, candle_frame_t *frame)
{
	int i = *ip;
	int byte_count;

	if (i == argc - 1){
		printf("Missing canbus id\n");
		return false;
	}

	i++;

	if (sscanf(argv[i], "%x", &frame->can_id) != 1){
		printf("Invalid canbus id: %s\n", argv[i]);
		*ip = i;
		return false;
	}

	i++;

	for (byte_count = 0; i < argc && argv[i][0] != '-'; ++byte_count, ++i){
		if (byte_count >= 8){
			printf("Too many can data bytes, max 8\n");
			*ip = i;
			return false;
		}

		if (sscanf(argv[i], "%hhx", &frame->data[byte_count]) != 1){
			printf("Invalid can data byte #%d: %s\n", byte_count,argv[i]);
			*ip = i;
			return false;
		}
	}

	frame->can_dlc = byte_count;
	i--;
	*ip = i;
	return true;
}

bool parse_args(int argc, char *argv[], cmd_args_t *args)
{
	int i;

	if (!args)
		return false;

	memset(args, 0, sizeof(*args));
	args->bitrate = 250000;
	args->canbus_id = -1;
	args->send_rate = 0;

	for (i = 0; i < argc; ++i)
	{
		if (argv[i][0] == '-'){
			switch (tolower(argv[i][1])){
			case 'b':
				if (!parse_arg(argc, argv, i, &args->bitrate, "%u", "bitrate"))
					goto error;
				i++;
				break;

			case 'c':
				if (!parse_arg(argc, argv, i, &args->device_channel, "%u", "device channel"))
					goto error;
				i++;
				break;

			case 'i':
				if (!parse_arg(argc, argv, i, &args->canbus_id, "%x", "canbus id"))
					goto error;
				i++;
				break;

			case 'n':
				if (!parse_arg(argc, argv, i, &args->device_number, "%u", "device number"))
					goto error;
				i++;
				break;

			case 'p':
				if (!parse_arg(argc, argv, i, &args->print_period, "%lf", "print period"))
					goto error;
				i++;
				break;

			case 'r':
				if (!parse_arg(argc, argv, i, &args->send_rate, "%lf", "send rate"))
					goto error;
				i++;
				break;

			case 's':
				if (!parse_frame_arg(argc, argv, &i, &args->send_frame))
					goto error;
				args->do_send_frame = true;
				break;

			default:
				printf("Invalid arg %s\n", argv[i]);
				goto error;
				break;
			}
		}
	}

	return true;

error:
	printf("Usage: %s <options>\nOptions:", argv[0]);
	printf("\t-b bitrate (default %d)\n", args->bitrate);
	printf("\t-c device_channel (default %d)\n", args->device_channel);
	printf("\t-i canbus_id in hex print only frames from this id, default off\n");
	printf("\t-n device_number (default %d)\n", args->device_number);
	printf("\t-p print_period (default %0.1f seconds)\n", args->print_period);
	printf("\t-s canbus_id(hex) <byte(hex) byte(hex) ...> Frame to send (default none)\n");
	printf("\t-r send_rate (default %0.1f frames/sec)\n", args->send_rate);

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
	cmd_args_t args;
	candle_err_t err;

	memset(&tx_rate, 0, sizeof(tx_rate));
	memset(&rx_rate, 0, sizeof(rx_rate));

	if (!parse_args(argc, argv, &args))
		return 1;

	printf("Starting can device %u channel %u at bitrate %d with print rate %.3f secs\n",
		args.device_number, args.device_channel, args.print_period);

	if (args.do_send_frame){
		printf("Send Frame is:");
		print_can_frame(&args.send_frame);
		printf("\n");
	}

	err = candle_init_single_device(args.device_number, args.device_channel, args.bitrate,
		CANDLE_MODE_PAD_PACKETS_TO_MAX_PACKET_SIZE, &list, &hdev);

	if (err != CANDLE_ERR_OK)
		goto done;

	printf("Hit q to exit, s to send frame\n");
	
	bool got_frame = false;
	last_send_time = hi_res_time();
	for (count = 0; 1 ;++count){
		candle_frame_t frame;
		double now;

		if (_kbhit())
		{
			char c = _getch();
			if (tolower(c) == 'q')
				break;

			if (tolower(c) == 's'){
				if (args.do_send_frame){
					candle_frame_send(hdev, args.device_channel, &args.send_frame);
					printf("Sent: ");
					print_can_frame(&args.send_frame);
					printf("\n");
					now = hi_res_time();
					calc_filtered_rate(&tx_rate, now, 1);
				}
				else{
					printf("\nNo send packet on command line\n");
				}
			}
		}

		err = CANDLE_ERR_OK;
		if (candle_frame_read(hdev, &frame, (args.do_send_frame ? 1 : 1000))){
			if (args.canbus_id == -1 || frame.can_id == args.canbus_id){
				got_frame = true;

				now = hi_res_time();
				calc_filtered_rate(&rx_rate, now, 1);
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

		if (args.send_rate > 0 && args.do_send_frame){
			double dt;
			double period = 1.0 / args.send_rate;
			int i;

			for (i = 0; i < 2;++i)
			{
				now = hi_res_time();

				dt = now - last_send_time;

				if (dt > period){
					if (candle_frame_send(hdev, args.device_channel, &args.send_frame)){
						calc_filtered_rate(&tx_rate, now, 1);
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
			if (got_frame){
				if (args.canbus_id == -1 || frame.can_id == args.canbus_id){
					printf("Fr:");
					print_can_frame(&frame);
				}
			}
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
		printf("\nCandle Error: %s\n", candle_error_text(err));

	close_candle(list, hdev);
	return (err == CANDLE_ERR_OK ? 0 : 1);
}

