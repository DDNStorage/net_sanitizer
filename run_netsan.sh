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
#               --clients "imeclient[1-4]" --servers-hcas "mlx5_0:mlx5_1"
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
SERVERS_HCAS_NUM="1" # 1 HCA per default
SERVERS_HCAS=""
CLIENTS=""
NUM_CLIENTS=""

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
    echo "    --servers-hcas <HCA1[:HCA3]>  List of HCAs to use on servers."
    echo "    --clients <list>              List of clients."
    echo "    --clients-file <file>         File containing the client names (not implemented)."
    echo "    --niters <num>                Number of iterations."
    echo "    --nflight <num>               Number of infligh messages."
    echo "    --bsize <num>                 Buffer size (in byptes)."
    echo "    --verbose                     Enable verbose mode."
    echo "    --help                        Print this help message."
}

OPTS="$(getopt -o h,v -l servers:,servers-file:,servers-hcas:,niters:,\
clients:,clients-file:,bsize:,help,nflight:,verbose -n "$0" -- "$@")"
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
        --servers-hcas)
            SERVERS_HCAS="$2"
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

if [ -n "$SERVERS_HCAS" ]; then
    SERVERS_HCAS_NUM=$(echo $SERVERS_HCAS | awk -F: '{print NF}')
fi

SERVERS_LIST=$(node_set "$SERVERS_LIST" ":$SERVERS_HCAS_NUM")
NUM_SERVERS=$(echo $SERVERS_LIST | awk -F, '{print NF}')
MPI_NUM_SERVERS=$(($NUM_SERVERS * $SERVERS_HCAS_NUM))

CLIENTS_LIST=$(node_set "$CLIENTS_LIST" "")
NUM_CLIENTS=$(echo $CLIENTS_LIST | awk -F, '{print NF}')

COMMON_OPTS="-hosts $SERVERS_LIST,$CLIENTS_LIST"
SERVERS_OPTS="-genv MV2_NUM_HCAS $SERVERS_HCAS_NUM -np $MPI_NUM_SERVERS"
if [ -n "$SERVERS_HCAS" ]; then
    SERVERS_OPTS+=" -genv MV2_IBA_HCA $SERVERS_HCAS"
fi
CLIENTS_OPTS="-envnone -genv MV2_NUM_HCAS 1 -np $NUM_CLIENTS"
NETSAN_OPTS+=" --nservers $MPI_NUM_SERVERS"

echo "Servers($NUM_SERVERS): $SERVERS_LIST"
echo "Clients($NUM_CLIENTS): $CLIENTS_LIST"
echo "Netsan args: $NETSAN_OPTS"
echo ""

$MPIEXEC $COMMON_OPTS \
$SERVERS_OPTS $NETSAN $NETSAN_OPTS : \
$CLIENTS_OPTS $NETSAN $NETSAN_OPTS
