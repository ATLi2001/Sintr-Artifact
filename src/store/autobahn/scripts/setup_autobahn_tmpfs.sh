#!/bin/bash

# this script is expected to be run on the ssh machine

AUTOBAHN_DIR=$1
SIZE=20G

if [[ -z "$AUTOBAHN_DIR" ]]; then
    echo "Usage: $0 <autobahn-tmpfs-directory>"
    exit 1
fi

if [[ -d "$AUTOBAHN_DIR" ]]; then
    mountpoint -q $AUTOBAHN_DIR
    if [[ $? -eq 0 ]]; then
        echo "Cleaning up existing mount point at $AUTOBAHN_DIR"
        sudo umount -f $AUTOBAHN_DIR
    fi
    echo "Removing existing directory at $AUTOBAHN_DIR"
    rm -rf $AUTOBAHN_DIR
fi

mkdir -p $AUTOBAHN_DIR

sudo mount -t tmpfs -o size=$SIZE,nr_inodes=10k,mode=0777 tmpfs $AUTOBAHN_DIR
