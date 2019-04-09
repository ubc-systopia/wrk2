// Copyright (C) 2012 - Will Glozer.  All rights reserved.

#include "wrk.h"
#include "script.h"
#include "main.h"
#include "hdr_histogram.h"
#include "stats.h"

// Max recordable latency of 1 day
#define MAX_LATENCY 24L * 60 * 60 * 1000000

static struct config {
#if SME_CLIENT
    uint64_t num_reqs;
#endif
    uint64_t threads;
    uint64_t connections;
    uint64_t duration;
    uint64_t timeout;
    uint64_t pipeline;
    uint64_t rate;
    uint64_t delay_ms;
    bool     latency;
    bool     u_latency;
    bool     dynamic;
    bool     record_all_responses;
    char    *host;
    char    *script;
    SSL_CTX *ctx;
} cfg;

static struct {
    stats *requests;
    pthread_mutex_t mutex;
} statistics;

static struct sock sock = {
    .connect  = sock_connect,
    .close    = sock_close,
    .read     = sock_read,
    .write    = sock_write,
    .readable = sock_readable
};

static struct http_parser_settings parser_settings = {
    .on_message_complete = response_complete
};

static volatile sig_atomic_t stop = 0;

static void handler(int sig) {
    stop = 1;
}

static void usage() {
    printf("Usage: wrk <options> <url>                            \n"
           "  Options:                                            \n"
           "    -c, --connections <N>  Connections to keep open   \n"
           "    -d, --duration    <T>  Duration of test           \n"
           "    -t, --threads     <N>  Number of threads to use   \n"
           "                                                      \n"
           "    -s, --script      <S>  Load Lua script file       \n"
           "    -n, --num_reqs    <N>  Max #reqs per conn.        \n"
           "    -H, --header      <H>  Add header to request      \n"
           "    -L  --latency          Print latency statistics   \n"
           "    -U  --u_latency        Print uncorrected latency statistics\n"
           "        --timeout     <T>  Socket/request timeout in ms     \n"
           "    -B, --batch_latency    Measure latency of whole   \n"
           "                           batches of pipelined ops   \n"
           "                           (as opposed to each op)    \n"
           "    -v, --version          Print version details      \n"
           "    -R, --rate        <T>  work rate (throughput)     \n"
           "                           in requests/sec (total)    \n"
           "                           [Required Parameter]       \n"
           "                                                      \n"
           "                                                      \n"
           "  Numeric arguments may include a SI unit (1k, 1M, 1G)\n"
           "  Time arguments may include a time unit (2s, 2m, 2h)\n");
}

int main(int argc, char **argv) {
    char *url, **headers = zmalloc(argc * sizeof(char *));
    struct http_parser_url parts = {};

    if (parse_args(&cfg, &url, &parts, headers, argc, argv)) {
        usage();
        exit(1);
    }

    char *schema  = copy_url_part(url, &parts, UF_SCHEMA);
    char *host    = copy_url_part(url, &parts, UF_HOST);
    char *port    = copy_url_part(url, &parts, UF_PORT);
    char *service = port ? port : schema;

    if (!strncmp("https", schema, 5)) {
        if ((cfg.ctx = ssl_init()) == NULL) {
            fprintf(stderr, "unable to initialize SSL\n");
            ERR_print_errors_fp(stderr);
            exit(1);
        }
        sock.connect  = ssl_connect;
        sock.close    = ssl_close;
        sock.read     = ssl_read;
        sock.write    = ssl_write;
        sock.readable = ssl_readable;
    }
	
    cfg.host = host;
	
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  SIG_IGN);

    pthread_mutex_init(&statistics.mutex, NULL);
    statistics.requests = stats_alloc(10);
    thread *threads = zcalloc(cfg.threads * sizeof(thread));

    hdr_init(1, MAX_LATENCY, 3, &(statistics.requests->histogram));


    lua_State *L = script_create(cfg.script, url, headers);
    if (!script_resolve(L, host, service)) {
        char *msg = strerror(errno);
        fprintf(stderr, "unable to connect to %s:%s %s\n", host, service, msg);
        exit(1);
    }

    uint64_t connections = cfg.connections / cfg.threads;
    double throughput    = (double)cfg.rate / cfg.threads;
    uint64_t stop_at     = time_us() + (cfg.duration * 1000000);

#if SME_CLIENT
    printf("=== SME Paced Client ===\nPer thread Xput: %lf, Rate: %lf, Thread count %"PRIu64" \n", throughput, (double)cfg.rate, cfg.threads);
    printf("Asyncronous client? %s \n Randomised Start Of Threads +[0, %d]? %s \n Randomised Inter Request spacing +[0, %d]? %s \n ----------------------- \n", 
        SME_ASYNC_CLIENT? "TRUE" :"FALSE", 
        RANDOMIZATION_US,
        SME_STAGGER_WORKERS?  "TRUE" : "FALSE",
        RANDOMIZATION_US,
        SME_RANDOMIZE_IRQ? "TRUE" : "FALSE"
        );
    uint64_t start    = time_us();
#endif


    for (uint64_t i = 0; i < cfg.threads; i++) {
        thread *t = &threads[i];
        t->loop        = aeCreateEventLoop(10 + cfg.connections * 3);
        t->connections = connections;
        t->throughput = throughput;
        t->stop_at     = stop_at;
#if SME_CLIENT
        t->id = i;
        t->start_at     = start;
#endif

        t->L = script_create(cfg.script, url, headers);
        script_init(L, t, argc - optind, &argv[optind]);

        if (i == 0) {
            cfg.pipeline = script_verify_request(t->L);
            cfg.dynamic = !script_is_static(t->L);
            if (script_want_response(t->L)) {
                parser_settings.on_header_field = header_field;
                parser_settings.on_header_value = header_value;
                parser_settings.on_body         = response_body;
            }
        }

        if (!t->loop || pthread_create(&t->thread, NULL, &thread_main, t)) {
            char *msg = strerror(errno);
            fprintf(stderr, "unable to create thread %"PRIu64": %s\n", i, msg);
            exit(2);
        }
#if SME_CLIENT && SME_STAGGER_WORKERS
       usleep(rand() % RANDOMIZATION_US);
#endif   
    }
    struct sigaction sa = {
        .sa_handler = handler,
        .sa_flags   = 0,
    };
    sigfillset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    char *time = format_time_s(cfg.duration);
    printf("Running %s test @ %s\n", time, url);
    printf("  %"PRIu64" threads and %"PRIu64" connections\n",
            cfg.threads, cfg.connections);
