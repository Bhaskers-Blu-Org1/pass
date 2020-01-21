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

const char *cmd_options_available = "b:f:n:";

const char *cmd_options_help = "\
-b: bytes (how many bytes in each send (default is 1000000)  \n\
-f: file name                                                \n\
-n: port number to use (default is 1234)                     \n\
-v: verbose (0 - no print, 1 - print, default 0)             \n\
                                                             \n\
Note:                                                        \n\
  bytes = sample rate x devices x channels x sizeof(short)   \n\
                                                             \n\
                                                             \n";

const char *sample_usage = "\
sample args:\n\
emit 500000 bytes from `somefile` each second over port 1235:-b 500000 -f somefile -n 1235\n";

struct cmd_options {
	char *filename;
	int bytes;
	int verbose;

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
	cmd->filename = NULL;
	cmd->bytes = 1000000;
	cmd->verbose = 0;

	memset(cmd->port_number, '\0', sizeof(cmd->port_number));
	strcpy(cmd->port_number, "1234");
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
		case 'b':  cmd->bytes     = atoi(optarg);  break;
		case 'f':  cmd->filename  = optarg;        break;
		case 'n':
			if (strlen(optarg) < 15) {
				strcpy(cmd->port_number, optarg);
			}
			break;
		case 'v':  cmd->verbose        = atoi(optarg);  break;

		default:
			flush(stdout, "%s\n", cmd_options_help);
			flush(stdout, "%s\n", sample_usage);
			exit(EXIT_SUCCESS);
		}
	}

	if (cmd->filename == NULL) {
		flush(stdout, "\n filename required\n");
		flush(stdout, "%s\n", cmd_options_help);
		flush(stdout, "%s\n", sample_usage);
		exit(EXIT_SUCCESS);
	}
}

static void cmd_options_print(struct cmd_options *cmd) {
	flush(stdout, "[b] bytes        : %d", cmd->bytes);
	flush(stdout, "[f] filename     : %s", cmd->filename);
	flush(stdout, "[n] port number  : %s", cmd->port_number);
	flush(stdout, "[v] verbose      : %d (%s)", cmd->verbose, (cmd->verbose == 1 ? "yes" : "no"));
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

	FILE *data_fp;
	char *data_buffer;
	int data_size = cmd.bytes;

	data_fp = fopen(cmd.filename, "r");
	exit_failure_if((data_fp == NULL), "failed to open %s", cmd.filename);

	data_buffer = malloc(data_size);
	exit_failure_if((data_buffer == NULL), "failed to allocate memory");
	memset(data_buffer, 0, data_size);

	int socket_listen = connection_create(cmd.port_number, 16);
	exit_failure_if((socket_listen < 0), "connection_create() failed: %s", strerror(errno));

	int socket_max = socket_listen;

	fd_set master;
	FD_ZERO(&master);
	FD_SET(socket_listen, &master);

	while (PROCEED) {
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

		// fetch sample
		rc = fread(data_buffer, 1, data_size, data_fp);
		if (rc != data_size) {
			// TODO: check if eof error
			fclose(data_fp);

			data_fp = fopen(cmd.filename, "r");
			int bytes_remaining = data_size - rc;
			rc = fread(data_buffer+rc, 1, bytes_remaining, data_fp);
			exit_failure_if((rc != bytes_remaining), "failed to read");
		}

		// emit sample
		writes = master;
		for (int i = 0; i <= socket_max; i++) {
			if (FD_ISSET(i, &writes)) {
				if (i == socket_listen) {
					continue;
				}
				info(stdout, "emit");

				int offset = 0;
				int remaining = data_size;
				do {
					int sent = send(i, data_buffer + offset, remaining, 0);
					if (sent == -1) {
						info(stdout, "send() failed: %s", strerror(errno));
						FD_CLR(i, &master);
						close(i);
						break;
					}
					offset = offset + (sent / sizeof(int));
					remaining = remaining - sent;
				} while(remaining != 0);
			}
		}		
	}

	fclose(data_fp);
	free(data_buffer);

	info(stdout, "closing connections");
	for (int i = 0; i <= socket_max; i++) {
		if (FD_ISSET(i, &master)) {
			close(i);
		}
	}
	close(socket_listen);

	return EXIT_SUCCESS;
}
