// author john.d.sheehan@ie.ibm.com

#include <byteswap.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include "macros.h"


const char *cmd_options_available = "a:c:e:h:i:l:m:n:p:r:s:u:v:";

const char *cmd_options_help = "\
-a: amplitude (default 0.01)\n\
-c: channels (number of channels, default 1)\n\
-e: endian swap (0 - no swap, 1 - swap, default 0)\n\
-h: include header (0 - no header, 1 - header, default 1)\n\
-i: max iterations (default 1200)\n\
-l: lower frequency (default 128)\n\
-m: max amplitude (1.0)\n\
-n: port number (default 1234)\n\
-p: period (default 10)\n\
-r: sample rate (default 500000)\n\
-s: sensors (number of sensors, default 1)\n\
-u: upper frequency (default 8192)\n\
-v: print data buffer (0 - no print, 1 - print, default 0)\n";

const char *sample_usage = "\
sample args:\n\
endian swapped single sensor with single channel with sample rate 500000 on port 1234 switching between a lower frequency of 2048 and an upper frequency of 8192 every 10 seconds: -e 1 -s 1 -c 1 -r 500000 -n 1234 -l 2048 -u 8096 -p 10 \n";


struct cmd_options {
	int    endian_swap;
	int    header_use;
	int    verbose;

	int    sensors;
	int    channels;
	int    sample_rate;
	int    max_iterations;
	int    period;

	double amplitude;
	double max_amplitude;
	double frequency_l;
	double frequency_u;

	char port_number[16];
};


struct __attribute__ ((__packed__)) header {
	uint64_t magic;
	uint64_t id;
	uint64_t version;
	uint64_t timestamp;
	uint64_t reserved;
	int16_t  checksum;
};

static void cmd_options_init(struct cmd_options *cmd) {
	cmd->amplitude       = 0.01;
	cmd->channels        = 1;
	cmd->endian_swap     = 0;

	cmd->header_use      = 1;
	cmd->max_iterations  = 1200;
	cmd->frequency_l     = 256.0;
	cmd->max_amplitude   = 1.0;

	memset(cmd->port_number, '\0', sizeof(cmd->port_number));
	strcpy(cmd->port_number, "1234");

	cmd->period          = 10;
	cmd->sample_rate     = 500000;
	cmd->sensors         = 1;
	cmd->frequency_u     = 2048.0;
	cmd->verbose         = 0;
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
		switch (c) {
		case 'a':  cmd->amplitude      = atof(optarg);  break;
		case 'c':  cmd->channels       = atoi(optarg);  break;
		case 'e':  cmd->endian_swap    = atoi(optarg);  break;
		case 'h':  cmd->header_use     = atoi(optarg);  break;

		case 'l':  cmd->frequency_l    = atof(optarg);  break;
		case 'i':  cmd->max_iterations = atoi(optarg);  break;
		case 'm':  cmd->max_amplitude  = atof(optarg);  break;

		case 'n':
			if (strlen(optarg) < 15) {
				strcpy(cmd->port_number, optarg);
			}
			break;

		case 'r':  cmd->sample_rate    = atoi(optarg);  break;
		case 's':  cmd->sensors	       = atoi(optarg);  break;
		case 'p':  cmd->period	       = atoi(optarg);  break;
		case 'u':  cmd->frequency_u    = atof(optarg);  break;
		case 'v':  cmd->verbose        = atoi(optarg);  break;

		default:
			flush(stdout, "%s\n", cmd_options_help);
			flush(stdout, "%s\n", sample_usage);
			exit(EXIT_SUCCESS);
		}
	}
}

static void cmd_options_print(struct cmd_options *cmd) {
	flush(stdout, "[a] amplitude       : %.02f", cmd->amplitude);
	flush(stdout, "[c] channels        : %d", cmd->channels);
	flush(stdout, "[e] endian swap     : %d (%s)", cmd->endian_swap, (cmd->endian_swap == 1 ? "yes" : "no"));
	flush(stdout, "[h] include header  : %d (%s)", cmd->header_use, (cmd->header_use == 1 ? "yes" : "no"));
	flush(stdout, "[i] max iteration   : %d", cmd->max_iterations);
	flush(stdout, "[l] frequency lower : %.02f", cmd->frequency_l);
	flush(stdout, "[m] max amplitude   : %.02f", cmd->max_amplitude);
	flush(stdout, "[n] port number     : %s", cmd->port_number);
	flush(stdout, "[p] period          : %d", cmd->period);
	flush(stdout, "[r] sample rate     : %d", cmd->sample_rate);
	flush(stdout, "[s] sensors         : %d", cmd->sensors);
	flush(stdout, "[u] frequency_upper : %.02f", cmd->frequency_u);
	flush(stdout, "[v] verbose         : %d (%s)", cmd->verbose, (cmd->verbose == 1 ? "yes" : "no"));
}