#if !SME_CLIENT
   uint64_t start    = time_us();
   // start    = time_us();
#endif
    uint64_t complete = 0;
#if SME_CLIENT
    uint64_t post_warmup_total_reqs_count = 0;
    uint64_t total_reqs_count = 0;
    uint64_t post_warmup_total_reqs_written_count = 0;
    uint64_t total_reqs_written_count = 0;
    errors post_warmup_errors     = { 0 };
#endif
    uint64_t bytes    = 0;
    errors errors     = { 0 };

    struct hdr_histogram* latency_histogram;
    hdr_init(1, MAX_LATENCY, 3, &latency_histogram);
    struct hdr_histogram* u_latency_histogram;
    hdr_init(1, MAX_LATENCY, 3, &u_latency_histogram);

    for (uint64_t i = 0; i < cfg.threads; i++) {
        thread *t = &threads[i];
        pthread_join(t->thread, NULL);
    }

    uint64_t runtime_us = time_us() - start;

    for (uint64_t i = 0; i < cfg.threads; i++) {
        thread *t = &threads[i];
        complete += t->complete;
        bytes    += t->bytes;
#if SME_CLIENT
        for(uint64_t j = 0; j < t-> connections; j++){
          post_warmup_total_reqs_count += t->cs[j].all_requests_count - t->cs[j].all_requests_count_at_calibration;
          total_reqs_count += t->cs[j].all_requests_count;

          post_warmup_total_reqs_written_count += t->cs[j].all_requests_written_count - t->cs[j].all_requests_written_count_at_calibration;
          total_reqs_written_count += t->cs[j].all_requests_written_count;
#if SME_ASYNC_CLIENT
          //printf("Freeing_up_queues\n");
          delete_all(&(t->cs[j].head_time), &(t->cs[j].tail_time));
          delete_all(&(t->cs[j].rand_head_time), &(t->cs[j].rand_tail_time));
#endif
        }
#endif

#if SME_CLIENT
        post_warmup_errors.connect += t->errors.connect - t->errors_at_calibration.connect;
        post_warmup_errors.read    += t->errors.read - t->errors_at_calibration.read;
        post_warmup_errors.write   += t->errors.write - t->errors_at_calibration.write;
        post_warmup_errors.timeout += t->errors.timeout - t->errors_at_calibration.timeout;
        post_warmup_errors.status  += t->errors.status - t->errors_at_calibration.status;

#endif
        errors.connect += t->errors.connect;
        errors.read    += t->errors.read;
        errors.write   += t->errors.write;
        errors.timeout += t->errors.timeout;
        errors.status  += t->errors.status;

        hdr_add(latency_histogram, t->latency_histogram);
        hdr_add(u_latency_histogram, t->u_latency_histogram);
    }

    long double runtime_s   = runtime_us / 1000000.0;
#if SME_CLIENT
    runtime_s = runtime_s < cfg.duration? runtime_s : cfg.duration;
//    long double warm_runtime_s = runtime_us / 1000000.0 - CALIBRATE_DELAY_MS/1000;
//    I am not using the actual time it took to reach here because it adds more time than
//    the actual time requests were allowed to happen. A minimum runtime of 1 sec is used as a default
    long double warm_runtime_s = (runtime_s - CALIBRATE_DELAY_MS/1000.0) <= 1 ? 1 : (runtime_s - CALIBRATE_DELAY_MS/1000.0);
    long double post_warmup_all_req_per_s   = post_warmup_total_reqs_count   / warm_runtime_s;
    long double post_warmup_all_complete_req_per_s   = (post_warmup_total_reqs_count - post_warmup_errors.timeout)  / warm_runtime_s;
    long double all_req_per_s   = total_reqs_count   / runtime_s;
#endif
    long double req_per_s   = complete   / runtime_s;
    long double bytes_per_s = bytes      / runtime_s;

    stats *latency_stats = stats_alloc(10);
    latency_stats->min = hdr_min(latency_histogram);
    latency_stats->max = hdr_max(latency_histogram);
    latency_stats->histogram = latency_histogram;

    print_stats_header();
    print_stats("Latency", latency_stats, format_time_us);
    print_stats("Req/Sec", statistics.requests, format_metric);
//    if (cfg.latency) print_stats_latency(latency_stats);

    if (cfg.latency) {
        print_hdr_latency(latency_histogram,
                "Recorded Latency");
        printf("----------------------------------------------------------\n");
    }

    if (cfg.u_latency) {
        printf("\n");
        print_hdr_latency(u_latency_histogram,
                "Uncorrected Latency (measured without taking delayed starts into account)");
        printf("----------------------------------------------------------\n");
    }

    char *runtime_msg = format_time_us(runtime_us);

    printf("  %"PRIu64" requests in %s, %sB read\n",
            complete, runtime_msg, format_binary(bytes));
    if (errors.connect || errors.read || errors.write || errors.timeout) {

#if SME_CLIENT
        printf("  Post Warmup: Socket errors: connect %d, read %d, write %d"
            ", timeout %d\n"
            , post_warmup_errors.connect, post_warmup_errors.read
            , post_warmup_errors.write, post_warmup_errors.timeout);
#endif
        printf("  Socket errors: connect %d, read %d, write %d, timeout %d \n",
               errors.connect, errors.read, errors.write, errors.timeout);
    }

    if (errors.status) {
#if SME_CLIENT
        printf("  Post Warmup: Non-2xx or 3xx responses: %d\n"
            , post_warmup_errors.status);
#endif
        printf("  Non-2xx or 3xx responses: %d\n", errors.status);
    }

