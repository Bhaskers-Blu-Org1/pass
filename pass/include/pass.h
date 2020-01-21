// author john.d.sheehan@ie.ibm.com

#ifndef PASS_HEADER
#define PASS_HEADER

#define IS_PASS_DATA_HEADER(b) \
	(*( ((uint32_t *)(b)) + 0) == 0xc0c0c0c0 && \
	 *( ((uint32_t *)(b)) + 1) == 0xc0c0c0c0 && \
	 *( ((uint32_t *)(b)) + 2) != 0xc0c0c0c0 && \
	 *( ((uint32_t *)(b)) + 3) != 0xc0c0c0c0)

#define PASS_DATA_HEADER_SIZE 42

#include <fftw3.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	PASS_SUCCESS,
	PASS_FAILURE_CURL,
	PASS_FAILURE_GAP_DETECTED,
	PASS_FAILURE_GENERIC,
	PASS_FAILURE_NOMEM,
	PASS_FAILURE_NO_CONN,
	PASS_FAILURE_NO_DATA
} pass_response;

typedef struct {
	int sample_rate;
	int output_rate;

	fftw_plan plan_forward;
	fftw_complex *result;

	double *input;
	double *window;
} pass_fftw_plan;

typedef struct {
	double scale; 
        
	int filename_length;
	int duration;
	int seconds_written;
        
	char *directory;
	char *filename;
	char *prefix;
} pass_wav_description;

typedef struct {
	int count;   /* current number of bytes stored */
	int total;   /* maximum number of bytes that can be stored */

	unsigned char *buffer;
} scratch_bytes;

typedef struct {
	uint64_t sequence_id;

	int count;
	int total;

	double *values;
} pass_array;

typedef struct {
	uint64_t sequence_id;

	int sensor_count;
	int channel_count;
	int sample_rate;
	int header_size;         // in bytes
	int single_sample_size;  // in bytes, assuming sizeof(short)

	int sd;

	scratch_bytes scratch;

	short *input;
	short *payload;
	unsigned char *header;
	double *values;
} pass_context;


pass_response  pass_array_allocate(pass_array *, const int);

pass_response  pass_array_free(pass_array *);

void           pass_close(pass_context *);

pass_response  pass_connect(
	pass_context *,
	const char *,   // host
	const char *);  // port

pass_response  pass_context_free(pass_context *);

pass_response  pass_context_init(
	pass_context *,
	int,    // sensor_count
	int,    // channel_count
	int,    // sample_rate
	bool);  // contains_header

pass_response pass_convert_to_doubles(
	pass_array *,
	const pass_context *,
	const int,      // sensor
	const int,      // channel
	const double,   // gradient
	const double);  // offset

pass_response  pass_curl_init();

pass_response  pass_curl_post(
	const char *,        // url to post to
	const pass_array *,  // values (doubles)
	const char *,        // name, any string identifier
	const char *,        // message type
	const int,           // sensor
	const int);          // channel

pass_response  pass_curl_term();

void           pass_decibels(pass_array *, const double, const double);

pass_response  pass_endian_swap(pass_context *);

pass_response  pass_fftw_execute(pass_array *, pass_fftw_plan *);

pass_response  pass_fftw_plan_init(pass_fftw_plan *, const int);

pass_response  pass_fftw_plan_term(pass_fftw_plan *);

pass_response  pass_frequency_bins(pass_array *, const int, const int, const int);

pass_response  pass_gaps_detection(pass_context *);

pass_response  pass_octave_bands(pass_array *, const int, const int);

pass_response  pass_read(pass_context *);

pass_response  pass_wav_init(
	pass_wav_description *,
	const char *,  // directory
	const char *,  // prefix
	const double,  // scale
	const int);    // duration

pass_response  pass_wav_write(
	pass_context *,
	pass_wav_description *,
	const int,   // sensor
	const int);  // channel

pass_response  pass_wav_term(pass_wav_description *);

const int octave_band_smallest = 10;
const int octave_band_largest = 53;

const struct {
	int band_number;
	int lower;
	int upper;

	double lower_weight;
	double upper_weight;
} octave_bands[] = {
	{10,      9,     11, 0.087491, 0.220185},
	{11,     11,     14, 0.779815, 0.125375},
	{12,     14,     18, 0.874625, 0.782794},
	{13,     18,     22, 0.217206, 0.387211},
	{14,     22,     28, 0.612789, 0.183829},
	{15,     28,     35, 0.816171, 0.481339},
	
	{16,     35,     45, 0.518661, 0.668359},
	{17,     45,     56, 0.331641, 0.234133},
	{18,     56,     71, 0.765867, 0.794578},
	{19,     71,     89, 0.205422, 0.125094},
	
	{20,     89,    112, 0.874906, 0.201845},
	{21,    112,    141, 0.798155, 0.253754},
	{22,    141,    178, 0.746246, 0.827941},
	{23,    178,    224, 0.172059, 0.872114},
	{24,    224,    282, 0.127886, 0.838293},

	{25,    282,    355, 0.161707, 0.813389},
	{26,    355,    447, 0.186611, 0.683592},
	{27,    447,    562, 0.316408, 0.341325},
	{28,    562,    708, 0.658675, 0.945784},

	{29,    708,    891, 0.054216, 0.250938},
	{30,    891,   1122, 0.749062, 0.018454},
	{31,   1122,   1413, 0.981546, 0.537545},
	{32,   1413,   1778, 0.462455, 0.279410},

	{33,   1778,   2239, 0.720590, 0.721139},
	{34,   2239,   2818, 0.278861, 0.382931},
	{35,   2818,   3548, 0.617069, 0.133892},
	{36,   3548,   4467, 0.866108, 0.835922},

	{37,   4467,   5623, 0.164078, 0.413252},
	{38,   5623,   7079, 0.586748, 0.457844},
	{39,   7079,   8913, 0.542156, 0.509381},
	{40,   8913,  11220, 0.490619, 0.184543},

	{41,  11220,  14125, 0.815457, 0.375446},
	{42,  14125,  17783, 0.624554, 0.794100},
	{43,  17783,  22387, 0.205900, 0.211386},
	{44,  22387,  28184, 0.788614, 0.829313},

	{45,  28184,  35481, 0.170687, 0.338923},
	{46,  35481,  44668, 0.661077, 0.359215},
	{47,  44668,  56234, 0.640785, 0.132519},
	{48,  56234,  70795, 0.867481, 0.578438},

	{49,  70795,  89125, 0.421562, 0.093813},
	{50,  89125, 112202, 0.906187, 0.845430},
	{51, 112202, 141254, 0.154570, 0.754462},
	{52, 141254, 177828, 0.245538, 0.941004},

	{53, 177828, 223872, 0.058996, 0.113857}
};
const int octave_bands_count = (int)(sizeof(octave_bands) / sizeof(octave_bands[0]));

#endif