int connection_accept(int socket_listen, bool address_info_print) {
	struct sockaddr_storage client_address;
	socklen_t client_length = sizeof(client_address);

	int socket_client = accept(
		socket_listen,
		(struct sockaddr*)&client_address,
		&client_length);
	return_failure_if((socket_client < 0), -1, "accept() failed: %s", strerror(errno));

	if (address_info_print) {
		char address_buffer[128];
		memset(address_buffer, '\0', sizeof(address_buffer));

		getnameinfo(
			(struct sockaddr *)&client_address,
			client_length,
			address_buffer,
			sizeof(address_buffer),
			0,
			0,
			NI_NUMERICHOST);
		info(stdout, "new connection from: %s", address_buffer);
	}
	return socket_client;
}

int connection_create(char *port, int max_connections) {
	int rc;

	info(stdout, "configuring local address: %s", port);

	// socket
	struct addrinfo hints;
	memset(&hints, '\0', sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo *bind_address;
	getaddrinfo(0, port, &hints, &bind_address);

	int socket_listen = socket(
		bind_address->ai_family,
		bind_address->ai_socktype,
		bind_address->ai_protocol);
	return_failure_if((socket_listen < 0), -1, "socket() failed: %s", strerror(errno));

	// socket options
	int y = 0;
	rc = setsockopt(socket_listen, SOL_SOCKET, SO_REUSEADDR, (void*)&y, sizeof(y));
	return_failure_if((rc < 0), -1, "setsockopt() failed: %s", strerror(errno));

	info(stdout, "binding socket to local address");

	// bind
	rc = bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen);
	return_failure_if((rc != 0), -1, "bind() failed: %s", strerror(errno));

	freeaddrinfo(bind_address);

	info(stdout, "listening");

	// listen
	rc = listen(socket_listen, max_connections);
	return_failure_if((rc < 0), -1, "listen() failed: %s", strerror(errno));

	return socket_listen;
}

volatile sig_atomic_t PROCEED = 1;
void term(int signum) {
	info(stdout, "signal: %d", signum);
	if (signum == SIGINT) {
		info(stdout, "setting proceed = 0");
		PROCEED = 0;
	}
}

static short *sample_malloc(int *buffer_size, struct cmd_options *cmd) {
	short *sample;

	int header_len = 0;
	if (cmd->header_use)
		header_len = 42;

	*buffer_size = header_len + cmd->sensors * cmd->channels * cmd->sample_rate * sizeof(*sample);
	sample = malloc(*buffer_size);

	return sample;
}

static void sample_generate(short *sample, double *time_elapsed, struct cmd_options *cmd, double frequency) {
	short *data;

	int i, j, k, l;

	double delta, two_pi, scale;

	struct header h;

	int header_len = 0;
	if (cmd->header_use) {
		header_len = 42;
	}

	if (sample != NULL) {
		data = sample + (header_len / sizeof(*sample));

		delta  = 1.0 / ((double) cmd->sample_rate);
		two_pi = 6.28318530718;
		scale = 32767.0 / cmd->max_amplitude;

		l = 0;
		for (i = 0; i < cmd->sample_rate; i++) {
			double t = *time_elapsed;
			double r = scale * cmd->amplitude * sin(frequency * two_pi * t);

			short s = (short) r;
			if (cmd->endian_swap) {
				s = bswap_16(s);
			}

			for (j = 0; j < cmd->sensors; j++) {
				for (k = 0; k < cmd->channels; k++) {
					data[l] = s;
					debug(stdout, "%8d, %d, %d, %d, %.7f, %.7f, %8d\n",
						i, j, k, l, t, r,
						data[i * cmd->sensors * cmd->channels + k]);
					l++;
				}
			}

			(*time_elapsed) += delta;
		}

		if (cmd->header_use) {
			unsigned short total = 0;
			for (i = 0; i < cmd->sensors * cmd->channels * cmd->sample_rate; i += 2) {
				unsigned short b = *((unsigned short *)(data + i));
				unsigned short t = bswap_16(b);
				total += t;
			}

			h.magic      = 0xC0C0C0C0C0C0C0C0;
			h.id         = 1;
			h.version    = 1;
			h.timestamp  = 1;
			h.reserved   = 0;
			h.checksum   = total;

			memcpy(sample, &h, 42);
		}
	}
}

static void sample_delete(short *sample)
{
	free(sample);
}

struct wav_header {
	char chunkID[4];
	uint32_t chunkSize;
	char riffType[4];
};

