#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#define _GNU_SOURCE
#include <unistd.h>
#include <mpi.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>
#include <assert.h>
#include <string.h>

/* Number of RDMA buffers allowed to run in parallel */
#define NUM_RDMA_BUFFERS 128
#define NITERS (128)
#define NFLIGHT 12
#define MPI_ROOT_RANK 0
#define HOST_MAX_SIZE 16 /* Keep it short */
#define MPI_RANK_ANY -1

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

enum output_mode
{
    OUTPUT_MPI,
    OUTPUT_VERBOSE,
};

enum direction
{
    DIR_NONE = 0,
    DIR_PUT  = 1 << 0,
    DIR_GET  = 1 << 1,
};

const char * direction_str[] =
{
    [DIR_NONE] = "Und",
    [DIR_PUT]  = "Put",
    [DIR_GET]  = "Get",
};

#define MPI_CHECK(func)                                                        \
do {                                                                           \
    int rc;                                                                    \
    rc = func;                                                                 \
    if (rc) MPI_Abort(MPI_COMM_WORLD, rc);                                     \
} while(0)

enum operation
{
    OP_SUM = 0,
    OP_MIN,
    OP_MAX,
    _OP_LAST,
};

MPI_Datatype results_dtype;
MPI_Op       results_op[_OP_LAST];
MPI_Comm     clients_comm = MPI_COMM_NULL;

struct globals
{
    int glob_rank;
    int glob_size;
    int niters;
    int nflight;
    int nservers;
    int bsize;
    int nclients;
    bool hostname_resolve;
    bool sequential_ios;
    char hostname[HOST_MAX_SIZE];
    char *hosts;
    enum output_mode output_mode;
};
#define GLOBALS_INIT { -1, -1, NITERS, NFLIGHT, 0, -1, 0, false, false, {0}, NULL, OUTPUT_MPI}
static struct globals my = GLOBALS_INIT;

struct results
{
    double bw;
    double latency;
    double iops;
    double exec_time;
};

enum test_mode
{
    TEST_MODE_CLIENT_SERVER,
    TEST_MODE_ALL_TO_ALL,
};

enum peer_role
{
    PEER_RECV, /* current rank expects to receive data from peer */
    PEER_SEND, /* current rank expects to send data to peer */
};

struct peer_entry
{
    int rank;
    enum peer_role role;
};

struct test_config
{
    enum test_mode test_mode;
    int curr_iter;
    int data_size;
    int niters;
    int nflight;
    enum direction direction;
    void *s_buffer;
    void *r_buffer;
    /* All to all specific data */
    struct peer_entry *peers_list; /* List of peers to communicate with */
    /* Client server specific data */
    void *rdma_buffer;
    MPI_Win rdma_win;
};

/* State machine for client/server mode */
enum rstate
{
    STATE_REQ_NULL = 0, /* Null request */
    STATE_REQ_POSTED,   /* Request posted */
    STATE_RDMA_POSTED,  /* RDMA posted */
    STATE_RESP_POSTED,  /* Response posted */
};

const char * rstate_str[] =
{
    [STATE_REQ_NULL]    = "req null",
    [STATE_REQ_POSTED]  = "req posted",
    [STATE_RDMA_POSTED] = "rdma posted",
    [STATE_RESP_POSTED] = "resp posted",
};

#define CONFIG_PRINT_HEADER                                                    \
    "Dir size(B)"

#define CONFIG_PRINT_FMT                                                       \
    "%3s %7d"

#define CONFIG_PRINT_ARGS(config)                                              \
    direction_str[(config)->direction],                                        \
    (config)->data_size

#define RESULTS_PRINT_HEADER                                                   \
    "   time(s)   bw(MB/s) lat(us)       iops"

#define RESULTS_PRINT_FMT                                                      \
    "%10.1f %10.0f %7.2f %10.0f"

#define RESULTS_PRINT_ARGS(res)                                                \
    (res)->exec_time,                                                          \
    (res)->bw,                                                                 \
    (res)->latency,                                                            \
    (res)->iops

