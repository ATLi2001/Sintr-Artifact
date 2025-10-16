#!/bin/bash

# === Server list and related config (simplified from your larger script) ===
declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2" "eu-west-1-0" "eu-west-1-1" "eu-west-1-2")
USER="dhl001"
EXP_NAME="sintr-rw"
PROJECT_NAME="pequin"
CLUSTER_NAME="utah"

REMOTE_DIR="/mnt/extra/coredump"

for host in "${arr_servers[@]}"; do
    full_host="${USER}@${host}.${EXP_NAME}.${PROJECT_NAME}-pg0.${CLUSTER_NAME}.cloudlab.us"
    echo "Deleting on $full_host"
    ssh "$full_host" "rm -rf $REMOTE_DIR"
done