struct format_header {
	char      chunkID[4];
	uint32_t  chunkSize;
	uint16_t  compressionCode;
	uint16_t  channels;
	uint32_t  sampleRate;
	uint32_t  averageBytesPerSecond;
	uint16_t  blockAlign;
	uint16_t  signalBitsPerSample;
};

struct data_header {
	char      chunkID[4];
	uint32_t  chunkSize;
};

void wav_header_write(unsigned char *buffer, int sample_rate, int duration) {
	struct wav_header    * WAV    = (struct wav_header *) buffer;
	struct format_header * Format = (struct format_header *) (WAV + 1);
	struct data_header   * Data   = (struct data_header *) (Format + 1);

	strncpy(WAV->chunkID, "RIFF", 4);
	WAV->chunkSize = (uint32_t)(sample_rate) - 8;
	strncpy(WAV->riffType, "WAVE", 4);

	strncpy(Format->chunkID, "fmt ", 4);
	Format->chunkSize = 16;
	Format->compressionCode = 1;
	Format->channels = 1;
	Format->sampleRate = (uint32_t)(sample_rate);
	Format->signalBitsPerSample = 16;
	Format->blockAlign = 2;
	Format->averageBytesPerSecond = Format->blockAlign * sample_rate;

	strncpy(Data->chunkID, "data", 4);
	Data->chunkSize = duration * sample_rate * 2;
}

int main(int argc, char *argv[]) {
	int rc;


	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGINT, &action, NULL);


	struct cmd_options cmd;

	cmd_options_init(&cmd);
	cmd_options_parse(&cmd, argc, argv);
	cmd_options_print(&cmd);


	short *sample = NULL;
	int buffer_size;

	sample = sample_malloc(&buffer_size, &cmd);
	exit_failure_if(sample == NULL, "failed to allocate sample memory");
	info(stdout, "allocated: %d", buffer_size);


	int socket_listen = connection_create(cmd.port_number, 16);
	exit_failure_if((socket_listen < 0), "connection_create() failed: %s", strerror(errno));

	int socket_max = socket_listen;

	fd_set master;
	FD_ZERO(&master);
	FD_SET(socket_listen, &master);


	FILE *fp = NULL;
	if (cmd.verbose) {
		unsigned char b[44];
		wav_header_write(b, cmd.sample_rate, cmd.max_iterations * cmd.period);
		fp = fopen("emit_freq.wav", "wb");
		fwrite(b, 44, 1, fp);
	}


	double frequency = cmd.frequency_l;
	double time_elapsed = 0.0;
	int total_loops = cmd.max_iterations * cmd.period;
	for (int l = 0; l < total_loops && PROCEED; l++) {
		// select sample
		if ((l % cmd.period) == 0) {
			frequency = (frequency == cmd.frequency_l) ? cmd.frequency_u : cmd.frequency_l;
			info(stdout, "switching frequency %.2lf, % 8d", frequency, l);
		}

		sample_generate(sample, &time_elapsed, &cmd, frequency);

		if (cmd.verbose)
			fwrite(sample, buffer_size, 1, fp);

		fd_set reads, writes;
		reads = master;

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		rc = select(socket_max+1, &reads, 0, 0, &timeout);
		exit_failure_if((rc < 0), "select() failed: %s", strerror(errno));

		info(stdout, "tick");

		// accept new connections, handle goodbyes
		for (int i = 0; i <= socket_max; i++) {
			if (i == socket_listen) {
				if (FD_ISSET(i, &reads)) {
					int socket_new = connection_accept(socket_listen, true);
					FD_SET(socket_new, &master);
					if (socket_new > socket_max) {
						socket_max = socket_new;
					}
				}
			}
			else {
				if (FD_ISSET(i, &reads)) {
					// not expecting any data from client other than goodbye
					info(stdout, "closing connection");
					close(i);
					FD_CLR(i, &master);
				}
			}
		}

		void *buf = sample;

		// emit sample
		writes = master;
		for (int i = 0; i <= socket_max; i++) {
			if (FD_ISSET(i, &writes)) {
				if (i == socket_listen) {
					continue;
				}
				info(stdout, "emit");

				int offset = 0;
				int remaining = buffer_size;
				do {
					int sent = send(i, buf + offset, remaining, 0);
					if (sent == -1) {
						info(stdout, "send() failed: %s", strerror(errno));
						FD_CLR(i, &master);
						close(i);
						continue;
					}
					offset = offset + (sent / sizeof(int));
					remaining = remaining - sent;
				} while(remaining != 0);
			}
		}		
	}

	if (cmd.verbose)
		fclose(fp);

	info(stdout, "closing connections");
	for (int i = 0; i <= socket_max; i++) {
		if (FD_ISSET(i, &master)) {
			close(i);
		}
	}
	close(socket_listen);

	sample_delete(sample);

	return EXIT_SUCCESS;
}