#if SME_CLIENT
    printf("\nExperiment Duration: %lu, Configured Warmup time: %d"
        ", Post Warmup time: %lf\n"
        , cfg.duration, CALIBRATE_DELAY_MS/1000
        , (cfg.duration - CALIBRATE_DELAY_MS/1000.0));
    printf("Post Warmup: Total Requests (incl timeouts): %"PRIu64"\n"
        , post_warmup_total_reqs_count);
    printf("Post Warmup: Total Requests Written(incl timeouts): %"PRIu64"\n"
        , post_warmup_total_reqs_written_count);
    printf("Post Warmup: Total Requests/sec (incl timeouts): %9.2Lf\n"
        , post_warmup_all_req_per_s);
    printf("Post Warmup: Total Requests/sec: %9.2Lf\n"
        , post_warmup_all_complete_req_per_s);
//    printf("Post Warmup time: %9.2Lf\n", warm_runtime_s);

    printf("Total Requests (incl timeouts): %"PRIu64"\n"
        , total_reqs_count);
    printf("Total Requests Written(incl timeouts): %"PRIu64"\n"
        , total_reqs_written_count);
    printf("Total Requests/sec (incl timeouts): %9.2Lf\n"
        , all_req_per_s);
#endif
    printf("Requests/sec: %9.2Lf\n", req_per_s);
    printf("Transfer/sec: %10sB\n", format_binary(bytes_per_s));

    if (script_has_done(L)) {
        script_summary(L, runtime_us, complete, bytes);
        script_errors(L, &errors);
        script_done(L, latency_stats, statistics.requests);
    }

#if SME_CLIENT
    sleep(2);
#endif
    return 0;
}

void *thread_main(void *arg) {
    thread *thread = arg;
    aeEventLoop *loop = thread->loop;

    thread->cs = zcalloc(thread->connections * sizeof(connection));
    tinymt64_init(&thread->rand, time_us());
    hdr_init(1, MAX_LATENCY, 3, &thread->latency_histogram);
    hdr_init(1, MAX_LATENCY, 3, &thread->u_latency_histogram);

    char *request = NULL;
    size_t length = 0;

    if (!cfg.dynamic) {
        script_request(thread->L, &request, &length);
    }

    double throughput = (thread->throughput / 1000000.0) / thread->connections;

    //printf("Thread Xput: %lf \n", throughput);
    connection *c = thread->cs;

    for (uint64_t i = 0; i < thread->connections; i++, c++) {
        c->thread     = thread;
        c->ssl        = cfg.ctx ? SSL_new(cfg.ctx) : NULL;
        c->request    = request;
        c->length     = length;
        c->throughput = throughput;
#if SME_CLIENT
        c->catch_up_throughput = throughput; // * 2;
        c->all_requests_count = 0;
        c->all_requests_count_at_calibration = 0;
        c->all_requests_written_count = 0;
#if SME_RANDOMIZE_IRQ
        c->rand_as_of_all_requests_written_count = 0;
#endif
#if SME_ASYNC_CLIENT
        c->id = thread->id * thread->connections + i;
        c->head_time = NULL;
        c->tail_time = NULL;
        c->rand_head_time = NULL;
        c->rand_tail_time = NULL;
        c->rand_as_of_all_requests_written_count = 0;
#endif
#else
        c->catch_up_throughput = throughput * 2;
#endif
        c->complete   = 0;
        c->caught_up  = true;

        // Stagger connects 5 msec apart within thread:
        aeCreateTimeEvent(loop, i * 5, delayed_initial_connect, c, NULL);
    }


#if SME_CLIENT
    uint64_t calibrate_delay = CALIBRATE_DELAY_MS - (time_us() - thread->start_at)/1000; //- rand()% 100; //+ (thread->connections * 25);
    uint64_t timeout_delay = cfg.timeout; // TIMEOUT_INTERVAL_MS; // + (thread->connections * 5);
#else
    uint64_t calibrate_delay = CALIBRATE_DELAY_MS + (thread->connections * 5);
    uint64_t timeout_delay = TIMEOUT_INTERVAL_MS + (thread->connections * 5);
#endif

    aeCreateTimeEvent(loop, calibrate_delay, calibrate, thread, NULL);

#if SME_CLIENT
    aeCreateTimeEvent(loop, timeout_delay/4, check_timeouts, thread, NULL);
#else
    aeCreateTimeEvent(loop, timeout_delay, check_timeouts, thread, NULL);
#endif
    thread->start = time_us();
    aeMain(loop);

    aeDeleteEventLoop(loop);
    zfree(thread->cs);

    return NULL;
}

static int connect_socket(thread *thread, connection *c) {
    struct addrinfo *addr = thread->addr;
    struct aeEventLoop *loop = thread->loop;
    int fd, flags;

    fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#if SME_CLIENT
    // TIMEOUT_INTERVAL_MS;
    c->request_written = 0;
#endif
    if (connect(fd, addr->ai_addr, addr->ai_addrlen) == -1) {
        if (errno != EINPROGRESS) goto error;
    }

    flags = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flags, sizeof(flags));
#if SME_DBG
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(fd, (struct sockaddr *)&sin, &len) == -1)
      perror("getsockname");
    else
      printf("port number of fd %d is %d\n", fd, ntohs(sin.sin_port));
#endif

#if SME_CLIENT
    struct timeval tv;
    tv.tv_sec = cfg.timeout/1000;
    tv.tv_usec = (cfg.timeout%1000);
#if SME_DBG
    printf("Setting timeout on socket to: %lld \n", tv.tv_sec + tv.tv_usec);
#endif
    //printf("Although configured time is: %lld \n", cfg.timeout);
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0){
      printf("Cannot Set SO_RCVTIMEO for socket\n");
    }