static const char *get_hostname(int rank, bool is_client)
{
    static const char * rank_any = "all";

    /* Treat MPI_RANK_ANY as a special value corresponding to all ranks */
    if (rank == MPI_RANK_ANY)
        return rank_any;

    return (my.hosts +
            (rank + (is_client ? my.nservers : 0)) * HOST_MAX_SIZE);
}

bool is_server(void)
{
    return clients_comm == MPI_COMM_NULL;
}

static void generate_results(const struct test_config *config,
                             int npeers,
                             double exec_time,
                             struct results *res)
{
    const int niters    = config->niters;
    const int data_size = config->data_size;

    res->bw = (double) data_size * npeers * niters /(1024 * 1024 * exec_time);
    res->latency = (double) exec_time / (npeers * niters * 10e-6);
    res->iops = (double) npeers * niters / exec_time;
    res->exec_time = exec_time;
}

static void print_header_verbose(const struct test_config *config)
{
    int client_rank;

    /* It's a warmup, nothing to print */
    if (config->curr_iter < 0)
        return;

    MPI_CHECK(MPI_Comm_rank(clients_comm, &client_rank));
    if (client_rank != 0)
        return;

   fprintf(stdout,"#             src             dest "
                  CONFIG_PRINT_HEADER" "
                  RESULTS_PRINT_HEADER"\n");
   fflush(stdout);
}

static void print_results_verbose(const struct test_config *config,
                                  const int dst,
                                  const struct results *input_res)
{
    /* It's a warmup */
    if (config->curr_iter < 0)
        return;

    int client_rank;
    MPI_CHECK(MPI_Comm_rank(clients_comm, &client_rank));

    if (!my.hostname_resolve)
        fprintf(stdout," %16d %16d "CONFIG_PRINT_FMT" "RESULTS_PRINT_FMT"\n",
                        client_rank,
                        dst,
                        CONFIG_PRINT_ARGS(config),
                        RESULTS_PRINT_ARGS(input_res));
    else
        fprintf(stdout," %16s %16s "CONFIG_PRINT_FMT" "RESULTS_PRINT_FMT"\n",
                        get_hostname(client_rank, true),
                        get_hostname(dst, false),
                        CONFIG_PRINT_ARGS(config),
                        RESULTS_PRINT_ARGS(input_res));
    fflush(stdout);
}

static void *mallocz(const size_t size)
{
    void *p = NULL;

    int rc = posix_memalign(&p, 4096, size);
    if (rc == 0 && p)
        memset(p, 0, size);
    return p;
}

static void* allocate_buffer(const size_t size)
{
    void *ptr =  mallocz(size);
    assert(ptr);
    return ptr;
}

static void destroy_buffer(void *ptr)
{
    free(ptr);
}

static double client(const struct test_config *config)
{
    double start, end;
    int npeers = my.nservers;
    double exec_time;

    const int nflight   = config->nflight;
    const int niters    = config->niters;
    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    MPI_Request reqs[nflight * 2];
    int k = 0;

    if (my.output_mode == OUTPUT_VERBOSE)
        print_header_verbose(config);

    MPI_CHECK(MPI_Barrier(clients_comm));

    start = MPI_Wtime();

    for (int j = 0; j < niters; j++)
    {
        for (int peer = 0; peer < npeers; peer++)
        {
            MPI_CHECK(MPI_Irecv(&r_buffer[k], 1,
                                MPI_CHAR, peer,
                                0,
                                MPI_COMM_WORLD,
                                &reqs[k * 2]));

            /* Send the RDMA request. The displacement to use is encoded
             * into the MPI TAG */
            MPI_CHECK(MPI_Isend(&s_buffer[k], 1,
                                MPI_CHAR, peer,
                                k, /* MPI TAG = displacement */
                                MPI_COMM_WORLD,
                                &reqs[k * 2 + 1]));

            /* Nflight reached, now wait for all reqs to complete */
            if (++k >= nflight)
            {
                /* Wait for all Isend/Irecv to complete */
                MPI_CHECK(MPI_Waitall(k * 2, reqs, MPI_STATUSES_IGNORE));
                k = 0;
            }
        }
    }

    MPI_CHECK(MPI_Waitall(k * 2, reqs, MPI_STATUSES_IGNORE));

    end = MPI_Wtime();
    exec_time = (end - start);

    if (my.output_mode == OUTPUT_VERBOSE)
    {
        struct results res;
        generate_results(config, npeers, exec_time, &res);
        print_results_verbose(config, -1, &res);
    }

    return exec_time;
}

