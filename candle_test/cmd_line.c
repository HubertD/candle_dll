#include <candle.h>
#include "cmd_line.h"

#include <stdlib.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <ctype.h>

#pragma warning (disable: 4996)


static bool parse_arg(int argc, char *argv[], int i, void *pval, char *format, char *name)
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

static bool parse_frame_arg(int argc, char *argv[], int *ip, candle_frame_t *frame)
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
			printf("Invalid can data byte #%d: %s\n", byte_count, argv[i]);
			*ip = i;
			return false;
		}
	}

	frame->can_dlc = byte_count;
	i--;
	*ip = i;
	return true;
}

static bool usage(char *argv[])
{
	cmd_line_args_t args;

	set_default_args(&args);

	printf("Usage: candle_test <options>\nOptions:\n");
	printf("    -a argfile        Read arguments from argfile. # is a comment character\n");
	printf("    -b bitrate        (default %d)\n", args.bitrate);
	printf("    -c device_channel (default %d)\n", args.device_channel);
	printf("    -f                pad usb packets to full packet size for better speed\n");
	printf("                      (default off, firmware support on candle required)\n");
	printf("    -i canbus_id      only print frames from from these ids, multiples ok\n");
	printf("    -n device_number  (default %d)\n", args.device_number);
	printf("    -p print_period   (default %0.1f seconds)\n", args.print_period);
	printf("    -r send_rate      (default %0.1f frames/sec)\n", args.send_rate);
	printf("    -s canbus_id(hex) <byte(hex) byte(hex) ...> add this can frame to send list\n");
	printf("    -t                turn on timestamp mode (default off)\n");

	return false;
}

static bool parse_args_file(char *args_file_name, cmd_line_args_t *args)
{
	FILE *ifile;
	char *str, arg_string[1000];
	char **argv = NULL;
	int i, argc;
	bool rval = false;

	ifile = fopen(args_file_name, "r");

	if (!ifile){
		printf("Can't open args file %s\n", args_file_name);
		return false;
	}

	argv = malloc(sizeof(*argv));
	if (!argv){
		printf("Can't alloc memory in read_args_file\n");
		goto done;
	}

	argv[0] = "candle_test";

	printf("Reading args file %s\n", args_file_name);

	for (argc = 1; fscanf(ifile, "%998s", arg_string) == 1;){
		char **argv1;
		arg_string[999] = 0;

		// Skip to end of line and discard this arg if we get a comment character
		if (arg_string[0] == '#'){
			fscanf(ifile, "%*[\n]");
			continue;
		}

		argv1 = realloc(argv, (argc + 1)*sizeof(*argv));
		if (!argv1){
			printf("Can't alloc memory in read_args_file\n");
			goto done;
		}
		argv = argv1;

		str = malloc(strlen(arg_string) + 1);
		if (!str)
			goto done;

		strcpy(str, arg_string);
		argv[argc] = str;

		argc++;
	}

	rval = parse_args(argc, argv, args);

done:
	for (i = 1; i < argc; ++i){
		free(argv[i]);
	}
	if (argv)
		free(argv);

	return rval;
}

void set_default_args(cmd_line_args_t *args)
{
	memset(args, 0, sizeof(*args));
	args->bitrate = 250000;
	args->send_rate = 0;
}


bool parse_args(int argc, char *argv[], cmd_line_args_t *args)
{
	int i;

	if (!args)
		return false;

	for (i = 0; i < argc; ++i)
	{
		if (argv[i][0] == '-'){
			switch (tolower(argv[i][1])){
			case 'a':
			{
				char args_file_name[1000];
				if (!parse_arg(argc, argv, i, args_file_name, "%998s", "args file name"))
					return usage(argv);

				if (!parse_args_file(args_file_name, args)){
					return false;
				}
				break;
			}
			case 'b':
				if (!parse_arg(argc, argv, i, &args->bitrate, "%u", "bitrate"))
					return usage(argv);
				i++;
				break;

			case 'c':
				if (!parse_arg(argc, argv, i, &args->device_channel, "%u", "device channel"))
					return usage(argv);
				i++;
				break;

			case 'f':
				args->pad_pkts_to_max_pkt_size = true;
				break;

			case 'i':
				if (args->num_canbus_ids >= ARG_ARRAY_SIZE){
					printf("Too many canbus IDs, max %d\n", ARG_ARRAY_SIZE);
					return usage(argv);
				}
				if (!parse_arg(argc, argv, i, &args->canbus_ids[args->num_canbus_ids], "%x", "canbus id"))
					return usage(argv);
				args->num_canbus_ids++;
				i++;
				break;

			case 'n':
				if (!parse_arg(argc, argv, i, &args->device_number, "%u", "device number"))
					return usage(argv);
				i++;
				break;

			case 'p':
				if (!parse_arg(argc, argv, i, &args->print_period, "%lf", "print period"))
					return usage(argv);
				i++;
				break;

			case 'r':
				if (!parse_arg(argc, argv, i, &args->send_rate, "%lf", "send rate"))
					return usage(argv);
				i++;
				break;

			case 's':
				if (args->num_send_frames >= ARG_ARRAY_SIZE){
					printf("Too many send frames, max %d\n", ARG_ARRAY_SIZE);
					return usage(argv);
				}
				if (!parse_frame_arg(argc, argv, &i, &args->send_frames[args->num_send_frames]))
					return usage(argv);
				args->num_send_frames++;
				break;

			case 't':
				args->timestamp = true;
				break;

			case 'u':
				usage(argv);

				break;

			default:
				printf("Invalid arg %s\n", argv[i]);
				return usage(argv);
				break;
			}
		}
	}

	return true;
}