#endif

    c->latest_connect = time_us();
    c->has_pending = false;
    c->pending= 0;

    flags = AE_READABLE | AE_WRITABLE;
    //printf("Will try Connecting socket %i\n", fd);
    if (aeCreateFileEvent(loop, fd, flags, socket_connected, c) == AE_OK) {
        c->parser.data = c;
        c->fd = fd;
        return fd;
    }

  error:
    thread->errors.connect++;
    close(fd);
#if SME_DBG
    printf("Connection Error fd: %i \n", fd);
#endif
    return -1;
}

static int reconnect_socket(thread *thread, connection *c) {
    aeDeleteFileEvent(thread->loop, c->fd, AE_WRITABLE | AE_READABLE);
    sock.close(c);
    close(c->fd);
#if SME_DBG
    printf("Reconnecting socket %i\n", c->fd);
#endif
    return connect_socket(thread, c);
}

static int delayed_initial_connect(aeEventLoop *loop, long long id, void *data) {
    connection* c = data;
    c->thread_start = time_us();
//    printf("Delayed Initial connect on  socket %i\n", c->fd);
    connect_socket(c->thread, c);
    return AE_NOMORE;
}

static int calibrate(aeEventLoop *loop, long long id, void *data) {
    thread *thread = data;

    long double mean = hdr_mean(thread->latency_histogram);
    long double latency = hdr_value_at_percentile(
            thread->latency_histogram, 90.0) / 1000.0L;
    long double interval = MAX(latency * 2, 10);

    if (mean == 0) return CALIBRATE_DELAY_MS;

    thread->mean     = (uint64_t) mean;
    hdr_reset(thread->latency_histogram);
    hdr_reset(thread->u_latency_histogram);

    thread->start    = time_us();
#if SME_CLIENT
    for(uint64_t j = 0; j < thread->connections; j++){
//       thread->cs[j].thread_start = thread->start;
       thread->cs[j].all_requests_count_at_calibration = thread->cs[j].all_requests_count;
       thread->cs[j].all_requests_written_count_at_calibration = thread->cs[j].all_requests_written_count;
//       thread->cs[j].just_calibrated = 1;
//       thread->cs[j].all_requests_count_at_last_batch_start = 0;
    }
    memcpy(&thread->errors_at_calibration,&thread->errors,sizeof(errors)); //shallow copy of s1 INTO s2?

#endif

    thread->interval = interval;
    thread->requests = 0;

    printf("  Thread calibration: mean lat.: %.3fms, rate sampling interval: %dms\n",
            (thread->mean)/1000.0,
            thread->interval);

#if !SME_CLIENT
    aeCreateTimeEvent(loop, thread->interval, sample_rate, thread, NULL);
#endif

    return AE_NOMORE;
}

