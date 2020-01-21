// author john.d.sheehan@ie.ibm.com

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "macros.h"
#include "pass.h"


const char *cmd_options_available = "c:d:e:h:o:p:r:s:v:";

const char *cmd_options_help = "\
-c: channels (number of channels, default 1)\n\
-d: duration (durationn of wav files, default 60 seconds)\n\
-e: endian swap (0 - no swap, 1 - swap, default 0)\n\
-h: has header (0 - no header, 1 - header, default 1)\n\
-o: origin ip (default 127.0.0.1)\n\
-p: port number (default 1234)\n\
-r: sample rate (default 500000)\n\
-s: sensors (number of sensors, default 1)\n\
-v: verbose\n";

const char *sample_usage = "\
sample args:\n\
listen to localhost:1234 for 1 sensor with 2 channels of endian swapped data containing header with sample rate 10000: -o localhost -p 1234 -s 1 -c 2 -e 1 -h 1 -r 10000\n";    

struct cmd_options {
	int channels;
	int duration;
	int endian_swap;
	int has_header;

	int sample_rate;
	int sensors;
	int verbose;

	char port_number[16];
	char server_name[256];
};

static void cmd_options_init(struct cmd_options *cmd) {
	cmd->channels = 1;
	cmd->duration = 60;
	cmd->endian_swap = 0;
	cmd->has_header = 1;

	cmd->sample_rate = 500000;
	cmd->sensors = 1;
	cmd->verbose = 0;

	memset(cmd->port_number, '\0', sizeof(cmd->port_number));
	strcpy(cmd->port_number, "1234");

	memset(cmd->server_name, '\0', sizeof(cmd->server_name));
	strcpy(cmd->server_name, "127.0.0.1");
}

static void cmd_options_parse(struct cmd_options *cmd, int argc, char **argv) {
	if ((argc == 2) &&
	    ((strcmp("-h", argv[1]) == 0) || (strcmp("--help", argv[1]) == 0))) {
		flush(stdout, "%s\n", cmd_options_help);
		flush(stdout, "%s\n", sample_usage);
		exit(EXIT_SUCCESS);
	}

	int c;
	while ((c = getopt(argc, argv, cmd_options_available)) != -1) {
		switch(c) {
			case 'c':  cmd->channels       = atoi(optarg);  break;
			case 'd':  cmd->duration       = atoi(optarg);  break;
			case 'e':  cmd->endian_swap    = atoi(optarg);  break;
			case 'h':  cmd->has_header     = atoi(optarg);  break;

			case 'o':
				if (strlen(optarg) < 255) {
					strcpy(cmd->server_name, optarg);
				}
				break;

			case 'p':
				if (strlen(optarg) < 15) {
					strcpy(cmd->port_number, optarg);
				}
				break;

			case 'r':  cmd->sample_rate    = atoi(optarg);  break;
			case 's':  cmd->sensors	= atoi(optarg);  break;
			case 'v':  cmd->verbose	= atoi(optarg);  break;
			default:
				flush(stdout, "%s\n", cmd_options_help);
				flush(stdout, "%s\n", sample_usage);
				exit(EXIT_SUCCESS);
		}
	}
}

static void cmd_options_print(struct cmd_options *cmd) {
	flush(stdout, "[c] channels     : %d", cmd->channels);
	flush(stdout, "[d] duration     : %d", cmd->duration);
	flush(stdout, "[e] endian swap  : %d (%s)", cmd->endian_swap, (cmd->endian_swap == 1 ? "yes" : "no"));

	flush(stdout, "[h] has header   : %d (%s)", cmd->has_header, (cmd->has_header == 1 ? "yes" : "no"));
	flush(stdout, "[o] origin ip    : %s", cmd->server_name);
	flush(stdout, "[p] port number  : %s", cmd->port_number);

	flush(stdout, "[r] sample rate  : %d", cmd->sample_rate);
	flush(stdout, "[s] sensors      : %d", cmd->sensors);
	flush(stdout, "[v] verbose      : %d (%s)", cmd->verbose, (cmd->verbose == 1 ? "yes" : "no"));
}

static volatile int proceed = 1;
static volatile int result = 0;

void leave(int sig) {
	proceed = 0;
	result = sig;
}

int main(int argc, char **argv){
	struct cmd_options cmd;

	cmd_options_init(&cmd);
	cmd_options_parse(&cmd, argc, argv);
	cmd_options_print(&cmd);

	signal(SIGINT, leave);
	signal(SIGTERM, leave);

	pass_response pr;

	pass_context pc;
	pr = pass_context_init(&pc, cmd.sensors, cmd.channels, cmd.sample_rate, cmd.has_header);
	exit_failure_if(pr != PASS_SUCCESS, "failed to init pass_context");

	pr = pass_connect(&pc, cmd.server_name, cmd.port_number);
	exit_failure_if(pr != PASS_SUCCESS, "failed to connect");

	pass_wav_description *wav_descriptions;
	wav_descriptions = malloc(sizeof(pass_wav_description) * pc.sensor_count * pc.channel_count);
	exit_failure_if((wav_descriptions == NULL), "failed to allocate memory");

	for (int i = 0, k = 0; i < pc.sensor_count; i++) {
		for (int j = 0; j < pc.channel_count; j++) {
			char base[64];
			memset(base, '\0', 64);
			snprintf(base, 63, "sensor%dchannel%d", i, j);

			pr = pass_wav_init(&wav_descriptions[k], "./", base, 32767.0, cmd.duration);
			exit_failure_if(pr != PASS_SUCCESS, "failed to allocate memory");
			k++;
		}
	}

	while ((proceed) &&
	      ((pr = pass_read(&pc)) == PASS_SUCCESS)) {

		if (cmd.has_header) {
			pr = pass_gaps_detection(&pc);
			if (pr != PASS_SUCCESS) {
				info(stdout, "gap detected");
				continue;
			}
		}

		if (cmd.endian_swap)
			pass_endian_swap(&pc);

		for (int i = 0, k = 0; i < pc.sensor_count; i++) {
			for (int j = 0; j < pc.channel_count; j++) {
				pass_wav_write(&pc, &wav_descriptions[k], i, j);
				k++;
			}
		}
	}

	pass_close(&pc);

	for (int i = 0; i < (pc.sensor_count * pc.channel_count); i++) {
		pr = pass_wav_term(&wav_descriptions[i]);
		exit_failure_if(pr != PASS_SUCCESS, "failed to release memory");
	}
	free(wav_descriptions);

	pr = pass_context_free(&pc);
	exit_failure_if(pr != PASS_SUCCESS, "failed to release memory");

	return 0;
}
