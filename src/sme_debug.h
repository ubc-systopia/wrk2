/*
 * sme_debug.h
 *
 * created on: Feb 21, 2017
 * author: aasthakm
 *
 * debug macros
 */

#ifndef __SME_DEBUG_H__
#define __SME_DEBUG_H__

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sched.h>

#include "config.h"

#include "pacer_time.h"

#define WSTR  "WRK2"

#define itrace  \
  do {  \
    if (SME_DEBUG_LVL <= LVL_EXP) { \
      printf("(%d) %s:%d\n"  \
          , getpid() \
          , __func__, __LINE__);  \
    } \
  } while (0)

#define wprint(LVL, F, A...)  \
  do {  \
    if (SME_DEBUG_LVL <= LVL) { \
      printf("%ld %s (%d) %s:%d " F "\n"  \
          , (uint64_t) get_current_time(SCALE_NS), WSTR, getpid() \
          , __func__, __LINE__, A); \
    } \
  } while (0)

#define wprint2(LVL, F, A...) \
  do {  \
    if (SME_DEBUG_LVL <= LVL) { \
      printf("(%d) " F "\n"  \
          , getpid(), A);  \
    } \
  } while (0)

#define wprint3(LVL, F, A...) \
  do {  \
    if (SME_DEBUG_LVL <= LVL) { \
      printf(F "\n", A);  \
    } \
  } while (0)

#endif /* __SME_DEBUG_H__ */
