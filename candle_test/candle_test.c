// CandleTest.cpp : Defines the entry point for the console application.
//

#include <candle.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <time.h>
#include <Windows.h>

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

double calc_filtered_fps(double now,unsigned num_frames)
{
	static double last_time = 0;
	static double filtered_fps = 0;
	const double time_constant = 1;
	double dt, fps;

	if (last_time == 0){
		last_time = now;
		return filtered_fps;
	}

	if (num_frames == 0)
		return filtered_fps;

	dt = now - last_time;

	last_time = now;

	if (dt <= 0)
		return filtered_fps;

	fps = num_frames / dt;


	// Prevent filter from going unstable if dt is too large
	if (dt > time_constant)
		dt = time_constant;

	// Single pole low pass iir filter
	filtered_fps += (fps - filtered_fps) * dt / time_constant;

	return filtered_fps;
}


void print_can_frame(candle_frame_t *pframe)
{
	int i;
	printf("%d %8x %1d ",pframe->channel, pframe->can_id,pframe->can_dlc);

	for (i = 0; i < pframe->can_dlc; ++i){
		printf("%02x ", pframe->data[i]);
	}
}

int init_candle(uint8_t device_num,uint8_t device_channel,uint32_t bitrate, candle_list_handle *plist, candle_handle *phdev)
{
	bool rval = 0;
	uint8_t len;
	candle_devstate_t state;
	uint8_t num_channels;
	candle_capability_t cap;

	if (!plist || *phdev)
		return 0;

	*plist = NULL;
	*phdev = NULL;

	if (!candle_list_scan(plist)){
		printf("Can't get candle list\n");
		goto done;
	}

	if (!candle_list_length(*plist, &len)){
		printf("Can't get candle list length\n");
		goto done;
	}

	if (len < device_num){
		printf("No device %d in candle list (max %d)\n",device_num,len);
		goto done;
	}

	if (!candle_dev_get(*plist, device_num, phdev)){
		printf("Can't get device #%d\n", device_num);
		goto done;
	}

	if (!candle_dev_open(*phdev)){
		printf("Can't open device #%d", device_num);
		goto done;
	}

	if (!candle_dev_get_state(*phdev, &state)){
		printf("Can't get state for device #%d\n", device_num);
		goto done;
	}

	if (state != CANDLE_DEVSTATE_AVAIL){
		printf("Candle device #%d in use\n", device_num);
		goto done;
	}

	if (!candle_channel_count(*phdev, &num_channels)){
		printf("Can't get channel count for device #%d\n", device_num);
		goto done;
	}

	if (num_channels <= device_channel){
		printf("No channel %d available on device #%d (max %d)\n", device_channel, device_num,num_channels-1);
		goto done;
	}

	if (!candle_channel_get_capabilities(*phdev, device_channel, &cap)){
		printf("Can't get capabilities for channel %d on device #%d\n",device_channel,device_num);
		goto done;
	}

	if (!candle_channel_set_bitrate(*phdev, device_channel, bitrate))
	{
		printf("Can't set bitrate to %d for channel #%d on device #%d\n", bitrate, device_channel, device_num);
		goto done;
	}

	if (!candle_channel_start(*phdev, device_channel, CANDLE_MODE_HW_TIMESTAMP)){
		printf("Can't start channel #%d on device #%d\n", device_channel, device_num);
		goto done;
	}

	rval = 1;

done:

	if (plist && !phdev){
		candle_list_free(*plist);
		*plist = NULL;
	}

	 return rval;
}

void close_candle(candle_list_handle list, candle_handle hdev)
{
	if (hdev)
		candle_dev_close(hdev);

	if (list)
		candle_list_free(list);
}



int main(int argc, char* argv[])
{
	bool rval = 1;
	candle_list_handle list = NULL;
	candle_handle hdev = NULL;
	uint32_t bitrate = 250000;
	unsigned count;
	double fps;
	uint8_t num = 0;
	uint8_t ch = 0;
	double last_print_time = 0;
	double print_time = 1;

	if (!init_candle(num, ch, bitrate,&list,&hdev)){
		goto done;
	}

	printf("Hit q to exit\n");
	for (count = 0; 1 ;++count){
		candle_frame_t frame;
		candle_err_t err = CANDLE_ERR_OK;
		double now;

		if (_kbhit())
		{
			char c = _getch();
			if (tolower(c) == 'q')
				break;
		}

		if (!candle_frame_read(hdev, &frame, 1000)){
			err = candle_dev_last_error(hdev);
			if (err != CANDLE_ERR_READ_TIMEOUT){
				printf("Can't read frame on device #%d, error %d\n", num, err);
				goto done;
			}
		}

		now = hi_res_time();

		fps = calc_filtered_fps(now,1);

		if ((now - last_print_time) >= print_time){
			last_print_time = now;
			printf("Fps: %6.1f ", fps);
			if (err == CANDLE_ERR_READ_TIMEOUT){
				printf("Timeout");
			}
			else{
				printf("Frame:");
				print_can_frame(&frame);
			}
			printf("\r");
		}
	}

	rval = 0;

done:
	close_candle(list, hdev);
	return rval;
}