static double server(const struct test_config *config)
{
    double start, end;

    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    int (*mpi_rma_func)(const void *origin_addr, int origin_count,
                        MPI_Datatype origin_datatype, int target_rank,
                        MPI_Aint target_disp, int target_count,
                        MPI_Datatype target_datatype, MPI_Win win,
                        MPI_Request *req);

    /* Select which direction to use */
    if (config->direction == DIR_PUT)
        mpi_rma_func = MPI_Rput;
    else if (config->direction == DIR_GET)
        mpi_rma_func = MPI_Rput;
    else
        assert(0);

    size_t nb_completed = 0;
    const int nflight = MIN(NUM_RDMA_BUFFERS, (my.nclients * config->niters));
    MPI_Request reqs[nflight];
    enum rstate rstates[nflight];
    int dst_ranks[nflight];

    /* Post all receive buffers to retrieve client's requests */
    for (int i = 0; i < nflight; i++)
    {
        MPI_CHECK(MPI_Irecv(&r_buffer[i],
                            1, MPI_CHAR,
                            MPI_ANY_SOURCE,
                            MPI_ANY_TAG,
                            MPI_COMM_WORLD,
                            &reqs[i]));
        rstates[i] = STATE_REQ_POSTED;
        dst_ranks[i] = MPI_RANK_ANY;
    }

    MPI_Win_lock_all(0, config->rdma_win);

    start = MPI_Wtime();

retry:
    /* Make sure we progress all the requests in a fair way */
    for (int i = 0; i < nflight; i++)
    {
        int flag;
        MPI_Status status;

        if (reqs[i] == MPI_REQUEST_NULL)
        {
            assert(rstates[i] == STATE_REQ_NULL);
            continue;
        }

        MPI_Test(&reqs[i], &flag, &status);
        if (!flag)
            continue;

        switch (rstates[i])
        {
        case STATE_REQ_POSTED:
            dst_ranks[i] = status.MPI_SOURCE;
            assert(dst_ranks[i] >= 0 &&
                   dst_ranks[i] < my.glob_size);

            /* Start RMA operation */
            void *base_ptr = (char *) config->rdma_buffer +
                                      i * config->data_size;
            MPI_CHECK(mpi_rma_func(base_ptr,
                                   config->data_size,
                                   MPI_CHAR,
                                   status.MPI_SOURCE /* Rank of receiver */,
                                   status.MPI_TAG /* Disp at receiver side */,
                                   config->data_size,
                                   MPI_CHAR,
                                   config->rdma_win,
                                   &reqs[i]));
            rstates[i] = STATE_RDMA_POSTED;
            break;

        case STATE_RDMA_POSTED:
            assert(dst_ranks[i] >= 0 &&
                   dst_ranks[i] < my.glob_size);
            /* RMA completed, now send the response */
            MPI_CHECK(MPI_Isend(&s_buffer[i],
                                1, MPI_CHAR,
                                dst_ranks[i],
                                0, MPI_COMM_WORLD,
                                &reqs[i]));
            rstates[i] = STATE_RESP_POSTED;
            break;

        case STATE_RESP_POSTED:
            /* Response sent, now repost the recv buffer */
            nb_completed++;
            dst_ranks[i] = MPI_RANK_ANY;

            if ((nb_completed + nflight) <=
                my.nclients * config->niters)
            {
                MPI_CHECK(MPI_Irecv(&r_buffer[i],
                                    1, MPI_CHAR,
                                    MPI_ANY_SOURCE,
                                    MPI_ANY_TAG,
                                    MPI_COMM_WORLD,
                                    &reqs[i]));
                rstates[i] = STATE_REQ_POSTED;
            }
            else
            {
                reqs[i] = MPI_REQUEST_NULL;
                rstates[i] = STATE_REQ_NULL;

                /* End of test reached, now leaving */
                if (my.nclients * config->niters == nb_completed)
                    goto exit;
            }
            break;

        case STATE_REQ_NULL:
        /* fallthrough */
        default:
            fprintf(stderr, "Wrong state %d %d\n",
                    rstates[i], i);
            assert(0);
        }
    }

    /* Retry */
//    MPI_CHECK(MPI_Win_flush_all(config->rdma_win));
    goto retry;

exit:
    MPI_Win_unlock_all(config->rdma_win);
    end = MPI_Wtime();

    return end - start;
}

