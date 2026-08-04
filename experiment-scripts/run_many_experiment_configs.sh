#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifact root. Precedence: --rootdir flag > ROOTDIR env var > the directory this
# script lives in (experiment-scripts/..), which is correct for a normal checkout.
ROOTDIR="${ROOTDIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"

EXPERIMENT_CONFIGS_DIR=""
NUM_TRIALS=1
RECURSIVE=false
DRY_RUN=false

usage() {
    echo "Usage: $0 <experiment-configs-directory> [<num-trials>] [--recursive] [--dry-run] [--rootdir <path>]"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rootdir|-R)
            if [[ -z "${2:-}" ]]; then
                echo "Missing value for $1"
                usage
                exit 1
            fi
            ROOTDIR="$2"
            shift
            ;;
        --recursive|-r)
            RECURSIVE=true
            ;;
        --dry-run|-d)
            DRY_RUN=true
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [[ "$1" =~ ^[0-9]+$ ]]; then
                NUM_TRIALS="$1"
            elif [[ -z "$EXPERIMENT_CONFIGS_DIR" ]]; then
                EXPERIMENT_CONFIGS_DIR="$1"
            else
                echo "Unknown argument: $1"
                usage
                exit 1
            fi
            ;;
    esac
    shift
done

# Check if the experiment configs directory is provided
if [[ -z "$EXPERIMENT_CONFIGS_DIR" ]]; then
    usage
    exit 1
fi

RUN_SCRIPT="$ROOTDIR/experiment-scripts/run_multiple_experiments.py"
if [[ ! -f "$RUN_SCRIPT" ]]; then
    echo "run_multiple_experiments.py not found at $RUN_SCRIPT"
    echo "Pass the artifact root with --rootdir <path>."
    exit 1
fi

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
            python3 "$RUN_SCRIPT" "$config"
        fi
    done
done
