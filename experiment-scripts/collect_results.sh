#!/bin/bash

# collect all results from the output directory and copy them to the experiment-results directory
# output directory is temporary, experiment-results is permanent

ROOTDIR="$HOME/Sintr-Artifact"
OUTPUT_DIR="$ROOTDIR/output"
EXPERIMENT_RESULTS_DIR="$ROOTDIR/experiment-results"
COLLECT_DIR="$EXPERIMENT_RESULTS_DIR/original"
# COLLECT_LOGS can be passed as the first positional argument (0 or 1). Default 0.
COLLECT_LOGS="${1:-0}"

# validate COLLECT_LOGS is 0 or 1
if [ "$COLLECT_LOGS" != "0" ] && [ "$COLLECT_LOGS" != "1" ]; then
    echo "Usage: $0 [COLLECT_LOGS]  # COLLECT_LOGS must be 0 or 1"
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