static void init_test(enum test_mode test_mode,
                      const int curr_iter,
                      const int niters,
                      const int nflight,
                      const int data_size,
                      const enum direction direction,
                      struct test_config *config)
{
    config->test_mode = test_mode;
    config->nflight   = nflight;
    config->niters    = niters;
    config->data_size = data_size;
    config->direction = direction;
    config->curr_iter = curr_iter;
}

static double run_test_client_server(struct test_config *config,
                                     struct results *res)
{
    /* Few barriers to sync everybody */
    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
    MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

    if (is_server())
    {
        /* each server waits for niters messages from each client */
        return server(config);
    }
    else /* use all ranks >= nservers as the "clients" */
        return client(config);
}

static void reduce_results_sum(struct results *invec, struct results *inoutvec,
                               int *len, MPI_Datatype *dtype)
{
    for (int i = 0; i < *len; i++)
    {
        inoutvec[i].bw        += invec[i].bw;
        inoutvec[i].latency   += invec[i].latency;
        inoutvec[i].iops      += invec[i].iops;
        inoutvec[i].exec_time += invec[i].exec_time;
    }
}

static void reduce_results_min(struct results *invec, struct results *inoutvec,
                               int *len, MPI_Datatype *dtype)
{
    for (int i = 0; i < *len; i++)
    {
        inoutvec[i].bw        = MIN(invec[i].bw,        inoutvec[i].bw);
        inoutvec[i].latency   = MIN(invec[i].latency,   inoutvec[i].latency);
        inoutvec[i].iops      = MIN(invec[i].iops,      inoutvec[i].iops);
        inoutvec[i].exec_time = MIN(invec[i].exec_time, inoutvec[i].exec_time);
    }
}

static void reduce_results_max(struct results *invec, struct results *inoutvec,
                               int *len, MPI_Datatype *dtype)
{
    for (int i = 0; i < *len; i++)
    {
        inoutvec[i].bw        = MAX(invec[i].bw,        inoutvec[i].bw);
        inoutvec[i].latency   = MAX(invec[i].latency,   inoutvec[i].latency);
        inoutvec[i].iops      = MAX(invec[i].iops,      inoutvec[i].iops);
        inoutvec[i].exec_time = MAX(invec[i].exec_time, inoutvec[i].exec_time);
    }
}

