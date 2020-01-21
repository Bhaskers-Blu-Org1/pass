
all: clean file freq

clean:
	rm -rf ./emit_file
	rm -rf ./emit_freq


chirp_linear:
	gcc -Wall emit_chirp_linear.c -lm -o emit_chirp_linear


file:
	gcc -Wall emit_file.c -lm -o emit_file


freq:
	gcc -Wall emit_freq.c -lm -o emit_freq
