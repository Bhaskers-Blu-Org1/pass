// author john.d.sheehan@ie.ibm.com

#include <byteswap.h>
#include <curl/curl.h>
#include <errno.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#include "macros.h"
#include "pass.h"

#define   likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

const int DATESIZE = 26;

struct wav_header {
	char      chunkID[4];
	uint32_t  chunkSize;
	char      riffType[4];
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

static void hann(double *buffer, const int window_length) {
	int i;

	double s = 0.0;
	double w = (double)(window_length - 1);
	double x = 0.0;

	w = window_length - 1;

	for (i = 0; i < window_length; i++) {
		x = (2.0 * M_PI * i) / w;
		buffer[i] = 0.5 - 0.5 * cos(x);
	}

	for (i = 0; i < window_length; ++i) {
		s += (buffer[i] * buffer[i]);
	}

	for (i = 0; i < window_length; ++i) {
		buffer[i] /= s;
	}
}

static int header_search(const unsigned char *buffer, int start, int end) {
	while (start < end - PASS_DATA_HEADER_SIZE) {
		if (IS_PASS_DATA_HEADER(buffer + start))
			return start;
		start++;
	}
	return end;
}

pass_response pass_array_allocate(pass_array *array, const int total) {
	array->values = malloc(sizeof(double) * total);
	return_failure_if((array->values == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));

	array->count = 0;
	array->total = total;

	return PASS_SUCCESS;
}

pass_response pass_array_free(pass_array *array) {
	if (array->values != NULL) {
		free(array->values);
		array->values = NULL;
	}

	array->count = 0;
	array->total = 0;

	return PASS_SUCCESS;
}

void pass_close(pass_context *pc) {
	if (pc->sd >= 0)
		close(pc->sd);
	pc->sd = -1;
}

pass_response pass_connect(pass_context *pc, const char *server, const char *port) {
	int rc;
	int sd;

	debug(stdout, "configuring remote address");

	struct addrinfo hints;
	memset(&hints, '\0', sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *peer_address;
	rc = getaddrinfo(server, port, &hints, &peer_address);
	return_failure_if((rc != 0), PASS_FAILURE_NO_CONN, "getaddrinfo() failed: %s", strerror(errno));

	char address_buffer[128];
	char service_buffer[128];
	getnameinfo(
		peer_address->ai_addr,
		peer_address->ai_addrlen,
		address_buffer,
		sizeof(address_buffer),
		service_buffer,
		sizeof(service_buffer),
		NI_NUMERICHOST);
	info(stdout, "server address: %s %s", address_buffer, service_buffer);

	debug(stdout, "creating socket");

	sd = socket(
		peer_address->ai_family,
		peer_address->ai_socktype,
		peer_address->ai_protocol);
	return_failure_if((sd < 0), PASS_FAILURE_NO_CONN, "socket() failed: %s", strerror(errno));

	debug(stdout, "connecting");

	rc = connect(
		sd,
		peer_address->ai_addr,
		peer_address->ai_addrlen);
	return_failure_if((rc != 0), PASS_FAILURE_NO_CONN, "connect() failed: %s", strerror(errno));

	freeaddrinfo(peer_address);

	pc->sd = sd;

	debug(stdout, "connected");

	return PASS_SUCCESS;
}

pass_response pass_context_free(pass_context *pc) {
	pc->sequence_id = 0;

	pc->sensor_count  = 0;
	pc->channel_count = 0;
	pc->sample_rate   = 0;
	pc->header_size   = 0;

	if (pc->sd >= 0)
		close(pc->sd);
	pc->sd = -1;

	if (pc->input) {
		free(pc->input);
		pc->input = NULL;
	}

	if (pc->scratch.buffer != NULL) {
		free(pc->scratch.buffer);
		pc->scratch.buffer = NULL;

		pc->scratch.count = 0;
		pc->scratch.total = 0;
	}

	if (pc->payload != NULL) {
		free(pc->payload);
		pc->payload = NULL;
	}
	if (pc->header != NULL) {
		free(pc->header);
		pc->header = NULL;
	}
		
	return PASS_SUCCESS;
}

pass_response pass_context_init(
	pass_context *pc,
	int sensor_count,
	int channel_count,
	int sample_rate,
	bool contains_header) {

	if (pc == NULL) {
		pc = malloc(sizeof(pass_context));
		return_failure_if((pc == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));
	}
	memset(pc, 0, sizeof(pass_context));

	pc->sequence_id = 0;
	pc->sensor_count = sensor_count;
	pc->channel_count = channel_count;
	pc->sample_rate = sample_rate;

	pc->header_size = 0;
	if (contains_header)
		pc->header_size = 42;

	pc->sd = -1;

	pc->input          = NULL;
	pc->scratch.buffer = NULL;
	pc->payload        = NULL;
	pc->header         = NULL;

	int buffer_size = 0;
	int samples_total = pc->sample_rate * pc->channel_count * pc->sensor_count;

	// alloc input
	buffer_size = (sizeof(short) * samples_total) + pc->header_size;
	pc->input = malloc(buffer_size);
	return_failure_if((pc->input == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));
	memset(pc->input, '\0', buffer_size);

	// alloc scratch
	buffer_size = 2 * ((sizeof(short) * samples_total) + pc->header_size);
	pc->scratch.buffer = malloc(buffer_size);
	return_failure_if((pc->scratch.buffer == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));
	memset(pc->scratch.buffer, '\0', buffer_size);
	pc->scratch.count = 0;
	pc->scratch.total = buffer_size;

	// alloc payload
	buffer_size = sizeof(short) * samples_total;
	pc->payload = malloc(buffer_size);
	return_failure_if((pc->payload == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));
	memset(pc->payload, '\0', buffer_size);

	// alloc header
	if (contains_header) {
		buffer_size = pc->header_size;
		pc->header = malloc(buffer_size);
		return_failure_if((pc->header == NULL), PASS_FAILURE_NOMEM, "malloc() failed: %s", strerror(errno));
		memset(pc->header, '\0', buffer_size);
	}

	return PASS_SUCCESS;
}

pass_response pass_convert_to_doubles(
	pass_array *array,
	const pass_context *pc,
	const int sensor,
	const int channel,
	const double gradient,
	const double offset) {

	return_failure_if((pc->sample_rate > array->total), PASS_FAILURE_NOMEM, "insufficent memory");

	int count = pc->sensor_count * pc->channel_count * pc->sample_rate;

	int i = 0;
	for (int c = 0; c < count; c += (pc->sensor_count * pc->channel_count)) {
		int l = c + (sensor * pc->channel_count) + channel;
		short s = pc->payload[l];

		array->values[i] = ((double)(s)) * gradient + offset;
		i++;
	}

	array->count = i;
	array->sequence_id = pc->sequence_id;

	return PASS_SUCCESS;
}

pass_response pass_curl_init()
{
	CURLcode cr;
	cr = curl_global_init(CURL_GLOBAL_ALL);
	if (cr != CURLE_OK)
		return PASS_FAILURE_CURL;

	return PASS_SUCCESS;
}

pass_response pass_curl_post(
	const char *url,
	const pass_array *input,
	const char *name,
	const char *msg_type,
	const int sensor,
	const int channel) {

	CURL *curl;
	CURLcode cr;
	struct curl_slist *headers = NULL;

	json_object *jobject = json_object_new_object();

	json_object *jstring = json_object_new_string(name);
	json_object *jmsg_type = json_object_new_string(msg_type);
	json_object *jsensor = json_object_new_int(sensor);
	json_object *jchannel = json_object_new_int(channel);

	json_object *jarray = json_object_new_array();
	for (int i = 0; i < input->count; i++) {
		json_object *jdouble = json_object_new_double(input->values[i]);
		json_object_array_add(jarray, jdouble);
	}

	json_object_object_add(jobject, "name", jstring);
	json_object_object_add(jobject, "type", jmsg_type);
	json_object_object_add(jobject, "sensor", jsensor);
	json_object_object_add(jobject, "channel", jchannel);
	json_object_object_add(jobject, "values", jarray);

	curl = curl_easy_init();
	if (curl == NULL)
		return PASS_FAILURE_CURL;

	headers = curl_slist_append(headers, "Accept: application/json");
	headers = curl_slist_append(headers, "Content-Type: application/json");
	headers = curl_slist_append(headers, "charsets: utf-8");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(jobject));

	cr = curl_easy_perform(curl);
	if (cr != CURLE_OK)
		return PASS_FAILURE_CURL;

	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);

