// Macros to help with writing tests.
#undef NDEBUG
#ifndef PROVE_H
#define PROVE_H

#include <stdio.h>
#include <stdlib.h>

#define prove_init() int prove_nfailures = 0;

#define prove_run(funcname) \
    fprintf(stderr, "=prove= " #funcname " START\n"); \
    if (funcname()) { \
        fprintf(stderr, "=prove= " #funcname " FAIL\n"); \
        prove_nfailures++; \
    } else { \
        fprintf(stderr, "=prove= " #funcname " PASS\n"); \
    }

#define prove_exit() exit(prove_nfailures ? 1 : 0);


#endif
