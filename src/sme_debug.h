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

#include <unistd.h>
#include <sys/types.h>
#include <sched.h>

#include "config.h"

#define PFX "SME"

#define itrace  \
  do {  \
    if (SME_DEBUG_LVL <= LVL_EXP) { \
      printf(PFX " (%d) %s:%d\n"  \
          , getpid() \
          , __func__, __LINE__);  \
    } \
  } while (0)

#define wprint(LVL, F, A...)  \
  do {  \
    if (SME_DEBUG_LVL <= LVL) { \
      printf(PFX " (%d) %s:%d " F "\n"  \
          , getpid() \
          , __func__, __LINE__, A); \
    } \
  } while (0)

#define wprint2(LVL, F, A...) \
  do {  \
    if (SME_DEBUG_LVL <= LVL) { \
      printf(PFX " (%d) " F "\n"  \
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