	json_object_put(jobject);

	return PASS_SUCCESS;
}

pass_response pass_curl_term()
{
	curl_global_cleanup();
	return PASS_SUCCESS;
}

void pass_decibels(pass_array *input, const double reference, const double correction) {
	for (int i = 0; i < input->count; i++) {
		input->values[i] = 10.0 * log10(input->values[i] / reference) + correction;
	}
}

pass_response pass_endian_swap(pass_context *pc) {
	int count = pc->sensor_count * pc->channel_count * pc->sample_rate;
	short *buffer = pc->payload;

	for (int i = 0; i < count; i++) {
		buffer[i] = bswap_16(buffer[i]);
	}

	return PASS_SUCCESS;
}

pass_response pass_fftw_execute(pass_array *array, pass_fftw_plan *plan) {
	// TODO: check bounds

	for (int i = 0; i < plan->sample_rate; i++) {
		plan->input[i] = array->values[i] * plan->window[i];
	}

	fftw_execute(plan->plan_forward);

	array->values[0] = plan->result[0][0] * plan->result[0][0];
	for (int i = 1; i < plan->output_rate; i++) {
		array->values[i] = 2.0 * (plan->result[i][0] * plan->result[i][0] + plan->result[i][1] * plan->result[i][1]);
	}
	array->count = plan->output_rate;

	for (int i = array->count; i < array->total; i++) {
		array->values[i] = 0.0;
	}

	return PASS_SUCCESS;
}

