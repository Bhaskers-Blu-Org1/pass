// author john.d.sheehan@ie.ibm.com

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "macros.h"
#include "pass.h"


const char *cmd_options_available = "c:e:h:o:p:r:s:u:v:";

const char *cmd_options_help = "\
-c: channels (number of channels, default 1)\n\
-e: endian swap (0 - no swap, 1 - swap, default 0)\n\
-h: has header (0 - no header, 1 - header, default 1)\n\
-o: origin ip (default 127.0.0.1)\n\
-p: port number (default 1234)\n\
-r: sample rate (default 500000)\n\
-s: sensors (number of sensors, default 1)\n\
-u: url (url octave bands are posted to, default http://localhost:5100/data)\n\
-v: verbose\n";

const char *sample_usage = "\
sample args:\n\
listen to localhost:1234 for 1 sensor with 2 channels of endian swapped data containing header with sample rate 10000 posting octave bands to http://localhost:5100/data: -o localhost -p 1234 -s 1 -c 2 -e 1 -h 1 -r 10000 -b 'http://localhost:5100/data'\n";

struct cmd_options {
	int channels;
	int endian_swap;
	int has_header;

	int sample_rate;
	int sensors;
	int verbose;

	char port_number[16];
	char server_name[256];
	char url[256];
};

static void cmd_options_init(struct cmd_options *cmd) {
	cmd->channels = 1;
	cmd->endian_swap = 0;
	cmd->has_header = 1;

	cmd->sample_rate = 500000;
	cmd->sensors = 1;
	cmd->verbose = 0;

	memset(cmd->port_number, '\0', sizeof(cmd->port_number));
	strcpy(cmd->port_number, "1234");

	memset(cmd->server_name, '\0', sizeof(cmd->server_name));
	strcpy(cmd->server_name, "127.0.0.1");

	memset(cmd->url, '\0', sizeof(cmd->url));
	strcpy(cmd->url, "http://localhost:5100/data");
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

			case 'u':
				if (strlen(optarg) < 255) {
					strcpy(cmd->url, optarg);
				}
				break;

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
	flush(stdout, "[e] endian swap  : %d (%s)", cmd->endian_swap, (cmd->endian_swap == 1 ? "yes" : "no"));

	flush(stdout, "[h] has header   : %d (%s)", cmd->has_header, (cmd->has_header == 1 ? "yes" : "no"));
	flush(stdout, "[o] origin ip    : %s", cmd->server_name);
	flush(stdout, "[p] port number  : %s", cmd->port_number);

	flush(stdout, "[r] sample rate  : %d", cmd->sample_rate);
	flush(stdout, "[s] sensors      : %d", cmd->sensors);
	flush(stdout, "[u] url	  : %s", cmd->url);
	flush(stdout, "[v] verbose      : %d (%s)", cmd->verbose, (cmd->verbose == 1 ? "yes" : "no"));
}

static volatile int proceed = 1;
static volatile int result = 0;

void leave(int sig) {
	proceed = 0;
	result = sig;
}

int main(int argc, char **argv) {
	struct cmd_options cmd;

	cmd_options_init(&cmd);
	cmd_options_parse(&cmd, argc, argv);
	cmd_options_print(&cmd);

	signal(SIGINT, leave);
	signal(SIGTERM, leave);

	double gradient = 1.0;
	double offset = 0.0;

	pass_response pr;

	pass_context pc;
	pr = pass_context_init(&pc, cmd.sensors, cmd.channels, cmd.sample_rate, cmd.has_header);
	exit_failure_if(pr != PASS_SUCCESS, "failed to init pass_context");

	pass_array *values;
	values = malloc(sizeof(pass_array) * pc.sensor_count * pc.channel_count);
	exit_failure_if(values == NULL, "failed to allocate memory");
	for (int i = 0; i < (pc.sensor_count * pc.channel_count); i++) {
		pr = pass_array_allocate(&values[i], pc.sample_rate);
		exit_failure_if(pr != PASS_SUCCESS, "failed to allocate memory");
	}

	pass_fftw_plan pass_plan;
	pr = pass_fftw_plan_init(&pass_plan, pc.sample_rate);
	exit_failure_if(pr != PASS_SUCCESS, "failed to allocate memory");

	pr = pass_curl_init();
	exit_failure_if(pr != PASS_SUCCESS, "failed to init curl");

	char *name_buffer = malloc(128);
	exit_failure_if(name_buffer == NULL, "failed to allocate memory");

	pr = pass_connect(&pc, cmd.server_name, cmd.port_number);
	exit_failure_if(pr != PASS_SUCCESS, "failed to connect");

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
				pass_array *v = &values[k];

				k++;

				pass_convert_to_doubles(v, &pc, i, j, gradient, offset);
				pass_fftw_execute(v, &pass_plan);
				pass_octave_bands(v, 10, 36);
				pass_decibels(v, 1.0, 0.0);

				memset(name_buffer, '\0', 128);
				snprintf(name_buffer, 127, "Sensor %d, Channel %d", i, j);

				pass_curl_post(
					cmd.url,
					v,
					name_buffer,
					"octavebands",
					i,
					j);
			}
		}
	}

	pass_close(&pc);

	free(name_buffer);

	pr = pass_curl_term();
	exit_failure_if(pr != PASS_SUCCESS, "failed to release curl");

	pr = pass_fftw_plan_term(&pass_plan);
	exit_failure_if(pr != PASS_SUCCESS, "failed to release memory");

	for (int i = 0; i < (pc.sensor_count * pc.channel_count); i++ ) {
		pr = pass_array_free(&values[i]);
		exit_failure_if(pr != PASS_SUCCESS, "failed to release memory");
	}
	free(values);

	pr = pass_context_free(&pc);
	exit_failure_if(pr != PASS_SUCCESS, "failed to release memory");

	return 0;
}
