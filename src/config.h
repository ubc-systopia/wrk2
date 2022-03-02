#ifndef CONFIG_H
#define CONFIG_H

#if defined(__FreeBSD__) || defined(__APPLE__)
#define HAVE_KQUEUE
#elif defined(__linux__)
#define HAVE_EPOLL
#define _POSIX_C_SOURCE 200809L
#elif defined (__sun)
#define HAVE_EVPORT
#endif


#define LVL_DBG 0
#define LVL_INFO 1
#define LVL_ERR 2
#define LVL_EXP 3

#define SME_DEBUG_LVL LVL_EXP

//SME_CLIENT has to be set for all subsequent configurations
#define SME_CLIENT 1

//Randomized thread start times 
//(provides a slower warmup phase and adds randomness).
#define SME_STAGGER_WORKERS 1
#define WORKER_RANDOMIZATION_US 10000

//Randomized Inter request delay
#define SME_RANDOMIZE_IRQ 1

//Asynchronous request generation, as per the open loop assumption
#define SME_ASYNC_CLIENT 0

// Amount of time to be used for randomization, 
// OLD: If set to x then all randomizations will be +/- x for SME_RANDOMIZE_IRQ 
// NEW: If set to x then all randomizations will be + x for SME_RANDOMIZE_IRQ 
// Staggering of clients using SME_STAGGER_WORKERS will be 
// randomly picked from 0 - RANDOMIZATION_US
#define REQUEST_RANDOMIZATION_US 10000

#define TIMEOUT_LOOP_FREQ   2

#define MAX_STATQ_ARRAYS  257
#define NUM_RX_SSL_ELEMS  1000000

#define FACTOR_NS          (1000*1000*1000)
#define FACTOR_US          (1000*1000)
#define FACTOR_MS          (1000)

#define CONFIG_PROFLOG  0

#endif /* CONFIG_H */
