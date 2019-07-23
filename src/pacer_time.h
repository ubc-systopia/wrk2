#ifndef __PACER_TIME_H__
#define __PACER_TIME_H__

#include <sys/time.h>
#include <time.h>

enum {
  SCALE_NS,
  SCALE_US,
  SCALE_MS,
  SCALE_S
};

static inline uint64_t get_current_time(int scale)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  switch (scale) {
    case SCALE_NS:
      return t.tv_sec * FACTOR_NS + t.tv_nsec;

    case SCALE_US:
      {
        struct timeval t;
        gettimeofday(&t, NULL);
        return (t.tv_sec * 1000000) + t.tv_usec;
      }
//      return t.tv_sec * FACTOR_US + t.tv_nsec / FACTOR_MS;

    case SCALE_MS:
      return t.tv_sec * FACTOR_MS + t.tv_nsec / FACTOR_US;

    case SCALE_S:
      return t.tv_sec + t.tv_nsec / FACTOR_NS;

    default:
      return 0; // invalid
  }
}

#endif /* __PACER_TIME_H__ */
