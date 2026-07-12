#!/bin/bash

# === Server list and related config (simplified from your larger script) ===
declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2")
declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0" "client-3-0" "client-4-0" "client-5-0" "client-0-1" "client-1-1" "client-2-1" "client-3-1" "client-4-1" "client-5-1")
USER="atli"
EXP_NAME="sintr"
PROJECT_NAME="pequin"
CLUSTER_NAME="utah"

REMOTE_DIR="/mnt/extra/coredump"

for host in "${arr_servers[@]}"; do
    full_host="${USER}@${host}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us"
    echo "Deleting on $full_host"
    # ssh "$full_host" "sudo sysctl -w kernel.core_pattern=$REMOTE_DIR/core.%p.%t"
    ssh "$full_host" "rm -rf $REMOTE_DIR; mkdir -p $REMOTE_DIR"
done

# for host in "${arr_clients[@]}"; do
#     full_host="${USER}@${host}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us"
#     echo "Deleting on $full_host"
#     ssh "$full_host" "sudo sysctl -w kernel.core_pattern=$REMOTE_DIR/core.%p.%t"
#     ssh "$full_host" "rm -rf $REMOTE_DIR; mkdir -p $REMOTE_DIR"
# done
