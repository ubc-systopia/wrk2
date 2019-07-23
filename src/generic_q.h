/*
 * statq.h
 *
 * created on: Jan 20, 2018
 * author: aasthakm
 *
 * queue to hold timeseries data to be printed at rmmod
 */

#ifndef __STATQ_H__
#define __STATQ_H__

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include <stdint.h>

#include "config.h"
#include "sme_debug.h"

typedef struct generic_q {
  uint64_t prod_idx;
  uint64_t cons_idx;
  int qidx;
  int size;
  int elem_size;
  uint16_t port;
  uint16_t fd;
  uint16_t sys_tid;
  void *elem_arr;
  void (*printfn)(struct generic_q *queue);
//  spinlock_t lock;
} generic_q_t;

#define init_generic_q(Q, i, s, t, f)                 \
{                                                     \
  (Q).elem_arr = malloc(s * sizeof(t));              \
  (Q).qidx = i;                                       \
  (Q).size = s;                                       \
  (Q).elem_size = sizeof(t);                          \
  (Q).prod_idx = 0;                                   \
  (Q).cons_idx = 0;                                   \
  (Q).printfn = f;                                    \
  /* spin_lock_init(&(Q).lock); */                         \
}

#define cleanup_generic_q(Q)  \
{                             \
  if ((Q).elem_arr) {         \
    free((Q).elem_arr);      \
    (Q).elem_arr = NULL;      \
    (Q).elem_size = 0;        \
    (Q).size = 0;             \
    (Q).prod_idx = 0;         \
    (Q).cons_idx = 0;         \
  }                           \
}

#define print_generic_q(Q)  \
{                           \
  if ((Q).printfn) {        \
    (Q).printfn(&(Q));      \
  }                         \
}

static inline int generic_q_empty(generic_q_t *queue)
{
  return (queue->cons_idx == queue->prod_idx);
}

static inline int generic_q_full(generic_q_t *queue)
{
  return ((queue->prod_idx % queue->size == queue->cons_idx % queue->size)
      && (queue->prod_idx / queue->size != queue->cons_idx / queue->size));
}

static inline void put_generic_q(generic_q_t *queue, void *elem)
{
  void *e = NULL;
  int ret = 0;
  uint64_t curr_prod_idx = 0;
//  unsigned long flag = 0;
//  spin_lock_irqsave(&queue->lock, flag);
  if (!generic_q_full(queue)) {
    do {
      curr_prod_idx = queue->prod_idx;
      ret = __sync_val_compare_and_swap(&queue->prod_idx, curr_prod_idx, curr_prod_idx+1);
    } while (ret != curr_prod_idx);

    curr_prod_idx = curr_prod_idx % queue->size;
    e = (void *) ((unsigned long) queue->elem_arr
        + (curr_prod_idx * queue->elem_size));
    memcpy(e, elem, queue->elem_size);
  }
//  spin_unlock_irqrestore(&queue->lock, flag);
}

typedef struct rx_ssl_elem {
  uint64_t ts;
  uint64_t len;
  uint32_t sys_tid;
  uint32_t reqs;
  uint8_t fd;
  uint8_t caller;
  uint8_t epfd;
  uint8_t epret;
  uint8_t epmask;
  uint8_t thread_stop:4,
          time_stop:4;
} rx_ssl_elem_t;

extern generic_q_t *rx_ssl_q[MAX_STATQ_ARRAYS];
extern void do_print_rx_ssl_q(generic_q_t *queue);

#endif /* __STATQ_H__  */