static void init_mpi(int argc, char *argv[], const int nservers)
{
    MPI_Datatype dtype[4] = {MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE};
    MPI_Aint disp[4] = {offsetof(struct results, bw),
                        offsetof(struct results, latency),
                        offsetof(struct results, iops),
                        offsetof(struct results, exec_time)};
    int blocklen[4] = {1, 1, 1, 1};

    MPI_CHECK(MPI_Init(&argc, &argv));

    MPI_CHECK(MPI_Type_create_struct(4, blocklen, disp, dtype, &results_dtype));
    MPI_CHECK(MPI_Type_commit(&results_dtype));

    MPI_CHECK(MPI_Op_create((MPI_User_function *) reduce_results_sum, 1,
                            &results_op[OP_SUM]));
    MPI_CHECK(MPI_Op_create((MPI_User_function *) reduce_results_min, 1,
                            &results_op[OP_MIN]));
    MPI_CHECK(MPI_Op_create((MPI_User_function *) reduce_results_max, 1,
                            &results_op[OP_MAX]));

    MPI_CHECK(MPI_Comm_rank(MPI_COMM_WORLD, &my.glob_rank));
    MPI_CHECK(MPI_Comm_size(MPI_COMM_WORLD, &my.glob_size));

    MPI_Group clients_group;
    MPI_Group world_group;

    MPI_CHECK(MPI_Comm_group(MPI_COMM_WORLD, &world_group));
    int clients_range[1][3] = {{nservers, my.glob_size - 1, 1}};
    MPI_CHECK(MPI_Group_range_incl(world_group, 1, clients_range,
                                   &clients_group));

    MPI_CHECK(MPI_Comm_create_group(MPI_COMM_WORLD, clients_group, 0,
                                    &clients_comm));
    MPI_CHECK(MPI_Group_free(&clients_group));
    MPI_CHECK(MPI_Group_free(&world_group));
}

static void destroy_mpi(void)
{
    if (clients_comm != MPI_COMM_NULL)
    {
        MPI_CHECK(MPI_Comm_free(&clients_comm));
        clients_comm = MPI_COMM_NULL;
    }

    MPI_CHECK(MPI_Op_free(&results_op[OP_SUM]));
    MPI_CHECK(MPI_Op_free(&results_op[OP_MIN]));
    MPI_CHECK(MPI_Op_free(&results_op[OP_MAX]));
    MPI_CHECK(MPI_Type_free(&results_dtype));
    MPI_CHECK(MPI_Finalize());
}

static void print_results_reduced(const struct test_config *config,
                                  const struct results *input_res)
{
    struct results output_res[_OP_LAST];
    int split_comm_rank;

    /* Not part of client communicator: return */
    if (clients_comm == MPI_COMM_NULL)
        return;

    MPI_CHECK(MPI_Comm_rank(clients_comm, &split_comm_rank));
    for (int op = 0; op < _OP_LAST; ++op)
        MPI_CHECK(MPI_Reduce(input_res, &output_res[op], 1, results_dtype,
                             results_op[op], MPI_ROOT_RANK, clients_comm));

    if (split_comm_rank == MPI_ROOT_RANK)
    {
        if (config->curr_iter == 0)
        {
            fprintf(stdout, "                                  SUM                                     MIN                                      MAX                    \n");
            fprintf(stdout, CONFIG_PRINT_HEADER" "
                            RESULTS_PRINT_HEADER" "
                            RESULTS_PRINT_HEADER" "
                            RESULTS_PRINT_HEADER"\n");
        }

        fprintf(stdout, CONFIG_PRINT_FMT" "
                        RESULTS_PRINT_FMT" "
                        RESULTS_PRINT_FMT" "
                        RESULTS_PRINT_FMT"\n",
                        CONFIG_PRINT_ARGS(config),
                        RESULTS_PRINT_ARGS(&output_res[OP_SUM]),
                        RESULTS_PRINT_ARGS(&output_res[OP_MIN]),
                        RESULTS_PRINT_ARGS(&output_res[OP_MAX]));
    }
}

static void help_usage(char *prog, FILE *stream)
{
    fprintf(stream, "IME Network Analysis Tool.\n\n");
    fprintf(stream, "Usage: %s [OPTIONS]\n", basename(prog));
    fprintf(stream, "\t-s, --nservers\tNumber of servers.\n");
    fprintf(stream, "\t-i, --niters\tNumber of iterations.\n");
    fprintf(stream, "\t-f, --nflight\tNumber of max inflight messages per client.\n");
    fprintf(stream, "\t-b, --bsize\tSize of network buffers to test (in bytes).\n");
    fprintf(stream, "\t-n, --hostnames\tEnable hostname resulution with verbose mode.\n");
    fprintf(stream, "\t-v, --verbose\tEnable verbose mode.\n");
    fprintf(stream, "\t-h, --help\tHelp page.\n");
}

