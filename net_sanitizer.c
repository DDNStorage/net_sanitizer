#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

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

#define NITERS (1024)
#define NFLIGHT 1
#define MPI_ROOT_RANK 0


enum output_mode
{
    OUTPUT_MPI,
    OUTPUT_VERBOSE,
};

#define MPI_CHECK(func)                                                        \
do {                                                                           \
    int rc;                                                                    \
    rc = func;                                                                 \
    if (rc) MPI_Abort(MPI_COMM_WORLD, rc);                                     \
} while(0)

MPI_Datatype results_dtype;
MPI_Op       results_op_sum;
MPI_Comm     clients_comm = MPI_COMM_NULL;

struct globals
{
    int glob_rank;
    int glob_size;
    char hostname[256];
    int niters;
    int nflight;
    int nservers;
    int bsize;
    int nclients;
    enum output_mode output_mode;
};
#define GLOBALS_INIT { -1, -1, {0}, NITERS, NFLIGHT, 0, -1, 0, OUTPUT_MPI}
static struct globals my = GLOBALS_INIT;

struct results
{
    double bw;
    double latency;
    double iops;
    double exec_time;
};

struct test_config
{
    int curr_iter;
    int data_size;
    int niters;
    int nflight;
    void *s_buffer;
    void *r_buffer;
};

#define RESULTS_PRINT_HEADER                                                   \
    "   size(B)  time(s)  bw(MB/s) lat(us)       iops"

#define RESULTS_PRINT_FMT                                                      \
    "%7d %10.1f %10.0f %7.2f %10.0f\n"

#define RESULTS_PRINT_ARGS(config, res)                                        \
    (config)->data_size,                                                       \
    (res)->exec_time,                                                          \
    (res)->bw,                                                                 \
    (res)->latency,                                                            \
    (res)->iops

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

    /* It's a warmup */
    if (config->curr_iter < 0)
        return;

    MPI_CHECK(MPI_Comm_rank(clients_comm, &client_rank));
    if (client_rank != 0)
        return;

   fprintf(stdout, "#rank peer "RESULTS_PRINT_HEADER"\n");
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

    fprintf(stdout, " %4d %4d "RESULTS_PRINT_FMT,
                    client_rank, dst,
                    RESULTS_PRINT_ARGS(config, input_res));
    fflush(stdout);
}

static void *mallocz(const int size)
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
    double srv_exec_time[npeers];

    const int nflight   = config->nflight;
    const int niters    = config->niters;
    const int data_size = config->data_size;
    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    memset(&srv_exec_time[0], 0, npeers * sizeof(double));

    if (my.output_mode == OUTPUT_VERBOSE)
        print_header_verbose(config);

    MPI_CHECK(MPI_Barrier(clients_comm));

    start = MPI_Wtime();

    for (int j = 0; j < niters; j++)
    {
        for (int peer = 0; peer < npeers; peer++)
        {
            double srv_start = MPI_Wtime();
            int k;
            MPI_Request reqs[nflight * 2];

            /* send/recv a batch of payload messages, 1 to each server */
            for (k = 0; k < nflight; k++)
            {
                MPI_CHECK(MPI_Irecv(&r_buffer[data_size * k], 0, MPI_CHAR, peer,
                                    0, MPI_COMM_WORLD, &reqs[k * 2]));

                MPI_CHECK(MPI_Isend(&s_buffer[data_size * k], data_size,
                                    MPI_CHAR, peer, 0, MPI_COMM_WORLD,
                                    &reqs[k * 2 + 1]));
            }
            /* Wait for all Isend/Irecv to complete */
            MPI_CHECK(MPI_Waitall(k * 2, reqs, MPI_STATUSES_IGNORE));
            double srv_end = MPI_Wtime();

            /* Aggregate the results per server */
            srv_exec_time[peer] += (srv_end - srv_start);
        }
    }

    end = MPI_Wtime();
    exec_time = (end - start);

    if (my.output_mode == OUTPUT_VERBOSE)
    {
        for (int p = 0; p < npeers; p++)
        {
            struct results res;
            generate_results(config, 1, srv_exec_time[p], &res);
            print_results_verbose(config, p, &res);
        }
    }

    return exec_time;
}