static int check_timeouts(aeEventLoop *loop, long long id, void *data) {
    thread *thread = data;
    connection *c  = thread->cs;
    uint64_t now   = time_us();
    uint64_t maxAge = now - (cfg.timeout * 1000);
#if SME_DBG
    printf("Checking timeout at: %lu \n",now );
#endif
    for (uint64_t i = 0; i < thread->connections; i++, c++) {

#if SME_DBG
    printf("\tChecking request time out at time  %lu, %lu after last check"
        ", requests_written? %d, maxAge %lu, c->start %lu, c->latest_write %lu"
        ", req_timed_out? %d, should stop? %d, fd %d\n",
        now, now - c->last_timeout_check,  c->request_written, maxAge, c->start
        , c->latest_write,  maxAge > c->start, thread->stop_at < now , c->fd);
    c->last_timeout_check = now;
#endif

#if SME_CLIENT && !SME_ASYNC_CLIENT
        if (maxAge > c->start && c->request_written == 1 && thread->stop_at > now ) {
#elif SME_CLIENT && SME_ASYNC_CLIENT
// If the client is ASYNC, we should compare to the earlilest request written
        if (maxAge > peak(c->head_time) && c->request_written == 1 && thread->stop_at > now) {
#else
        if (maxAge > c->start) {
#endif
            thread->errors.timeout++;
#if SME_DBG
            printf("A request timed out on fd %d after %lu"
                ", original write at: %lu\n"
                , c->fd, now - c->latest_write, c->start);
            printf("\tChecking request time out at time  %lu, %lu"
                " after last check, requests_written? %d, maxAge %lu"
                ", c->start %lu, c->latest_write %lu, req_timed_out? %d"
                ", should stop? %d thread->start %lu\n"
                , now, now - c->last_timeout_check, c->request_written
                , maxAge, c->start, c->latest_write, maxAge > c->start
                , thread->stop_at < now, now - thread->start);
#endif

            c->all_requests_count++;
#if SME_CLIENT && !SME_ASYNC_CLIENT
            //stop = 1;
            //if (c->all_requests_count % 101 == 0){
            reconnect_socket(thread, c);
            //}
            //aeDeleteFileEvent(thread->loop, c->fd, AE_READABLE);
            //aeDeleteFileEvent(thread->loop, c->fd, AE_WRITABLE);
#endif
        }
    }

    if (stop || now >= thread->stop_at) {
        aeStop(loop);
    }

#if SME_CLIENT
    return cfg.timeout/4;// TIMEOUT_INTERVAL_MS;
#else
    return cfg.timeout;// TIMEOUT_INTERVAL_MS;
#endif
}

#if !SME_CLIENT
static int sample_rate(aeEventLoop *loop, long long id, void *data) {
    thread *thread = data;

    uint64_t elapsed_ms = (time_us() - thread->start) / 1000;
    uint64_t requests = (thread->requests / (double) elapsed_ms) * 1000;

    pthread_mutex_lock(&statistics.mutex);
    stats_record(statistics.requests, requests);
    pthread_mutex_unlock(&statistics.mutex);

    thread->requests = 0;
    thread->start    = time_us();

    return thread->interval;
}
#endif

static int header_field(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    if (c->state == VALUE) {
        *c->headers.cursor++ = '\0';
        c->state = FIELD;
    }
    buffer_append(&c->headers, at, len);
    return 0;
}

static int header_value(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    if (c->state == FIELD) {
        *c->headers.cursor++ = '\0';
        c->state = VALUE;
    }
    buffer_append(&c->headers, at, len);
    return 0;
}

static int response_body(http_parser *parser, const char *at, size_t len) {
    connection *c = parser->data;
    buffer_append(&c->body, at, len);
    return 0;
}

static uint64_t usec_to_next_send(connection *c) {
    uint64_t now = time_us();
#if SME_CLIENT && SME_ASYNC_CLIENT
    uint64_t next_start_time = c->thread_start
      + (c->all_requests_written_count / c->throughput);
#if SME_DBG
    printf("Next_start_time for c id: %d, fd: %d at time %ld"
        ", all_requests_written_count: %lu, idx: %lu \n"
        , c->id, c->fd, next_start_time, c->all_requests_written_count
        , c->id + c->all_requests_written_count);
#endif

#elif SME_CLIENT
    uint64_t next_start_time = c->thread_start + (c->all_requests_count / c->throughput);
#else
    uint64_t next_start_time = c->thread_start + (c->complete / c->throughput);
#endif


#if SME_CLIENT && SME_RANDOMIZE_IRQ
    uint64_t next_random;
    if (c->all_requests_written_count == c->rand_as_of_all_requests_written_count) {
      next_random = rand() % (REQUEST_RANDOMIZATION_US);
      //next_random = rand() % (2*REQUEST_RANDOMIZATION_US);

#if SME_CLIENT && SME_ASYNC_CLIENT
      insert(next_random, &(c->rand_head_time), &(c->rand_tail_time));
#elif SME_CLIENT
      c->rand_write_delay = next_random;
#endif
      c->rand_as_of_all_requests_written_count = c->all_requests_written_count + 1;
    } else {

#if SME_CLIENT && SME_ASYNC_CLIENT
      next_random = peak(c->rand_tail_time);
#elif SME_CLIENT
      next_random = c->rand_write_delay;
#endif

    }

    next_start_time = next_start_time + next_random;
#if SME_DBG
    printf("Req on fd %d, next_random %lu next_start_time %lu all_req_count %lu\n"
        , c->fd, next_random, next_start_time, c->all_requests_count);
#endif
#endif

    bool send_now = true;
    if (next_start_time > now) {
        // We are on pace. Indicate caught_up and don't send now.
        c->caught_up = true;
        send_now = false;
#if SME_CLIENT && SME_DBG
      printf("We are Good! by %lld, time now : %lld, thread started at: %lld"
          ", total requests count: %d, xput %lf , next start time: %lld\n"
          , next_start_time - now, now, c->thread_start, c->all_requests_count
          , c->throughput, next_start_time);
#endif
    } else {
#if SME_CLIENT && SME_DBG
      printf("We are behind by %lld, time now : %lld, thread started at: %lld"
          ", total requests count: %d, xput %lf , next start time: %lld\n"
          , now-next_start_time, now, c->thread_start, c->all_requests_count
          , c->throughput, next_start_time);
#endif
    }

    if (send_now) {
        c->latest_should_send_time = now;
        c->latest_expected_start = next_start_time;
    }
    //printf("Time for next send: %lld\n",next_start_time - now);
    return send_now ? 0 : (next_start_time - now);
}

static int delay_request(aeEventLoop *loop, long long id, void *data) {
    connection* c = data;
    uint64_t time_usec_to_wait = usec_to_next_send(c);
    if (time_usec_to_wait) {
        return round((time_usec_to_wait / 1000.0L) ); /* don't send, wait */
    }
//    aeCreateFileEvent(c->thread->loop, c->fd, AE_READABLE, socket_readable, c);
    aeCreateFileEvent(c->thread->loop, c->fd, AE_WRITABLE, socket_writeable, c);
#if SME_CLIENT && SME_ASYNC_CLIENT
#if SME_RANDOMIZE_IRQ
    double delay_for_next = 500/(c->throughput*1000000);// - RANDOMIZATION_US;
#else 
    double delay_for_next = 1000/(c->throughput*1000000);
#endif
    return (delay_for_next < 1)? 1: (int)delay_for_next; //
#else
    return AE_NOMORE;
#endif
}

static int response_complete(http_parser *parser) {
    connection *c = parser->data;
    thread *thread = c->thread;
    uint64_t now = time_us();
    int status = parser->status_code;
#if SME_DBG
    printf("Response recieved on fd %d for req %lu at time: %lld took %lu\n"
        , c->fd, c->start, now, now - c->start);

#endif
    thread->complete++;
    thread->requests++;

    if (status > 399) {
        thread->errors.status++;
    }

    if (c->headers.buffer) {
        *c->headers.cursor++ = '\0';
        script_response(thread->L, status, &c->headers, &c->body);
        c->state = FIELD;
    }

    // Count all responses (including pipelined ones:)
    c->complete++;
#if SME_CLIENT
    c->all_requests_count++;
#endif

    if (now >= thread->stop_at) {
        aeStop(thread->loop);
        goto done;
    }

    //***Batched requests latencies and no longer guaranteed to work with SME_CLIENT***
    // Note that expected start time is computed based on the completed
    // response count seen at the beginning of the last request batch sent.
    // A single request batch send may contain multiple requests, and
    // result in multiple responses. If we incorrectly calculated expect
    // start time based on the completion count of these individual pipelined
    // requests we can easily end up "gifting" them time and seeing
    // negative latencies.
//#if SME_CLIENT && SME_ASYNC_CLIENT
//    srand(c->id + c->all_requests_count - 1);
//    uint64_t expected_latency_start = c->thread_start +
//            ((c->all_requests_count - 1) / c->throughput);
#if SME_CLIENT
    uint64_t expected_latency_start = c->thread_start +
            ((c->all_requests_count -1 )/ c->throughput);
#else
    uint64_t expected_latency_start = c->thread_start +
            (c->complete_at_last_batch_start / c->throughput);
#endif

#if SME_CLIENT && SME_RANDOMIZE_IRQ

#if SME_CLIENT && SME_ASYNC_CLIENT
      uint64_t req_random = delete(&(c->rand_head_time), &(c->rand_tail_time));
#elif SME_CLIENT
      uint64_t req_random = c->rand_write_delay;
#endif
    //expected_latency_start = expected_latency_start - RANDOMIZATION_US + req_random;
    expected_latency_start = expected_latency_start + req_random;
#if SME_DBG
    printf("Req on fd %d, Req_random = %lu, expected_latency_start %lu"
        ", actual_start %lu \n"
        , c->fd, req_random, expected_latency_start, c->start);
#endif
    int64_t expected_latency_timing = now - expected_latency_start;
#else

    int64_t expected_latency_timing = now - expected_latency_start;
#endif
    if (expected_latency_timing < 0) {
        printf("\n\n ---------- \n\n");
        printf("We are about to crash and die (recoridng a negative #)");
        printf("This wil never ever ever happen...");
        printf("But when it does. The following information will help in debugging");
        printf("response_complete:\n");
        printf("  expected_latency_timing = %lu\n", expected_latency_timing);
        printf("  now = %lu\n", now);
        printf("  expected_latency_start = %lu\n", expected_latency_start);
        printf("  c->thread_start = %lu\n", c->thread_start);
        printf("  c->complete = %lu\n", c->complete);
#if SME_CLIENT
        printf("  c->all_requests_count = %lu\n", c->all_requests_count);
#endif
        printf("  throughput = %g\n", c->throughput);
        printf("  latest_should_send_time = %lu\n", c->latest_should_send_time);
        printf("  latest_expected_start = %lu\n", c->latest_expected_start);
        printf("  latest_connect = %lu\n", c->latest_connect);
        printf("  latest_write = %lu\n", c->latest_write);

#if SME_CLIENT
        // We would like to seed the calculation with the same seed that was
        // used to randomise the request, i.e. we need to take one off
        expected_latency_start = c->thread_start +
            (c->all_requests_count / c->throughput);
#else
        expected_latency_start = c->thread_start +
                ((c->complete ) / c->throughput);
#endif
        printf("  next expected_latency_start = %lu\n", expected_latency_start);
    }

    c->latest_should_send_time = 0;
    c->latest_expected_start = 0;

    // Record if needed, either last in batch or all, depending in cfg:
    if (cfg.record_all_responses || !c->has_pending) {

        hdr_record_value(thread->latency_histogram, expected_latency_timing);
#if SME_CLIENT && SME_ASYNC_CLIENT
        uint64_t head_req_time = delete(&(c->head_time), &(c->tail_time));
#if SME_DBG
        printf("Recieved Response for request on c id: %d, fd: %d at time %lu"
            ", all_requests_received_count: %d, idx: %d, expected_latency %lu"
            ", req_time %lu,  actual_latency: %lu \n"
            , c->id, c->fd, now, c->all_requests_count -1
            , c->id + c->all_requests_count, expected_latency_timing
            , head_req_time, now-head_req_time);
#endif
        uint64_t actual_latency_timing = now - head_req_time;//- delete(c->head_time, c->tail_time);
        //uint64_t actual_latency_timing = now - c->actual_latency_start;
#else

        uint64_t actual_latency_timing = now - c->actual_latency_start;
#endif

#if SME_DBG
        if (actual_latency_timing > TIMEOUT_INTERVAL_MS*1000) {
            //thread->errors.timeout++;
            printf("Request is getting added to the hdr, although it timed out."
                " Request is on fd %d Latency is %d\n"
                , c->fd, actual_latency_timing);
        }

        if (actual_latency_timing > expected_latency_timing) {
           //thread->errors.timeout++;
           printf("BUG happens. Request is on fd %d Expected Latency is: %lu"
               " actual latency is: %lu, actual_start %lu , c->start %lu"
               ", now %lu, rand used %d , c->has_pending %d\n"
               , c->fd, expected_latency_timing, actual_latency_timing
               , c->actual_latency_start, c->start, now, c->rand_write_delay
               , c->has_pending);
        } else {
           printf("BUG doesnt happen. Request is on fd %d Expected Latency is: %lu"
               " actual latency is: %lu, actual_start %lu , c->start %lu"
               ", now %lu, rand used %d , c->has_pending %d\n"
               , c->fd, expected_latency_timing, actual_latency_timing
               , c->actual_latency_start, c->start, now, c->rand_write_delay
               , c->has_pending);
        }
#endif

        hdr_record_value(thread->u_latency_histogram, actual_latency_timing);
#if 0//SME_CLIENT
      }
#endif

#if 0// SME_CLIENT
    if (c->just_calibrated){
       c->thread->start = time_us();
       c->thread_start = c->thread->start;
       c->all_requests_count = 1;
       c->all_requests_written_count = 1;
       c->rand_as_of_all_requests_written_count = 1;
       c->all_requests_count_at_last_batch_start = 0;     
       c->just_calibrated = 0;
    }
#endif
    }

#if SME_CLIENT
    if (c->all_requests_written_count >= cfg.num_reqs){
        aeStop(thread->loop);
        goto done;
    }
#endif

#if !(SME_CLIENT && SME_ASYNC_CLIENT)
    if (--c->pending == 0) {
        c->has_pending = false;
        aeCreateFileEvent(thread->loop, c->fd, AE_WRITABLE, socket_writeable, c);
    }
#endif

    if (!http_should_keep_alive(parser)) {
        reconnect_socket(thread, c);
        goto done;
    }

    http_parser_init(parser, HTTP_RESPONSE);

  done:
    return 0;
}

static void socket_connected(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;
#if SME_CLIENT &&  SME_ASYNC_CLIENT
    if(c->connected) return;
#endif
    //printf("Call to socket_connected on fd: %i at: %lu for c %p\n", fd, now, c);
    switch (sock.connect(c, cfg.host)) {
        case OK:    break;
        case ERROR: goto error;
        case RETRY: return;
    }

    http_parser_init(&c->parser, HTTP_RESPONSE);
    c->written = 0;
#if SME_CLIENT
    c->request_written = 0;
#endif

#if SME_CLIENT && SME_ASYNC_CLIENT
    aeCreateFileEvent(c->thread->loop, fd, AE_READABLE, socket_readable, c);
    //printf("sock_connected Time = %lu \n", time_us());
    aeCreateTimeEvent(c->thread->loop, 0, delay_request, c, NULL);
    //aeCreateTimeEvent(c->thread->loop, req_delay, delay_request, c, NULL);
#else
    aeCreateFileEvent(c->thread->loop, fd, AE_READABLE, socket_readable, c);
    aeCreateFileEvent(c->thread->loop, fd, AE_WRITABLE, socket_writeable, c);
#endif

    //printf("Connected on fd: %i at: %lu\n", fd, now);
#if SME_CLIENT && SME_ASYNC_CLIENT
    c->connected = 1;
#endif
    return;

  error:
    //printf("Error connecting on fd: %i at: %lu\n", fd, now);
    c->thread->errors.connect++;
    reconnect_socket(c->thread, c);

}

static void socket_writeable(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;
    thread *thread = c->thread;
#if SME_DBG
    uint64_t now = time_us();
#endif


#if SME_CLIENT
    if (c->all_requests_written_count >= cfg.num_reqs) {
        aeDeleteFileEvent(loop, fd, AE_WRITABLE);

//        aeStop(thread->loop);
        return;
    }
#endif

    if (!c->written) {
        uint64_t time_usec_to_wait = usec_to_next_send(c);
        if (time_usec_to_wait) {
            // SME: consider removing the additional 0.5
            int msec_to_wait = round((time_usec_to_wait / 1000.0L) + 0.5);

            // Not yet time to send. Delay:
            aeDeleteFileEvent(loop, fd, AE_WRITABLE);
            aeCreateTimeEvent(
                    thread->loop, msec_to_wait, delay_request, c, NULL);
            return;
        }
        c->latest_write = time_us();
    }

    if (!c->written && cfg.dynamic) {
        script_request(thread->L, &c->request, &c->length);
    }

    char  *buf = c->request + c->written;
    size_t len = c->length  - c->written;
    size_t n;

    if (!c->written) {
        c->start = time_us();

        if (!c->has_pending) {
            c->actual_latency_start = c->start;
            c->complete_at_last_batch_start = c->complete;

#if SME_CLIENT
            c->all_requests_count_at_last_batch_start = c->all_requests_count;
#endif
        }
        if (!c->written && !c->has_pending) {
            c->has_pending = true;
        }
        c->pending = cfg.pipeline;

    }

    switch (sock.write(c, buf, len, &n)) {
        case OK:    break;
        case ERROR: goto error;
        case RETRY: return;
    }
    c->written += n;
    if (c->written == c->length) {
        c->written = 0;
        aeDeleteFileEvent(loop, fd, AE_WRITABLE);
    }
    //potential bug if write over multiple writes
#if SME_CLIENT && SME_ASYNC_CLIENT
    insert(c->start, &(c->head_time), &(c->tail_time));
    //list( c->head_time, c->tail_time);
#endif

#if SME_DBG
    uint64_t preparing_for_write = time_us() - now;
    printf("Written Request at: %lu, preparation and writing time took %lu\n"
        , c->start, preparing_for_write);
#endif
#if SME_CLIENT
    c->all_requests_written_count++;
    c->request_written = 1;
#endif
    return;

  error:
#if SME_DBG
    printf("Error Writing Request on fd: %i at: %lu\n", fd, time_us());
#endif
    thread->errors.write++;
    reconnect_socket(thread, c);
}


static void socket_readable(aeEventLoop *loop, int fd, void *data, int mask) {
    connection *c = data;
    size_t n;
#if SME_DBG
    uint64_t now;
#endif
#if SME_DBG
    now = time_us();
    printf("XReading socket after: %lu \n", now - c->start);
#endif
    do {
#if SME_DBG
      now = time_us();
      printf("pre http_parser_execution for fd %d for req sent at %lu  time is %lu took %lu \n",c->fd, c->start, now, time_us()-c->start);
#endif
     // if (now - c->start > cfg.timeout*1000) {
     //     c->all_requests_count++;
     //     c->thread->errors.timeout++;

     //     printf("TO Reading %lu bytes from fd: %i after %lu us from request at %lu\n", n, fd, now - c->start, c->start );
     //     reconnect_socket(c->thread, c);
     //     return;
     //  }
        switch (sock.read(c, &n)) {
            case OK:    break;
            case ERROR: goto error;
            case RETRY: return;
        }


#if SME_DBG
      printf("Reading %lu bytes from fd: %i after %lu us from request at %lu\n", n, fd, now - c->start, c->start );
#endif
        //now = time_us();
        if (http_parser_execute(&c->parser, &parser_settings, c->buf, n) != n) goto error;
        c->thread->bytes += n;
    } while (n == RECVBUF && sock.readable(c) > 0 );//&& (now - c->start < (cfg.timeout*1000) ))

#if SME_DBG
     printf("post http_parser_execution for fd %d for req at %lu time is %lu took %lu\n",c->fd, c->start, now, time_us()-c->start);
#endif
#if SME_CLIENT && !SME_ASYNC_CLIENT
    // TIMEOUT_INTERVAL_MS;
    c->request_written = 0;
#elif SME_CLIENT && SME_ASYNC_CLIENT
   if (peak(c->head_time) == -1){
      c->request_written = 0;
   }

#endif

    return;

  error:
#if SME_DBG
    printf("Error Reading %lu bytes from fd: %i after %lu us from request at %lu\n", n, fd, now - c->start, c->start );
#endif
    c->thread->errors.read++;
    reconnect_socket(c->thread, c);

}

static uint64_t time_us() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (t.tv_sec * 1000000) + t.tv_usec;
}