pass_response pass_fftw_plan_init(pass_fftw_plan *plan, const int sample_rate)
{
	plan->sample_rate = sample_rate;
	plan->output_rate = (sample_rate / 2) + 1;
	plan->input = malloc(sizeof(double) * sample_rate);
	if (plan->input == NULL)
		return PASS_FAILURE_NOMEM;

	plan->window = malloc(sizeof(double) * sample_rate);
	if (plan->window == NULL)
		return PASS_FAILURE_NOMEM;

	plan->result = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * sample_rate);
	if (plan->result == NULL)
		return PASS_FAILURE_NOMEM;

	plan->plan_forward = fftw_plan_dft_r2c_1d(sample_rate, plan->input, plan->result, FFTW_ESTIMATE);
	hann(plan->window, sample_rate);

	return PASS_SUCCESS;
}

pass_response pass_fftw_plan_term(pass_fftw_plan *plan)
{
	fftw_destroy_plan(plan->plan_forward);
	fftw_free(plan->result);
	free(plan->window);
	free(plan->input);

	return PASS_SUCCESS;
}

pass_response pass_frequency_bins(pass_array *input, const int lower, const int upper, const int stride) {
	// TODO: check bounds

	int i, j, k;
	double sum;

	for (i = lower, j = 0; i < upper; i += stride, j++) {
		sum = 0.0;
		for (k = i; k < (i + stride); k++) {
			sum += (input->values[k] * input->values[k]);
		}
		input->values[j] = sum;
	}
	input->count = j;

	for (i = input->count; i < input->total; i++) {
		input->values[i] = 0.0;
	}

	return PASS_SUCCESS;
}

