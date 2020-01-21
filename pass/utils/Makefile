.PHONY: all

CC = gcc
CFLAGS = -Wall -Wextra -O3 -I../include -L../lib
LDFLAGS = -lpass -lm

all: emit_chirp_linear  emit_file  emit_freq  multi_frequency_bins  multi_octave_bands  multi_wav_file  viewer


emit_chirp_linear: emit/emit_chirp_linear.c
	$(CC) $(CFLAGS) -o $@ $< -lm

emit_file: emit/emit_file.c
	$(CC) $(CFLAGS) -o $@ $< -lm

emit_freq: emit/emit_freq.c
	$(CC) $(CFLAGS) -o $@ $< -lm

multi_frequency_bins: process/multi_frequency_bins.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

multi_octave_bands: process/multi_octave_bands.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

multi_wav_file: process/multi_wav_file.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

viewer: view/cmd/view/main.go
	cd view/cmd/view;  go get  &&  go build -o viewer
	mv view/cmd/view/viewer .

clean:
	rm -f *.o driver  emit_chirp_linear  emit_file  emit_freq  multi_frequency_bins  multi_octave_bands  multi_wav_file  viewer
