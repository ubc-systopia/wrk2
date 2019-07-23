#include <stdio.h>
#include <stdlib.h>
#include "generic_q.h"
#include "config.h"
#include "sme_debug.h"

void do_print_rx_ssl_q(generic_q_t *queue)
{
  uint64_t i;
  rx_ssl_elem_t *e;
  int64_t num_elems = queue->prod_idx - queue->cons_idx;

  if (num_elems == 0)
    return;

  wprint3(LVL_EXP, "==== [%d] port %u fd %u tid %u #elems %ld prod %lu cons %lu ===="
      , queue->qidx, queue->port, queue->fd, queue->sys_tid
      , num_elems, queue->prod_idx, queue->cons_idx
      );
  for (i = queue->cons_idx; i < queue->prod_idx; i++) {
    e = (rx_ssl_elem_t *) ((unsigned long) queue->elem_arr + (i * queue->elem_size));
    wprint3(LVL_EXP, "%lu %d fd [%d %d] R %d %lu EP %d %d 0x%0x S %d %d"
        , e->ts, e->caller, e->fd, e->sys_tid, e->reqs, e->len
        , e->epfd, e->epret, e->epmask, e->thread_stop, e->time_stop
        );
  }
}
