.PHONY: all utils

CC = gcc
CFLAGS = -fPIC -Wall -Wextra -O3 -I./include
LDFLAGS = -shared
PASS_LIB = libpass.so

SRCS = src/pass.c
OBJS = $(SRCS:.c=.o)

all: ${PASS_LIB} utils

$(PASS_LIB): $(OBJS)
	$(CC) ${LDFLAGS} -o $@ $^ -lm -lfftw3 -ljson-c -lcurl -lpthread
	mv libpass.so ./lib

utils:
	$(MAKE) -C ./utils

clean:
	rm -f ./lib/libpass.so
	rm -f ./src/pass.o
	$(MAKE) clean -C ./utils
