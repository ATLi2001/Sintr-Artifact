#!/bin/bash

N=10
OUTPUT_DIR="0_local_test_outputs"

num_files=0
for file in $OUTPUT_DIR/client-*.out; do
    num_files=$((num_files + 1))
done

for ((i=1; i<=N; i++)); do
    echo "Iteration: $i"
    ./server-tester.sh
    sleep 2
    ./client-tester.sh
    # check client output
    all_latencies=0
    for file in $OUTPUT_DIR/client-*.out; do
        if [ -f "$file" ]; then
            if grep -q LATENCY $file && ! grep -q mismatch $file && ! grep -q panic $file; then
                echo "$file: OK"
            elif grep -q mismatch $file; then
                echo "$file: MISMATCH FOUND"
                exit 1
            elif grep -q Panic $file; then
                echo "$file: PANIC FOUND"
                exit 1
            else
                echo "$file: LATENCIES NOT PRINTED"
                all_latencies=$((all_latencies + 1))
            fi
        fi
    done
    if [ "$all_latencies" -gt 1 ]; then 
        exit 1
    fi
    sleep 1
    ps aux | grep benchmark
    ps aux | grep store/server
done

echo "Done"