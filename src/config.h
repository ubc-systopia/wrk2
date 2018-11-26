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


#define SME_DBG 0 
//SME_CLIENT has to be set for all subsequent configurations
#define SME_CLIENT 1

//Randomized thread start times 
//(provides a slower warmup phase and adds randomness).
#define SME_STAGGER_WORKERS 1

//Randomized Inter request delay
#define SME_RANDOMIZE_IRQ 1

//Asynchronous request generation, as per the open loop assumption
#define SME_ASYNC_CLIENT 1

// Amount of time to be used for randomization, 
// OLD: If set to x then all randomizations will be +/- x for SME_RANDOMIZE_IRQ 
// NEW: If set to x then all randomizations will be + x for SME_RANDOMIZE_IRQ 
// Staggering of clients using SME_STAGGER_WORKERS will be 
// randomly picked from 0 - RANDOMIZATION_US
#define RANDOMIZATION_US 12500

#endif /* CONFIG_H */