static double server(const struct test_config *config)
{
    double start, end;
    int i;

    const int niters    = config->niters;
    const int data_size = config->data_size;
    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    MPI_Request reqs[my.nclients];
    int         cli_count[my.nclients];

    memset(&cli_count, 0, sizeof(int) * my.nclients);

    start = MPI_Wtime();

    /* Pre-post recvs for all clients */
    for (i = 0; i < my.nclients; i++)
    {
        MPI_CHECK(MPI_Irecv(&r_buffer[data_size * i], data_size, MPI_CHAR,
                            i + my.nservers, 0, MPI_COMM_WORLD, &reqs[i]));
    }

    for (i = 0; i < (niters * my.nclients); i++)
    {
        MPI_Status status;
        int loc;

        MPI_CHECK(MPI_Waitany(my.nclients, reqs, &loc, &status));
        MPI_CHECK(MPI_Send(&s_buffer[data_size * loc], 0, MPI_CHAR,
                           status.MPI_SOURCE, 0, MPI_COMM_WORLD));

        /* Last iteration for the dest MPI rank, don't need to repost recv */
        if (++(cli_count[loc]) <= (niters - 1))
        {
            MPI_CHECK(MPI_Irecv(&r_buffer[data_size * loc], data_size,
                                MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD,
                                &reqs[loc]));
        }
    }

    end = MPI_Wtime();
    return end - start;
}

static void init_test(const int curr_iter,
                      const int niters,
                      const int nflight,
                      const int data_size,
                      struct test_config *config)
{
    config->nflight   = nflight;
    config->niters    = niters;
    config->data_size = data_size;
    config->curr_iter = curr_iter;
}

static double run_test_client_server(const struct test_config *config,
                                   struct results *res)
{
    /* use rank 0 -> (nservers - 1) as the "servers" */
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
    int i;

    for (i = 0; i < *len; i++)
    {
        inoutvec[i].bw        += invec[i].bw;
        inoutvec[i].latency   += invec[i].latency;
        inoutvec[i].iops      += invec[i].iops;
        inoutvec[i].exec_time += invec[i].exec_time;
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
                            &results_op_sum));

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
    MPI_CHECK(MPI_Finalize());
}

static void print_results_reduced(const struct test_config *config,
                                  const struct results *input_res)
{
    struct results output_res;
    int split_comm_rank;

    /* Not part of client communicator: return */
    if (clients_comm == MPI_COMM_NULL)
        return;

    MPI_CHECK(MPI_Comm_rank(clients_comm, &split_comm_rank));
    MPI_CHECK(MPI_Reduce(input_res, &output_res, 1, results_dtype,
                         results_op_sum, MPI_ROOT_RANK, clients_comm));

    if (split_comm_rank == MPI_ROOT_RANK)
    {
        if (config->curr_iter == 0)
            fprintf(stderr, RESULTS_PRINT_HEADER"\n");

        fprintf(stderr, RESULTS_PRINT_FMT,
                        RESULTS_PRINT_ARGS(config, &output_res));
    }
}

static void help_usage(char *prog, FILE *stream)
{
    fprintf(stream, "IME Network Analysis Tool.\n\n");
    fprintf(stream, "Usage: %s [OPTIONS]\n", basename(prog));
    fprintf(stream, "\t-s, --nservers\tNumber of servers.\n");
    fprintf(stream, "\t-i, --niters\tNumber of iterations.\n");
}

