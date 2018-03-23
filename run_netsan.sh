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
OUTPUT_FILE=""

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
    echo "    --clients <list>              List of clients."
    echo "    --clients-file <file>         File containing the client names (not implemented)."
    echo "    --niters <num>                Number of iterations."
    echo "    --nflight <num>               Number of infligh messages."
    echo "    --bsize <num>                 Buffer size (in byptes)."
    echo "    --verbose                     Enable verbose mode."
    echo "    --hostnames                   Use hostname resolution for MPI ranks."
    echo "    --help                        Print this help message."
    echo "    --output <file>               Output data file to use with gnuplot."
}

OPTS="$(getopt -o h,v -l servers:,servers-file:,niters:,\
clients:,clients-file:,bsize:,help,nflight:,verbose,hostnames,output,\
clients-nranks:, -n "$0" -- "$@")"
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
        --clients)
            CLIENTS_LIST="$2"
            shift 2
            ;;
        --clients-file)
            not_implemented
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
        --nflight)
           NETSAN_OPTS+=" --nflight $2"
           shift 2
           ;;
        --bsize)
           NETSAN_OPTS+=" --bsize $2"
           shift 2
           ;;
        --output)
           OUTPUT_FILE="$2"
           shift 2
           ;;
        --clients-nranks)
           CLIENTS_NRANKS="$2"
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

SERVERS_LIST=$(node_set "$SERVERS_LIST")
NUM_SERVERS=$(echo $SERVERS_LIST | awk -F, '{print NF}')

CLIENTS_LIST=$(node_set "$CLIENTS_LIST" ":$CLIENTS_NRANKS")
NUM_CLIENTS=$(($(echo $CLIENTS_LIST | awk -F, '{print NF}') * $CLIENTS_NRANKS))

COMMON_OPTS="-hosts $SERVERS_LIST,$CLIENTS_LIST"
SERVERS_OPTS="-np $NUM_SERVERS"
CLIENTS_OPTS="-np $NUM_CLIENTS -env MV2_NUM_HCAS 1"
NETSAN_OPTS+=" --nservers $NUM_SERVERS"

echo "Servers($NUM_SERVERS): $SERVERS_LIST"
echo "Clients($NUM_CLIENTS): $CLIENTS_LIST"
echo "Netsan args: $NETSAN_OPTS"
echo ""

if [ -n "$OUTPUT_FILE" ]; then
    $MPIEXEC $COMMON_OPTS \
        $SERVERS_OPTS $NETSAN $NETSAN_OPTS : \
        $CLIENTS_OPTS $NETSAN $NETSAN_OPTS > "$OUTPUT_FILE"
else
    $MPIEXEC $COMMON_OPTS \
        $SERVERS_OPTS $NETSAN $NETSAN_OPTS : \
        $CLIENTS_OPTS $NETSAN $NETSAN_OPTS
fi