static char *copy_url_part(char *url, struct http_parser_url *parts, enum http_parser_url_fields field) {
    char *part = NULL;

    if (parts->field_set & (1 << field)) {
        uint16_t off = parts->field_data[field].off;
        uint16_t len = parts->field_data[field].len;
        part = zcalloc(len + 1 * sizeof(char));
        memcpy(part, &url[off], len);
    }

    return part;
}

static struct option longopts[] = {
    { "connections",    required_argument, NULL, 'c' },
    { "duration",       required_argument, NULL, 'd' },
    { "threads",        required_argument, NULL, 't' },
    { "script",         required_argument, NULL, 's' },
    { "num_reqs",       required_argument, NULL, 'n' },
    { "header",         required_argument, NULL, 'H' },
    { "latency",        no_argument,       NULL, 'L' },
    { "u_latency",      no_argument,       NULL, 'U' },
    { "batch_latency",  no_argument,       NULL, 'B' },
    { "timeout",        required_argument, NULL, 'T' },
    { "help",           no_argument,       NULL, 'h' },
    { "version",        no_argument,       NULL, 'v' },
    { "rate",           required_argument, NULL, 'R' },
    { NULL,             0,                 NULL,  0  }
};

static int parse_args(struct config *cfg, char **url, struct http_parser_url *parts, char **headers, int argc, char **argv) {
    char c, **header = headers;

    memset(cfg, 0, sizeof(struct config));
    cfg->threads     = 2;
    cfg->connections = 10;
    cfg->duration    = 10;
    cfg->timeout     = SOCKET_TIMEOUT_MS;
    //printf("Timeout Value: = %lld\n", SOCKET_TIMEOUT_MS);
    cfg->rate        = 0;
    cfg->record_all_responses = true;
    cfg->num_reqs = 9223372036854776;
    while ((c = getopt_long(argc, argv, "t:c:d:s:n:H:T:R:LUBrv?", longopts, NULL)) != -1) {
        switch (c) {
            case 't':
                if (scan_metric(optarg, &cfg->threads)) return -1;
                break;
            case 'c':
                if (scan_metric(optarg, &cfg->connections)) return -1;
                break;
            case 'd':
                if (scan_time(optarg, &cfg->duration)) return -1;
                break;
            case 's':
                cfg->script = optarg;
                break;
            case 'n':
                if (scan_metric(optarg, &cfg->num_reqs)) return -1;
                break;
            case 'H':
                *header++ = optarg;
                break;
            case 'L':
                cfg->latency = true;
                break;
            case 'B':
                cfg->record_all_responses = false;
                break;
            case 'U':
                cfg->latency = true;
                cfg->u_latency = true;
                break;
            case 'T':
                if (scan_time(optarg, &cfg->timeout)) return -1;
                cfg->timeout *= 1 ;
                printf("Timeout Value updated: = %lu\n", cfg->timeout);
                break;
            case 'R':
                if (scan_metric(optarg, &cfg->rate)) return -1;
                break;
            case 'v':
                printf("wrk %s [%s] ", VERSION, aeGetApiName());
                printf("Copyright (C) 2012 Will Glozer\n");
                break;
            case 'h':
            case '?':
            case ':':
            default:
                return -1;
        }
    }

    if (optind == argc || !cfg->threads || !cfg->duration) return -1;

    if (!script_parse_url(argv[optind], parts)) {
        fprintf(stderr, "invalid URL: %s\n", argv[optind]);
        return -1;
    }

    if (!cfg->connections || cfg->connections < cfg->threads) {
        fprintf(stderr, "number of connections must be >= threads\n");
        return -1;
    }

    if (cfg->rate == 0) {
        fprintf(stderr,
                "Throughput MUST be specified with the --rate or -R option\n");
        return -1;
    }

    *url    = argv[optind];
    *header = NULL;

    return 0;
}

