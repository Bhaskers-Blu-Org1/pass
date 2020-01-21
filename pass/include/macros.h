// author john.d.sheehan@ie.ibm.com

#ifndef PASS_MACROS
#define PASS_MACROS

#include <stdio.h>
#include <time.h>


#define exit_failure_if(A, M, ...) do {  \
        if ((A)) {  \
                fprintf(stdout, "%s:%d " M "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
                fflush(stdout);  \
                exit(1);  \
        }  \
} while(0)

#ifdef DEBUG
#define debug(A, M, ...) do {  \
	time_t local = time(NULL); \
	char *time_str = asctime(localtime(&local)); \
	time_str[strlen(time_str) - 1] = '\0'; \
        fprintf(A, "%s:%s:%d [debug] " M "\n", time_str, __FILE__, __LINE__, ##__VA_ARGS__);  \
        fflush(A);  \
} while(0)
#else
#define debug(A, M, ...) do {  \
} while(0)
#endif

#define flush(A, M, ...) do {  \
        fprintf(A, M "\n", ##__VA_ARGS__);  \
        fflush(A);  \
} while(0)

#define info(A, M, ...) do {  \
	time_t local = time(NULL); \
	char *time_str = asctime(localtime(&local)); \
	time_str[strlen(time_str) - 1] = '\0'; \
        fprintf(A, "%s:%s:%d " M "\n", time_str, __FILE__, __LINE__, ##__VA_ARGS__);  \
        fflush(A);  \
} while(0)

#define return_failure_if(A, E, M, ...) do {  \
        if ((A)) {  \
                fprintf(stdout, "%s:%d " M "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
                fflush(stdout);  \
                return E;  \
        }  \
} while(0)


#endif