pass_response pass_gaps_detection(pass_context *pc) {
	int header_first_start = 0;
	int header_first_end = 0;
	int header_second_start = 0;

	int byte_count = sizeof(short) * pc->sensor_count * pc->channel_count * pc->sample_rate;
	int data_count = sizeof(short) * pc->sensor_count * pc->channel_count * pc->sample_rate + pc->header_size;
	int payload_count = 0;

	unsigned char *data = (unsigned char *) (pc->input);

	scratch_bytes *scratch = &(pc->scratch);
	
	if (unlikely(scratch->count + data_count > scratch->total)) {
		/* too much data, emptying buffer */
		if (unlikely(scratch->buffer)) {
			memset(scratch->buffer, 0, scratch->count);
			scratch->count = 0;
		}

		return PASS_FAILURE_GAP_DETECTED;
	}

	/* copy input data into scratch buffer */
	memcpy(&(scratch->buffer[scratch->count]), data, data_count);
	scratch->count += data_count;

	while (unlikely(scratch->buffer)) {
		if (scratch->count < PASS_DATA_HEADER_SIZE) {
			/* insufficent data to search for a header */
			if (payload_count != byte_count) {
				return PASS_FAILURE_GAP_DETECTED;
			}
			else {
				break;
			}
		}

		/* search for first header */
		header_first_start = header_search(scratch->buffer, 0, scratch->count);
		if (unlikely(header_first_start == scratch->count)) {
			/* first header not found, removing old data */
			memset(scratch->buffer, 0, scratch->count);
			scratch->count = 0;

			return PASS_FAILURE_GAP_DETECTED;
		}

		/* search for second header */
		header_first_end = header_first_start + PASS_DATA_HEADER_SIZE;
		header_second_start = header_search(scratch->buffer, header_first_end, scratch->count);

		int payload_size_received = header_second_start - header_first_end;
		if (likely(payload_size_received == byte_count)) {
			memcpy(pc->header, &(scratch->buffer[header_first_start]), PASS_DATA_HEADER_SIZE);

			unsigned char *b = (unsigned char *)(pc->payload);
			memcpy(b, &(scratch->buffer[header_first_end]), byte_count);
			payload_count = byte_count;

			scratch->count = scratch->count - header_second_start;
			memcpy(scratch->buffer, &(scratch->buffer[header_second_start]), scratch->count);
			memset(&(scratch->buffer[scratch->count]), 0, scratch->total - scratch->count);
		} else if (unlikely(payload_size_received > byte_count)) {
			memset(scratch->buffer, 0, scratch->count);
			scratch->count = 0;

			return PASS_FAILURE_GAP_DETECTED;
		} else {
			if (unlikely(header_second_start < scratch->count)) {
				/* there are two headers */
				memset(scratch->buffer, 0, scratch->count);
				scratch->count = 0;

				return PASS_FAILURE_GAP_DETECTED;
			} else {
				scratch->count = scratch->count - header_first_start;

				memcpy(scratch->buffer, &(scratch->buffer[header_first_start]), scratch->count);
				memset(&(scratch->buffer[scratch->count]), 0, scratch->total - scratch->count);
			}
			break;
		}
	}

	unsigned char *h = pc->header;
	uint32_t *tmp = (uint32_t *)(h + 28);
	uint64_t s_id = bswap_32(*tmp);

	pc->sequence_id = s_id;

	return PASS_SUCCESS;
}

pass_response pass_octave_bands(pass_array *input, const int lower, const int upper) {
	// TODO: check bounds

	int index_lower, index_upper;

	index_lower = (lower <= octave_band_smallest) ? 0 : lower - octave_band_smallest;
	index_upper = (upper >= octave_band_largest) ? octave_band_largest - octave_band_smallest : upper - octave_band_smallest;

	int i = 0;
	double sum;
	for (int j = index_lower; j < index_upper; j++) {
		sum = octave_bands[j].lower_weight * input->values[ octave_bands[j].lower ];
		for (int k = (octave_bands[j].lower + 1); k < octave_bands[j].upper; k++) {
			sum += input->values[k];
		}
		sum += octave_bands[j].upper_weight * input->values[ octave_bands[j].upper ];

		input->values[i] = sum;
		i++;
	}
	input->count = i;

	for (int j = i; j < input->total; j++) {
		input->values[j] = 0.0;
	}

	return PASS_SUCCESS;

}

pass_response pass_read(pass_context *pc) {
	int received;
	int remaining = sizeof(short) * pc->sensor_count * pc->channel_count * pc->sample_rate + pc->header_size;

	unsigned char *buf = (unsigned char *)(pc->input);

	int count = 0;
	int size = remaining;
	while (count != size) {
		received = read(pc->sd, buf + count, remaining);
		if (received <= 0)
			return PASS_FAILURE_GENERIC;

		count += received;
		remaining -= received;
	}

	if (pc->header_size == 0)
		memcpy(pc->payload, pc->input, count);

	return PASS_SUCCESS;

}