static void print_stats_header() {
    printf("  Thread Stats%6s%11s%8s%12s\n", "Avg", "Stdev", "Max", "+/- Stdev");
}

static void print_units(long double n, char *(*fmt)(long double), int width) {
    char *msg = fmt(n);
    int len = strlen(msg), pad = 2;

    if (isalpha(msg[len-1])) pad--;
    if (isalpha(msg[len-2])) pad--;
    width -= pad;

    printf("%*.*s%.*s", width, width, msg, pad, "  ");

    free(msg);
}

static void print_stats(char *name, stats *stats, char *(*fmt)(long double)) {
    uint64_t max = stats->max;
    long double mean  = stats_summarize(stats);
    long double stdev = stats_stdev(stats, mean);

    printf("    %-10s", name);
    print_units(mean,  fmt, 8);
    print_units(stdev, fmt, 10);
    print_units(max,   fmt, 9);
    printf("%8.2Lf%%\n", stats_within_stdev(stats, mean, stdev, 1));
}

static void print_hdr_latency(struct hdr_histogram* histogram, const char* description) {
    long double percentiles[] = { 50.0, 75.0, 90.0, 99.0, 99.9, 99.99, 99.999, 100.0};
    printf("  Latency Distribution (HdrHistogram - %s)\n", description);
    for (size_t i = 0; i < sizeof(percentiles) / sizeof(long double); i++) {
        long double p = percentiles[i];
        int64_t n = hdr_value_at_percentile(histogram, p);
        printf("%7.3Lf%%", p);
        print_units(n, format_time_us, 10);
        printf("\n");
    }
    printf("\n%s\n", "  Detailed Percentile spectrum:");
    hdr_percentiles_print(histogram, stdout, 5, 1000.0, CLASSIC);
}

#if !SME_CLIENT
static void print_stats_latency(stats *stats) {
    long double percentiles[] = { 50.0, 75.0, 90.0, 99.0, 99.9, 99.99, 99.999, 100.0 };
    printf("  Latency Distribution\n");
    for (size_t i = 0; i < sizeof(percentiles) / sizeof(long double); i++) {
        long double p = percentiles[i];
        uint64_t n = stats_percentile(stats, p);
        printf("%7.3Lf%%", p);
        print_units(n, format_time_us, 10);
        printf("\n");
    }
}
#endif
