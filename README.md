# Network Sanitizer

## Description

The Network Sanitizer is a powerful tool used to find common network network
issues in a HPC cluster:

- Network congestion: some clients might complete after the others
  E.g., Fat-tree topology where there are more down-links than up-links.

- Bad cables: might cause errors or slowdowns to the application

- Misconfigured network adapter:
  - bad firmware version
  - wrong link speed

- Network routing issues / Subnet Manager configuration:
  E.g., how to connect some servers to a fat-tree topology to avoid network
  congestion?

- PCIe congestion, causing network performance drops

- IP addressing/routing issues

- IP address conflicts

- Multirail routing rules not correctly applied


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
    --servers-nranks <num>        Number of MPI ranks per server.
    --clients <list>              List of clients.
    --clients-file <file>         File containing the client names (not implemented).
    --clients-nranks <num>        Number of MPI ranks per client.
    --niters <num>                Number of iterations.
    --nflight <num>               Number of infligh messages.
    --bsize <num>                 Buffer size (in byptes).
    --verbose                     Enable verbose mode.
    --hostnames                   Use hostname resolution for MPI ranks.
    --help                        Print this help message.
```

The list of clients/servers can be specified using the following syntax:
- `client[1-5]` expands to `client1,client2,client3,client4,client5`
- `client[1,10]` expands to `client1,client10`
- `client[1-3,10]` expands to `client1,client2,client3,client10`

## Examples

- Run a client/server benchmark with 2 servers (2x IB FDR HCAs per server)
and 4 clients (1x IB FDR HCA per client). Because each server is dual-rail,
we need at least 2 MPI ranks per server to get the full bandwidth.

```
run_netsan.sh --servers "server[2-3]" --clients "client[1-4]" \
--servers-nranks 2

Servers(2): server2:2,server3:2
Clients(8): client1,client2,client3,client4

#nservers=4 nclients=4 niters=1024 nflight=1 ssize=1, esize=4194304
Dir size(B)    time(s)   bw(MB/s) lat(us)    iops
Put       1        1.5          0   36.40      43955
Put       2        1.5          0   35.46      45124
Put       4        1.5          0   35.72      44788
Put       8        1.4          0   35.11      45573
Put      16        1.4          1   34.66      46157
Put      32        1.4          1   34.96      45765
Put      64        1.4          3   35.09      45599
Put     128        1.4          6   34.85      45911
Put     256        1.4         11   35.24      45407
Put     512        1.5         22   35.47      45103
Put    1024        1.4         45   35.10      45582
Put    2048        1.4         89   35.31      45315
Put    4096        1.4        179   34.99      45724
Put    8192        1.4        354   35.27      45361
Put   16384        1.5        685   36.48      43861
Put   32768        1.4       1419   35.24      45403
Put   65536        1.4       2837   35.25      45393
Put  131072        1.4       5688   35.16      45503
Put  262144        1.8       9135   43.79      36539
Put  524288        2.7      11996   66.69      23993
Put 1048576        3.4      19384   82.54      19384
Put 2097152        6.1      21452  149.17      10726
Put 4194304       12.0      21770  293.98       5443

Dir size(B)    time(s)   bw(MB/s) lat(us)    iops
Get       1        1.4          0   35.19      45461
Get       2        1.4          0   35.10      45584
Get       4        1.4          0   35.33      45291
Get       8        1.4          0   35.20      45454
Get      16        1.5          1   35.54      45015
Get      32        1.5          1   37.12      43108
Get      64        1.5          3   36.23      44161
Get     128        1.5          5   36.07      44356
Get     256        1.5         11   36.21      44183
Get     512        1.5         22   35.53      45034
Get    1024        1.4         45   35.04      45659
Get    2048        1.4         89   34.99      45725
Get    4096        1.4        179   34.90      45841
Get    8192        1.5        353   35.45      45134
Get   16384        1.4        710   35.21      45437
Get   32768        1.4       1415   35.34      45273
Get   65536        1.5       2768   36.13      44282
Get  131072        1.5       5609   35.66      44869
Get  262144        1.9       8757   45.68      35030
Get  524288        2.6      12401   64.51      24803
Get 1048576        3.4      19492   82.08      19492
Get 2097152        6.1      21508  148.78      10754
Get 4194304       12.0      21874  292.59       5468
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
