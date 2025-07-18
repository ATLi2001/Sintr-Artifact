#!/bin/bash

ROOTDIR="$HOME/Pequin-Artifact"
EXPERIMENT_CONFIGS_DIR=$1
# check if the experiment configs directory is provided
if [[ -z "$EXPERIMENT_CONFIGS_DIR" ]]; then
    echo "Usage: $0 <experiment-configs-directory> [<num-trials>]"
    exit 1
fi

NUM_TRIALS=1
# if second argument is provided, use it as the number of trials
if [[ ! -z "$2" ]]; then
    NUM_TRIALS=$2
fi

for config in "$EXPERIMENT_CONFIGS_DIR"/*.json; do
    for ((i=1; i<=NUM_TRIALS; i++)); do
        echo "Running experiment with configuration: $config, trial $i/$NUM_TRIALS"
        python3 $ROOTDIR/experiment-scripts/run_multiple_experiments.py "$config"
    done
done