static void parse_args(int argc, char *argv[])
{
    static const struct option long_options[] = {
        { "nservers",   required_argument, 0, 's' },
        { "niters",     required_argument, 0, 'i' },
        { "nflight",    required_argument, 0, 'f' },
        { "help",       no_argument,       0, 'h' },
        { "bsize",      required_argument, 0, 'b' },
        { "hostnames",  no_argument,       0, 'n' },
        { "sequential", no_argument,       0, 't' },
        { "verbose",    no_argument,       0, 'v' },
        { 0,            0,                 0, 0 }
    };

    while (1) {
        int c = getopt_long(argc, argv, "s:i:h,f:,n,t",
                        long_options, NULL);
        if (c == -1)
            break;

        switch (c)
        {
            case 's':
                my.nservers = atoi(optarg);
                break;
            case 'i':
                my.niters = atoi(optarg);
                break;
            case 'f':
                my.nflight = atoi(optarg);
                break;
            case 'b':
                my.bsize = atoi(optarg);
                break;
            case 'n':
                my.hostname_resolve = true;
                break;
            case 't':
                my.sequential_ios = true;
                break;
            case 'h':
                help_usage(argv[0], stdout);
                exit(EXIT_SUCCESS);
                break;
            case 'v':
                my.output_mode = OUTPUT_VERBOSE;
                break;
            default:
                fprintf(stderr, "Invalid argument: %s\n", optarg);
                help_usage(argv[0], stderr);
                exit(EXIT_FAILURE);
        }
    }
}

static void test_client_server(int start_size, int end_size,
                               enum direction direction)
{
    int curr_size;
    int curr_iter = 0;
    struct test_config test_config;

    int nflight = is_server() ? NUM_RDMA_BUFFERS : my.nflight;
    const int win_size = end_size * nflight;

    /* Allocate buffers */
    test_config.s_buffer = allocate_buffer(nflight);
    test_config.r_buffer = allocate_buffer(nflight);

    MPI_CHECK(MPI_Win_allocate(win_size,
                               end_size, /* disp unit */
                               MPI_INFO_NULL,
                               MPI_COMM_WORLD,
                               &test_config.rdma_buffer,
                               &test_config.rdma_win));

    /* Warmup test */
    init_test(TEST_MODE_CLIENT_SERVER,
              -1, NUM_RDMA_BUFFERS, nflight, 1, direction, &test_config);
    run_test_client_server(&test_config, NULL);

    for (curr_size = start_size; curr_size <= end_size; curr_size *= 2)
    {
        struct results res;
        double exec_time = 0;

        init_test(TEST_MODE_CLIENT_SERVER,
                  curr_iter++,
                  my.niters, nflight, curr_size,
                  direction,
                  &test_config);

        exec_time = run_test_client_server(&test_config, &res);

        if (my.output_mode == OUTPUT_MPI)
        {
            int npeers = my.glob_rank < my.nservers ? my.nclients : my.nservers;
            generate_results(&test_config, npeers, exec_time, &res);
            print_results_reduced(&test_config, &res);
        }
    }

    MPI_CHECK(MPI_Win_free(&test_config.rdma_win));

    destroy_buffer(test_config.s_buffer);
    destroy_buffer(test_config.r_buffer);
}

static int alltoall_get_abs_rank(int rel_rank, int step, int size)
{
    return ((rel_rank - 1 - step + (size - 1)) % (size - 1)) + 1;
}

/* This function is used the generate a list of remote peers the current rank
 * will be communicating with.
 * This algorithm has been inspired from the 'linktest' tool from FZ Julich:
 * http://www.fz-juelich.de/ias/jsc/EN/Expertise/Support/Software/LinkTest/_node.html
 * It has been modified to be simpler to read and adapted to the need of the
 * network sanitizer.
 */
