#!/bin/bash

ROOTDIR="$HOME/Pequin-Artifact"
EXPERIMENT_CONFIGS_DIR=$1

# Check if the experiment configs directory is provided
if [[ -z "$EXPERIMENT_CONFIGS_DIR" ]]; then
    echo "Usage: $0 <experiment-configs-directory> [<num-trials>] [--recursive] [--dry-run]"
    exit 1
fi

NUM_TRIALS=1
RECURSIVE=false
DRY_RUN=false

# Parse optional arguments
shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --recursive|-r)
            RECURSIVE=true
            ;;
        --dry-run|-d)
            DRY_RUN=true
            ;;
        *)
            if [[ "$1" =~ ^[0-9]+$ ]]; then
                NUM_TRIALS="$1"
            else
                echo "Unknown argument: $1"
                echo "Usage: $0 <experiment-configs-directory> [<num-trials>] [--recursive]"
                exit 1
            fi
            ;;
    esac
    shift
done

if $RECURSIVE; then
    mapfile -t CONFIGS < <(find "$EXPERIMENT_CONFIGS_DIR" -type f -name "*.json" | sort)
else
    shopt -s nullglob
    CONFIGS=("$EXPERIMENT_CONFIGS_DIR"/*.json)
fi

if [[ ${#CONFIGS[@]} -eq 0 ]]; then
    echo "No experiment configuration files found."
    exit 1
fi

for config in "${CONFIGS[@]}"; do
    # skip config files that are vary jsons or base.json as these were used to generate the actual configs
    if [[ "$config" == *"vary"* ]] || [[ "$config" == *"base.json"* ]]; then
        continue
    fi
    for ((i=1; i<=NUM_TRIALS; i++)); do
        echo "Running experiment with configuration: $config, trial $i/$NUM_TRIALS"
        if $DRY_RUN; then
            break
        else
            python3 "$ROOTDIR/experiment-scripts/run_multiple_experiments.py" "$config"
        fi
    done
done