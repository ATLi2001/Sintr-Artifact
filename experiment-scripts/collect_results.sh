#!/bin/bash

# collect all results from the output directory and copy them to the experiment-results directory
# output directory is temporary, experiment-results is permanent

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Artifact root. Precedence: --rootdir flag > ROOTDIR env var > the directory this
# script lives in (experiment-scripts/..), which is correct for a normal checkout.
ROOTDIR="${ROOTDIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"

# COLLECT_LOGS can be passed as a positional argument (0 or 1). Default 0.
COLLECT_LOGS=0

usage() {
    echo "Usage: $0 [COLLECT_LOGS] [--rootdir <path>]  # COLLECT_LOGS must be 0 or 1"
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
        -h|--help)
            usage
            exit 0
            ;;
        *)
            COLLECT_LOGS="$1"
            ;;
    esac
    shift
done

# validate COLLECT_LOGS is 0 or 1
if [ "$COLLECT_LOGS" != "0" ] && [ "$COLLECT_LOGS" != "1" ]; then
    usage
    exit 1
fi

OUTPUT_DIR="$ROOTDIR/output"
EXPERIMENT_RESULTS_DIR="$ROOTDIR/experiment-results"
COLLECT_DIR="$EXPERIMENT_RESULTS_DIR/original"

if [ ! -d "$OUTPUT_DIR" ]; then
    echo "Output directory not found at $OUTPUT_DIR"
    echo "Pass the artifact root with --rootdir <path>."
    exit 1
fi

mkdir -p $COLLECT_DIR

# actual results are nested 3 levels deep in the output directory
for subdir in "$OUTPUT_DIR"/*; do
    if [ -d "$subdir" ]; then
        for subsubdir in "$subdir"/*; do
            if [ -d "$subsubdir" ]; then
                for subsubsubdir in "$subsubdir"/*; do
                    if [ -d "$subsubsubdir" ]; then
                        base_dir=$(basename "$subsubsubdir")
                        if [ "$base_dir" == "plots" ]; then
                            continue
                        fi
                        echo "Collecting results from $subsubsubdir"
                        # actual output is out/stats.json, save config files for reference
                        target_dir="$COLLECT_DIR/$base_dir"
                        mkdir -p "$target_dir"
                        cp "$subsubsubdir"/out/stats.json "$target_dir"
                        cp "$subsubsubdir"/*.config "$target_dir"
                        cp "$subsubsubdir"/*.json "$target_dir"

                        if [ $COLLECT_LOGS -eq 1 ]; then
                            mkdir -p "$target_dir/logs"
                            cp "$subsubsubdir"/out/client-*/*stdout*.log "$target_dir/logs"
                        fi
                    fi
                done
            fi
        done
    fi
done
