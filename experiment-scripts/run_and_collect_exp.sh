#!/bin/bash

ROOTDIR="$HOME/Pequin-Artifact"

"$ROOTDIR/experiment-scripts/run_many_experiment_configs.sh" "$1" "$2"
rm -rf "$ROOTDIR/experiment-results/original"/*
"$ROOTDIR/experiment-scripts/collect_results.sh"
python3 "$ROOTDIR/experiment-scripts/analyze_stats_file.py"