static struct peer_entry *
alltoall_get_peers(int rank, int size)
{
    int maxp = (int) (size / 2);
    int from, to;

    struct peer_entry *peers_list = malloc(sizeof(struct peer_entry) * size);
    if (peers_list == NULL)
        return NULL;

    for (int step = 0; step < size; step++) {

        for (int p = 0; p < (maxp - 1); p++) {
            from = alltoall_get_abs_rank(maxp - p, step, size);
            to = alltoall_get_abs_rank(maxp + 1 + p, step, size);

            if (from == rank)
            {
                peers_list[step].rank = to;
                peers_list[step].role = PEER_RECV;
            }
            if (to == rank)
            {
                peers_list[step].rank = from;
                peers_list[step].role = PEER_SEND;
            }
        }

        from = 0;
        to = alltoall_get_abs_rank(1, step, size);

        if (from == rank)
        {
            peers_list[step].rank = to;
            peers_list[step].role = PEER_RECV;
        }
        if (to == rank)
        {
            peers_list[step].rank = from;
            peers_list[step].role = PEER_SEND;
        }
    }
    return peers_list;
}

#if 0
static void alltoall_print_peers(struct peer_entry *peers_list, int size)
{
    for (int i = 0; i < size; i++)
        fprintf(stderr, "%d ", peers_list[i].rank);
    fprintf(stderr, "\n");
}
#endif

static double run_test_alltoall_pair(
        int peer_rank, enum peer_role peer_role,
        const struct test_config *config)
{
    double start, end;
    int k = 0;

    const int niters    = config->niters;
    const int data_size = config->data_size;
    const int nflight   = config->nflight;
    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    MPI_Request reqs[nflight + 1]; /* +1 for response message */

    end = start = MPI_Wtime(); /* Make sure 'end' gets always initialized */

    for (int j = 0; j < niters; j++)
    {
        if (peer_role == PEER_RECV)
            MPI_CHECK(MPI_Irecv(&r_buffer[data_size * k], data_size,
                                MPI_CHAR, peer_rank, 0, MPI_COMM_WORLD,
                                &reqs[k]));
        else
        {
            assert(peer_role == PEER_SEND);
            MPI_CHECK(MPI_Isend(&s_buffer[data_size * k], data_size,
                                MPI_CHAR, peer_rank, 0, MPI_COMM_WORLD,
                                &reqs[k]));
        }

        /* Nflight reached or last iteration, now send the response and wait
         * for all reqs (including the response) to complete */
        if (++k >= nflight || (j == niters - 1))
        {
            char response = 'x';
            const int resp_tag = 42;

            /* Send / Recv response */
            if (peer_role == PEER_RECV)
            {
                response = 'o';
                MPI_CHECK(MPI_Isend(&response, 1,
                                    MPI_CHAR, peer_rank,
                                    resp_tag, MPI_COMM_WORLD,
                                    &reqs[k]));
            }
            else
            {
                assert(peer_role == PEER_SEND);
                MPI_CHECK(MPI_Irecv(&response, 1,
                                    MPI_CHAR, peer_rank,
                                    resp_tag, MPI_COMM_WORLD,
                                    &reqs[k]));
            }

            MPI_CHECK(MPI_Waitall(k + 1, &reqs[0], MPI_STATUSES_IGNORE));
            assert(response == 'o');
            end = MPI_Wtime();
            k = 0;
        }
    }

    return (end - start);
}

static double run_test_alltoall(const struct test_config *config)
{
    double total_exec_time = 0, step_exec_time;
    int npeers = my.nclients;

    if (my.output_mode == OUTPUT_VERBOSE)
        print_header_verbose(config);

    for (int step = 0; step < npeers - 1; step++)
    {
        int peer_rank = config->peers_list[step].rank;
        enum peer_role peer_role = config->peers_list[step].role;

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        if (my.sequential_ios)
        {
            for (int i = 0; i < npeers; i++)
            {
                MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

                if (i == peer_rank)
                    step_exec_time = run_test_alltoall_pair(peer_rank,
                                                            PEER_SEND, config);

                if (i == my.glob_rank)
                    step_exec_time = run_test_alltoall_pair(peer_rank,
                                                            PEER_RECV, config);
            }
        }
        else
        {
            step_exec_time = run_test_alltoall_pair(peer_rank,
                                                    peer_role, config);
        }

        total_exec_time += step_exec_time;

        if (my.output_mode == OUTPUT_VERBOSE)
        {
            struct results res;
            generate_results(config, 1, step_exec_time, &res);
            print_results_verbose(config, peer_rank, &res);
        }
    }

    return total_exec_time;
}