pass_response pass_wav_init(
	pass_wav_description *desc,
	const char *directory,
	const char *prefix,
	const double scale,
	const int duration) {

	int length;

	desc->scale = scale;
	desc->filename_length = 0;
	desc->duration = duration;
	desc->seconds_written = 0;

	length = strlen(directory) + 1 + strlen(prefix) + DATESIZE + 4 + 1;
	desc->filename = malloc(sizeof(char) * length);
	if (desc->filename == NULL)
		return PASS_FAILURE_NOMEM;
	desc->filename_length = length;

	length = strlen(directory) + 1;
	desc->directory = malloc(sizeof(char) * length);
	if (desc->directory == NULL)
		return PASS_FAILURE_NOMEM;

	memset(desc->directory, '\0', length);
	sprintf(desc->directory, "%s", directory);

	length = strlen(prefix) + 1;
	desc->prefix = malloc(sizeof(char) * length);
	if (desc->prefix == NULL)
		return PASS_FAILURE_NOMEM;

	memset(desc->prefix, '\0', length);
	sprintf(desc->prefix, "%s", prefix);

	return PASS_SUCCESS;
}

pass_response pass_wav_write(pass_context *pc, pass_wav_description *desc, const int sensor, const int channel) {
	time_t timer;
	struct tm* tm_info;

	FILE *fp;
	char buffer[DATESIZE];
	char header[44];

	if (desc->seconds_written == 0) {
		memset(desc->filename, '\0', desc->filename_length);

		time(&timer);
		tm_info = localtime(&timer);

		strftime(buffer, DATESIZE, "%Y.%m.%d.%H.%M.%S", tm_info);
		sprintf(desc->filename, "%s/%s.%s.wav", desc->directory, desc->prefix, buffer);
	}

	fp = fopen(desc->filename, "ab");
	if (fp == NULL)
		return PASS_FAILURE_GENERIC;

	if (desc->seconds_written == 0) {
		int length = sizeof(struct wav_header)
			+ sizeof(struct format_header)
			+ sizeof(struct data_header)
			+ (desc->duration * pc->sample_rate * sizeof(short));

		struct wav_header    * WAV    = (struct wav_header *) header;
		struct format_header * Format = (struct format_header *) (WAV + 1);
		struct data_header   * Data   = (struct data_header *) (Format + 1);

		strncpy(WAV->chunkID, "RIFF", 4);
		WAV->chunkSize = (uint32_t)(length) - 8;
		strncpy(WAV->riffType, "WAVE", 4);

		strncpy(Format->chunkID, "fmt ", 4);
		Format->chunkSize = 16;
		Format->compressionCode = 1;
		Format->channels = 1;
		Format->sampleRate = (uint32_t)(pc->sample_rate);
		Format->signalBitsPerSample = 16;
		Format->blockAlign = 2;
		Format->averageBytesPerSecond = Format->blockAlign * pc->sample_rate;

		strncpy(Data->chunkID, "data", 4);
		Data->chunkSize = desc->duration * pc->sample_rate * 2;

		fwrite(header, 44, 1, fp);
	}

	int samples_total = pc->sensor_count * pc->channel_count * pc->sample_rate;
	for (int k = 0; k < samples_total; k += (pc->sensor_count * pc->channel_count)) {
		int l = k + (sensor * pc->channel_count) + channel;
		short s = pc->payload[l];
		fwrite(&s, 2, 1, fp);
	}

	fclose(fp);

	desc->seconds_written++;
	if (desc->seconds_written == desc->duration) {
		desc->seconds_written = 0;
	}

	return PASS_SUCCESS;
}

pass_response pass_wav_term(pass_wav_description *desc)
{
	free(desc->prefix);
	free(desc->filename);
	free(desc->directory);

	desc->filename_length = 0;
	desc->seconds_written = 0;
	desc->duration = 0;

	return PASS_SUCCESS;
}
