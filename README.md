# Network Sanitizer

## Description

The network Sanitizer is a powerful tool used to find common network network
issues in a HPC cluster.

The tool allows 2 different modes:

- *Client/server*: in this mode, the clients directly communicate with the
  servers. In other terms, there is no server-to-server nor client-to-client
  communication. This mode supports

- *All-to-all*: in this mode, all the nodes take part of a all-to-all
  communication pattern. To avoid a heavy saturation of the network, each
  benchmark iteration is divided in rounds where the compute nodes communicate
  in pairs. After all the rounds have been executed, all compute nodes have
  communicated all-together.

## Help message

```
run_netsan.sh
    --servers <list>              List of servers.
    --servers-file <file>         File containing the server names (not implemented).
    --servers-hcas <HCA1[:HCA3]>  List of HCAs to use on servers.
    --clients <list>              List of clients.
    --clients-file <file>         File containing the client names (not implemented).
    --niters <num>                Number of iterations.
    --nflight <num>               Number of inflight messages.
    --bsize <num>                 Buffer size (in byptes).
    --verbose                     Enable verbose mode.
    --help                        Print this help message.
```

The list of clients/servers can be specified using the following syntax:
- `client[1-5]` expands to `client1,client2,client3,client4,client5`
- `client[1,10]` expands to `client1,client10`
- `client[1-3,10]` expands to `client1,client2,client3,client10`

## Examples

- Run a client/server benchmark with Multirail enabled on server side (name
of HCAs can be determined using `ibstat` command line tool):

```
run_netsan.sh --servers "server[2-3]" --clients "client[1-48]" --servers-hcas "mlx5_0:mlx5_1"

Servers(2): server2:2,server3:2
Clients(8): client1,client2,client3,client4,client5,client6,client7,client8

   size(B)  time(s)  bw(MB/s) lat(us)       iops
      1        0.1          2    2.74    2335828
      2        0.1          5    2.50    2555396
      4        0.1         10    2.50    2558952
      8        0.1         19    2.51    2550935
     16        0.1         38    2.54    2517640
     32        0.1         77    2.54    2518297
     64        0.1        139    2.82    2271600
    128        0.1        274    2.85    2246773
    256        0.1        533    2.93    2183926
    512        0.1       1029    3.04    2108186
   1024        0.1       1878    3.33    1922853
   2048        0.1       3423    3.65    1752542
   4096        0.2       5642    4.43    1444395
   8192        0.6       3506   14.26     448810
  16384        0.7       5786   17.28     370324
  32768        0.8      10498   19.07     335937
  65536        1.0      15662   25.58     250595
 131072        1.7      19547   40.99     156380
 262144        3.0      21723   73.72      86893
 524288        5.9      22219  144.05      44438
1048576       11.3      23103  277.04      23103
2097152       22.8      23022  556.00      11511
4194304       45.5      23059 1110.18       5765
```

- Run a all-to-all benchmark:

```
run_netsan.sh --clients "client[1-8]"

Servers(0):
Clients(8): client1,client2,client3,client4,client5,client6,client7,client8
   size(B)  time(s)  bw(MB/s) lat(us)       iops
      1        0.1          4    1.56    4123685
      2        0.1          8    1.54    4157762
      4        0.1         16    1.53    4197300
      8        0.1         31    1.56    4102268
     16        0.1         63    1.56    4109249
     32        0.1        111    1.76    3630911
     64        0.1        215    1.82    3514649
    128        0.1        422    1.85    3458015
    256        0.1        810    1.93    3316894
    512        0.1       1527    2.05    3126876
   1024        0.2       2673    2.34    2737548
   2048        0.2       4755    2.63    2434314
   4096        0.2       7583    3.30    1941261
   8192        1.0       3757   13.33     480956
  16384        1.1       6282   15.95     402017
  32768        1.3      11005   18.18     352170
  65536        1.6      18454   21.69     295272
 131072        2.4      23839   33.62     190709
 262144        4.2      27441   58.49     109763
 524288        7.8      29644  108.37      59288
1048576       15.0      30791  208.72      30791
2097152       29.4      31371  409.74      15686
4194304       58.2      31632  812.62       7908
```

## Known Limitations

- Only runs with MVAPICH for now. This limitation is only due to the script
`run_netsan.sh`, which exports a bunch of environment variables that can
only be interpreted by MVAPICH.