static void parse_args(int argc, char *argv[])
{
    static const struct option long_options[] = {
        { "nservers",   required_argument, 0, 's' },
        { "niters",     required_argument, 0, 'i' },
        { "nflight",    required_argument, 0, 'f' },
        { "help",       no_argument,       0, 'h' },
        { "bsize",      required_argument, 0, 'b' },
        { "verbose",    no_argument,       0, 'v' },
        { 0,            0,                 0, 0 }
    };

    while (1) {
        int c = getopt_long(argc, argv, "s:i:h,f:",
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

static void test_client_server(int start_size, int end_size)
{
    int curr_size;
    int curr_iter = 0;
    struct test_config test_config;
    int npeers = my.glob_rank < my.nservers ? my.nclients : my.nservers;

    /* Allocate buffers */
    test_config.s_buffer = allocate_buffer(end_size * npeers);
    test_config.r_buffer = allocate_buffer(end_size * npeers);

    /* Warmup test */
    init_test(-1, my.niters, my.nflight, end_size, &test_config);
    run_test_client_server(&test_config, NULL);

    for (curr_size = start_size; curr_size <= end_size; curr_size*=2)
    {
        struct results res;
        double exec_time;

        init_test(curr_iter++,
                  my.niters, my.nflight, curr_size,
                  &test_config);

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
        exec_time = run_test_client_server(&test_config, &res);

        if (my.output_mode == OUTPUT_MPI)
        {
            generate_results(&test_config, npeers, exec_time, &res);
            print_results_reduced(&test_config, &res);
        }
    }

    destroy_buffer(test_config.s_buffer);
    destroy_buffer(test_config.r_buffer);
}

static double run_test_alltoall(const struct test_config *config)
{
    double start, end;
    int distance, j, k;
    double total_exec_time = 0;
    int npeers = my.nclients;

    const int niters    = config->niters;
    const int data_size = config->data_size;
    const int nflight   = config->nflight;
    char *s_buffer      = config->s_buffer;
    char *r_buffer      = config->r_buffer;

    MPI_Request reqs[2 * nflight];

    if (my.output_mode == OUTPUT_VERBOSE)
        print_header_verbose(config);

    for (distance = 1; distance < npeers; distance++)
    {
        int prev_rank = ((my.glob_rank - distance + npeers) % npeers);
        int next_rank = (my.glob_rank + distance) % npeers;

        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));
        MPI_CHECK(MPI_Barrier(MPI_COMM_WORLD));

        start = MPI_Wtime();

        for (j = 0; j < niters;)
        {
            for (k = 0; j < niters && k < nflight; j++, k++)
            {
                MPI_CHECK(MPI_Irecv(&r_buffer[data_size * k], data_size,
                                    MPI_CHAR, prev_rank, 0, MPI_COMM_WORLD,
                                    &reqs[k * 2]));

                MPI_CHECK(MPI_Isend(&s_buffer[data_size * k], data_size,
                                   MPI_CHAR, next_rank, 0, MPI_COMM_WORLD,
                                   &reqs[k * 2 + 1]));
            }
            /* Wait for all Isend/Irecv to complete */
            MPI_CHECK(MPI_Waitall(k * 2, reqs, MPI_STATUSES_IGNORE));
        }

        end = MPI_Wtime();
        total_exec_time += (end - start);

        if (my.output_mode == OUTPUT_VERBOSE)
        {
            struct results res;
            generate_results(config, 1, (end - start), &res);
            print_results_verbose(config, next_rank, &res);
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

    /* Warmup test */
    init_test(-1, my.niters, my.nflight, end_size, &test_config);
    run_test_alltoall(&test_config);

    for (curr_size = start_size; curr_size <= end_size; curr_size *= 2)
    {
        struct results res;
        double exec_time;

        init_test(curr_iter++,
                  my.niters, my.nflight, curr_size,
                  &test_config);

        exec_time = run_test_alltoall(&test_config);

        if (my.output_mode == OUTPUT_MPI)
        {
            generate_results(&test_config, npeers, exec_time, &res);
            print_results_reduced(&test_config, &res);
        }
    }

    destroy_buffer(test_config.s_buffer);
    destroy_buffer(test_config.r_buffer);
}

int main(int argc, char *argv[])
{
    int start_size = 1;
    int end_size = (1 << 22);

    parse_args(argc, argv);

    gethostname(my.hostname, 256);
    init_mpi(argc, argv, my.nservers);
    my.nclients = (my.glob_size - my.nservers);

    if (my.bsize >= 0)
        start_size = end_size = my.bsize;

    if (my.glob_rank == 0)
        fprintf(stderr, "nservers=%i nclients=%d niters=%d nflight=%d "
                        "ssize=%d, esize=%d\n",
                        my.nservers, my.nclients, my.niters, my.nflight,
                        start_size, end_size);

    if (my.nservers <= 0)
        test_alltoall(start_size, end_size);
    else
        test_client_server(start_size, end_size);

    destroy_mpi();

    return EXIT_SUCCESS;
}
