#!/bin/bash

declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2")
declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0" "client-3-0" "client-4-0" "client-5-0")

FIRST_TIME_CONNECTION=0

USER="dhl001"
EXP_NAME="sintr-rw"
PROJECT_NAME="pequin"
CLUSTER_NAME="utah"
BENCHMARK_NAME="tpcc"
NUM_SHARDS=1
PG_MODE=0
CLIENTS_PER_SERVER=1
VERIFY_SSH_ONLY=0

while getopts u:e:b:s:f:c:p:n:v: option; do
case "${option}" in
u) USER=${OPTARG};;
e) EXP_NAME=${OPTARG};;
b) BENCHMARK_NAME=${OPTARG};;
s) NUM_SHARDS=${OPTARG};;
f) FIRST_TIME_CONNECTION=${OPTARG};;
c) CLUSTER_NAME=${OPTARG};;
p) PG_MODE=${OPTARG};;
n) CLIENTS_PER_SERVER=${OPTARG};;
v) VERIFY_SSH_ONLY=${OPTARG};;
esac;
done

if [ $PG_MODE = 1 ]; then
  arr_clients=("client-0-0" "client-0-1" "client-0-2" "client-0-3" "client-0-4" "client-0-5")
fi

if [ $NUM_SHARDS = 2 ]; then
   arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2" "ap-northeast-1-0" "ap-northeast-1-1" "ap-northeast-1-2" "us-west-1-0" "us-west-1-1" "us-west-1-2")
   arr_clients=("client-0-0" "client-1-0" "client-2-0" "client-3-0" "client-4-0" "client-5-0" "client-6-0" "client-7-0" "client-8-0" "client-9-0" "client-10-0" "client-11-0")
fi
if [ $NUM_SHARDS = 3 ]; then
   arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2" "ap-northeast-1-0" "ap-northeast-1-1" "ap-northeast-1-2" "us-west-1-0" "us-west-1-1" "us-west-1-2" "eu-central-1-0" "eu-central-1-1" "eu-central-1-2" "ap-southeast-2-0" "ap-southeast-2-1" "ap-southeast-2-2")
   arr_clients=("client-0-0" "client-1-0" "client-2-0" "client-3-0" "client-4-0" "client-5-0" "client-6-0" "client-7-0" "client-8-0" "client-9-0" "client-10-0" "client-11-0" "client-12-0" "client-13-0" "client-14-0" "client-15-0" "client-16-0" "client-17-0")
fi

# If CLIENTS_PER_SERVER > 1, add additional virtual clients
additional_clients=()
for (( i=1; i<$CLIENTS_PER_SERVER; i++ )); do
	for host in "${arr_clients[@]}"
	do
		additional_clients+=("${host:0:-1}$i")
	done
done
arr_clients+=("${additional_clients[@]}")
echo "arr_clients: ${arr_clients[@]}"

if [ $FIRST_TIME_CONNECTION = 1 ]; then
	for host in "${arr_clients[@]}"
	do
	   echo "connecting to host: $host"
	   ssh ${USER}@$host.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us "echo"
	done
	for host in "${arr_servers[@]}"
	do
	   echo "connecting to host: $host"
	   ssh ${USER}@$host.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us "echo"
	done
fi

if [ $VERIFY_SSH_ONLY = 1 ]; then
	echo "SSH connection verification only. Exiting."
	exit 0
fi

echo "Deleting benchmark data for '$BENCHMARK_NAME' from clients and servers..."

# Common delete command
DELETE_CMD="rm -rf /users/${USER}/benchmark_data"

# Delete from clients
parallel "ssh ${USER}@{}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us '${DELETE_CMD}'" ::: ${arr_clients[@]}

# Delete from servers
parallel "ssh ${USER}@{}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us '${DELETE_CMD}'" ::: ${arr_servers[@]}

# Specific cleanup for benchmarks with extra profile data
if [ "$BENCHMARK_NAME" = "seats" ]; then
	parallel "ssh ${USER}@{}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us 'rm -f /users/${USER}/benchmark_data/sql-${BENCHMARK_NAME}-data/*.csv'" ::: ${arr_clients[@]}
fi

if [ "$BENCHMARK_NAME" = "auctionmark" ]; then
	parallel "ssh ${USER}@{}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us 'rm -rf /users/${USER}/benchmark_data/auctionmark_profile'" ::: ${arr_clients[@]}
fi

echo "Cleanup complete."