static void test_alltoall(int start_size, int end_size)
{
    int curr_size;
    int curr_iter = 0;
    struct test_config test_config;
    int npeers = my.nclients - 1;

    /* Allocate buffers */
    test_config.s_buffer = allocate_buffer(end_size * my.nflight);
    test_config.r_buffer = allocate_buffer(end_size * my.nflight);
    test_config.peers_list = alltoall_get_peers(my.glob_rank, my.nclients);
    assert(test_config.peers_list);
#if 0
    alltoall_print_peers(test_config.peers_list, my.nclients);
#endif

    /* Warmup test */
    init_test(TEST_MODE_ALL_TO_ALL,
              -1, 2, my.nflight, end_size, DIR_NONE, &test_config);
    run_test_alltoall(&test_config);

    for (curr_size = start_size; curr_size <= end_size; curr_size *= 2)
    {
        struct results res;
        double exec_time;

        init_test(TEST_MODE_ALL_TO_ALL,
                  curr_iter++,
                  my.niters, my.nflight, curr_size,
                  DIR_NONE,
                  &test_config);

        exec_time = run_test_alltoall(&test_config);

        if (my.output_mode == OUTPUT_MPI)
        {
            generate_results(&test_config,
                             npeers,
                             exec_time, &res);
            print_results_reduced(&test_config, &res);
        }
    }

    destroy_buffer(test_config.s_buffer);
    destroy_buffer(test_config.r_buffer);
    free(test_config.peers_list);
}

void exchange_hostnames(void)
{
    my.hosts = mallocz(my.glob_size * HOST_MAX_SIZE);
    assert(my.hosts);

    gethostname(my.hostname, HOST_MAX_SIZE);
    my.hostname[HOST_MAX_SIZE - 1] = '\0';

    int local_rank;

    if (is_server())
        local_rank = my.glob_rank;
    else
        MPI_CHECK(MPI_Comm_rank(clients_comm, &local_rank));

    /* Append the global rank  */
    snprintf(my.hostname + strnlen(my.hostname, HOST_MAX_SIZE),
             HOST_MAX_SIZE,
             "-%d",
             local_rank);

    MPI_CHECK(MPI_Allgather(my.hostname,
                            HOST_MAX_SIZE, MPI_BYTE,
                            my.hosts, HOST_MAX_SIZE, MPI_BYTE,
                            MPI_COMM_WORLD));
    memcpy(my.hosts + (my.glob_rank * HOST_MAX_SIZE), my.hostname,
           HOST_MAX_SIZE);
}

int main(int argc, char *argv[])
{
    int start_size = 1;
    int end_size = (1 << 22);

    parse_args(argc, argv);

    init_mpi(argc, argv, my.nservers);
    my.nclients = (my.glob_size - my.nservers);

    /* Exchange hostnames if requested */
    if (my.hostname_resolve)
        exchange_hostnames();

    if (my.bsize >= 0)
        start_size = end_size = my.bsize;

    if (my.glob_rank == 0)
        fprintf(stdout, "#nservers=%i nclients=%d niters=%d nflight=%d "
                        "sequential=%d ssize=%d, esize=%d\n",
                        my.nservers, my.nclients, my.niters, my.nflight,
                        my.sequential_ios, start_size, end_size);

    if (my.nservers <= 0)
    {
        if (my.nclients % 2)
        {
            fprintf(stderr,
                    "Alltoall mode requires an even number of clients\n");
            return EXIT_FAILURE;
        }
        test_alltoall(start_size, end_size);
    }
    else
    {
        test_client_server(start_size, end_size, DIR_PUT);
        test_client_server(start_size, end_size, DIR_GET);
    }

    destroy_mpi();

    if (my.hosts)
    {
        free(my.hosts);
        my.hosts = NULL;
    }

    return EXIT_SUCCESS;
}
