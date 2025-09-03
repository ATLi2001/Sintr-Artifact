#!/bin/bash

CONFIG_DIR=$1
CLIENTS_PER_SERVER=1

if [[ -z "$CONFIG_DIR" ]]; then
    echo "Usage: $0 <autobahn-config-directory>"
    exit 1
fi

source .env

echo "Setting up Autobahn on CloudLab with the following parameters:"
echo "User: $CLOUDLAB_USER"
echo "Experiment Name: $CLOUDLAB_EXP_NAME"
echo "Project Name: $CLOUDLAB_PROJECT_NAME"
echo "Cluster: $CLOUDLAB_CLUSTER"
echo "Remote Directory: $CLOUDLAB_REMOTE_DIR"

SERVER_IDX=0
while IFS= read -r host || [[ -n "$host" ]]; do
    echo "Uploading Autobahn config to $host"
    rsync -r -e ssh $CONFIG_DIR $CLOUDLAB_USER@$host.$CLOUDLAB_EXP_NAME.$CLOUDLAB_PROJECT_NAME.$CLOUDLAB_CLUSTER.cloudlab.us:$CLOUDLAB_REMOTE_DIR &
    for ((i=0; i<$CLIENTS_PER_SERVER; i++)); do
        echo "Uploading Autobahn config to client-$SERVER_IDX-$i"
        rsync -r -e ssh $CONFIG_DIR $CLOUDLAB_USER@client-$SERVER_IDX-$i.$CLOUDLAB_EXP_NAME.$CLOUDLAB_PROJECT_NAME.$CLOUDLAB_CLUSTER.cloudlab.us:$CLOUDLAB_REMOTE_DIR &
    done
    SERVER_IDX=$((SERVER_IDX + 1))
done < $CONFIG_DIR/server-hosts.txt

wait
