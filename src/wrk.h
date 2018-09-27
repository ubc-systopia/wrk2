#ifndef WRK_H
#define WRK_H

#include "config.h"
#include "list.h"
#include <pthread.h>
#include <inttypes.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <lua.h>

#include "stats.h"
#include "ae.h"
#include "http_parser.h"
#include "hdr_histogram.h"

#define VERSION  "4.0.0"
#define RECVBUF  8192
#define SAMPLES  100000000

//#if SME_CLIENT
//#define SOCKET_TIMEOUT_MS   20
//#define TIMEOUT_INTERVAL_MS 20
//#else
#define SOCKET_TIMEOUT_MS   2000
#define TIMEOUT_INTERVAL_MS 2000
//#endif
#define CALIBRATE_DELAY_MS  10000


typedef struct {
    pthread_t thread;
    aeEventLoop *loop;
    struct addrinfo *addr;
    uint64_t connections;
    int interval;
    uint64_t stop_at;
#if SME_CLIENT
    uint64_t start_at;
    int id;
#endif
    uint64_t complete;
    uint64_t requests;
    uint64_t bytes;
    uint64_t start;
    double throughput;
    uint64_t mean;
    struct hdr_histogram *latency_histogram;
    struct hdr_histogram *u_latency_histogram;
    tinymt64_t rand;
    lua_State *L;
    errors errors;
    struct connection *cs;
} thread;

typedef struct {
    char  *buffer;
    size_t length;
    char  *cursor;
} buffer;

typedef struct connection {
    thread *thread;
    http_parser parser;
    enum {
        FIELD, VALUE
    } state;
    int fd;
    SSL *ssl;
    double throughput;
    double catch_up_throughput;
    uint64_t complete;
    uint64_t complete_at_last_batch_start;
    uint64_t catch_up_start_time;
    uint64_t complete_at_catch_up_start;
    uint64_t thread_start;
    uint64_t start;
    char *request;
    size_t length;
    size_t written;
    uint64_t pending;
    buffer headers;
    buffer body;
    char buf[RECVBUF];
    uint64_t actual_latency_start;
    bool has_pending;
    bool caught_up;
#if SME_CLIENT
    node_t *head_time;
    node_t *rand_head_time;
    node_t *tail_time;
    node_t *rand_tail_time;
    uint64_t rand_write_delay;
    bool request_written;
    uint64_t all_requests_count_at_last_batch_start;
    uint64_t all_requests_count;
    uint64_t all_requests_count_at_calibration;
    uint64_t all_requests_written_count;
    uint64_t all_requests_written_count_at_calibration;
    uint64_t rand_as_of_all_requests_written_count;
    bool just_calibrated;
    int id;
    bool connected;
#endif
    uint64_t last_timeout_check;
    // Internal tracking numbers (used purely for debugging):
    uint64_t latest_should_send_time;
    uint64_t latest_expected_start;
    uint64_t latest_connect;
    uint64_t latest_write;
} connection;

#endif /* WRK_H */
