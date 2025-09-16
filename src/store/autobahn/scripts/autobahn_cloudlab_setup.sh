#!/bin/bash

CONFIG_DIR="../config/"
CLIENTS_PER_SERVER=1
CLOUDLAB_USER=atli
CLOUDLAB_EXP_NAME=sintr
CLOUDLAB_PROJECT_NAME=pequin-pg0
CLOUDLAB_CLUSTER=utah
CLOUDLAB_REMOTE_DIR=/users/atli/autobahn_config

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")


if [[ "$#" -eq 6 ]]; then
    echo "Using as: $0 <autobahn-config-directory> <user> <exp_name> <project_name> <cluster> <remote_dir>"
    CONFIG_DIR=$1
    CLOUDLAB_USER=$2
    CLOUDLAB_EXP_NAME=$3
    CLOUDLAB_PROJECT_NAME=$4
    CLOUDLAB_CLUSTER=$5
    CLOUDLAB_REMOTE_DIR=$6
elif [[ "$#" -gt 0 ]]; then
    echo "Usage: $0 <autobahn-config-directory> <user> <exp_name> <project_name> <cluster> <remote_dir>"
    exit 1
fi

echo "Setting up Autobahn on CloudLab with the following parameters:"
echo "User: $CLOUDLAB_USER"
echo "Experiment Name: $CLOUDLAB_EXP_NAME"
echo "Project Name: $CLOUDLAB_PROJECT_NAME"
echo "Cluster: $CLOUDLAB_CLUSTER"
echo "Remote Directory: $CLOUDLAB_REMOTE_DIR"

SERVER_IDX=0
while IFS= read -r host || [[ -n "$host" ]]; do
    echo "Uploading Autobahn config to $host"
    FULL_USER_HOST=$CLOUDLAB_USER@$host.$CLOUDLAB_EXP_NAME.$CLOUDLAB_PROJECT_NAME.$CLOUDLAB_CLUSTER.cloudlab.us
    (
        ssh $FULL_USER_HOST "bash -s" < $SCRIPT_DIR/setup_autobahn_tmpfs.sh $CLOUDLAB_REMOTE_DIR && \
        rsync -e ssh $CONFIG_DIR/\.*.json $FULL_USER_HOST:$CLOUDLAB_REMOTE_DIR
    ) &

    for ((i=0; i<$CLIENTS_PER_SERVER; i++)); do
        echo "Uploading Autobahn config to client-$SERVER_IDX-$i"
        FULL_USER_HOST=$CLOUDLAB_USER@client-$SERVER_IDX-$i.$CLOUDLAB_EXP_NAME.$CLOUDLAB_PROJECT_NAME.$CLOUDLAB_CLUSTER.cloudlab.us
        (
            ssh $FULL_USER_HOST "rm -rf $CLOUDLAB_REMOTE_DIR && mkdir -p $CLOUDLAB_REMOTE_DIR" && \
            rsync -e ssh $CONFIG_DIR/\.*.json $FULL_USER_HOST:$CLOUDLAB_REMOTE_DIR
        ) &
    done
    SERVER_IDX=$((SERVER_IDX + 1))
done < $CONFIG_DIR/server-hosts.txt

wait
