#! /bin/bash
###############################################################################
#
# @file
# @copyright
#                               --- WARNING ---
#
#     This work contains trade secrets of DataDirect Networks, Inc.  Any
#     unauthorized use or disclosure of the work, or any part thereof, is
#     strictly prohibited.  Copyright in this work is the property of
#     DataDirect Networks.  All Rights Reserved.  In the event of publication,
#     the following notice shall apply:  Copyright 2012-2016, DataDirect
#     Networks.
#
# @section DESCRIPTION
#
#  Name: run_netsan.sh
#  Module: network_sanitizer
#  Project: Iron Monkey
#
#
#  Description: run_netsan.sh --servers "imesrv[2,3]"
#               --clients "imeclient[1-4]"
#               --niters 2000
#
#  This script is a wrapper for the network analysis tool
#
###############################################################################

readonly SC_DIR=$(cd "$( dirname "${BASH_SOURCE[0]}" )" &&
                  pwd)             # Retrieve path where script is located
readonly SC_NAME=$(basename ${0})    # Retrieve name of the script

MPIEXEC="mpiexec"
NETSAN="./net_sanitizer"

SERVERS_LIST=""
NUM_SERVERS=""
CLIENTS=""
NUM_CLIENTS=""
CLIENTS_NRANKS="1"
SERVERS_NRANKS="1"
SERVERS_ARGS=""
CLIENTS_ARGS=""

# Include
. "$SC_DIR/common.sh"


not_implemented()
{
    echo "Not implemented yet!"
    exit 1;
}

usage()
{
    echo "${SC_NAME}"
    echo "    --servers <list>              List of servers."
    echo "    --servers-file <file>         File containing the server names (not implemented)."
    echo "    --servers-nranks <num>        Number of MPI ranks per server."
    echo "    --servers-args <args>         Extra mpirun args for servers."
    echo "    --clients <list>              List of clients."
    echo "    --clients-file <file>         File containing the client names (not implemented)."
    echo "    --clients-nranks <num>        Number of MPI ranks per client."
    echo "    --clients-args <args>         Extra mpirun args for clients."
    echo "    --niters <num>                Number of iterations."
    echo "    --nflight <num>               Number of infligh messages."
    echo "    --bsize <num>                 Buffer size (in bytes)."
    echo "    --verbose                     Enable verbose mode."
    echo "    --hostnames                   Use hostname resolution for MPI ranks."
    echo "    --sequential                  Use sequential mode, where only one pair of MPI ranks communicate at any time."
    echo "    --help                        Print this help message."
}

OPTS="$(getopt -o h,v -l servers:,servers-file:,niters:,\
clients:,clients-file:,bsize:,help,nflight:,verbose,hostnames,\
clients-nranks:,servers-nranks:,clients-args:,servers-args:,sequential -n "$0" -- "$@")"
eval set -- "$OPTS"

while true
do
    case "$1" in
        -h|--help)
            usage; exit
            ;;
        --servers)
            SERVERS_LIST="$2"
            shift 2
            ;;
        --servers-file)
            not_implemented
            shift 2
            ;;
        --servers-nranks)
            SERVERS_NRANKS="$2"
            shift 2
            ;;
        --servers-args)
            SERVERS_ARGS="$2"
            shift 2
            ;;
        --clients)
            CLIENTS_LIST="$2"
            shift 2
            ;;
        --clients-file)
            not_implemented
            shift 2
            ;;
        --clients-nranks)
           CLIENTS_NRANKS="$2"
           shift 2
           ;;
        --clients-args)
           CLIENTS_ARGS="$2"
           shift 2
           ;;
        --niters)
           NETSAN_OPTS+=" --niters $2"
           shift 2
           ;;
        --hostnames)
           NETSAN_OPTS+=" --hostnames "
           shift
           ;;
        --sequential)
           NETSAN_OPTS+=" --sequential "
           shift
           ;;
        --nflight)
           NETSAN_OPTS+=" --nflight $2"
           shift 2
           ;;
        --bsize)
           NETSAN_OPTS+=" --bsize $2"
           shift 2
           ;;
        -v|--verbose)
           NETSAN_OPTS+=" --verbose"
           shift
           ;;
        --)
          shift
          break
          ;;
    esac
done

SERVERS_LIST=$(node_set "$SERVERS_LIST" ":$SERVERS_NRANKS")
NUM_SERVERS=$(($(echo $SERVERS_LIST | awk -F, '{print NF}')  * $SERVERS_NRANKS))

CLIENTS_LIST=$(node_set "$CLIENTS_LIST" ":$CLIENTS_NRANKS")
NUM_CLIENTS=$(($(echo $CLIENTS_LIST | awk -F, '{print NF}') * $CLIENTS_NRANKS))

COMMON_OPTS="-hosts $SERVERS_LIST,$CLIENTS_LIST"
SERVERS_OPTS="-np $NUM_SERVERS $SERVERS_ARGS"
CLIENTS_OPTS="-np $NUM_CLIENTS $CLIENTS_ARGS"
NETSAN_OPTS+=" --nservers $NUM_SERVERS"

echo "Servers($NUM_SERVERS): $SERVERS_LIST"
echo "Clients($NUM_CLIENTS): $CLIENTS_LIST"
echo "Netsan args: $NETSAN_OPTS"
CMD="$MPIEXEC $COMMON_OPTS \
$SERVERS_OPTS $NETSAN $NETSAN_OPTS : \
$CLIENTS_OPTS $NETSAN $NETSAN_OPTS"
echo "Command line: $CMD"
echo ""
eval $CMD
