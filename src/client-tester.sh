#!/bin/bash

CLIENTS=2
F=0
NUM_GROUPS=1
CONFIG="0_local_test_outputs/configs/shard-r1.config"
CLIENTS_CONFIG="0_local_test_outputs/configs/clients-r${CLIENTS}.config"
POLICY_CONFIG="0_local_test_outputs/configs/policy-tpcc-wh.config"
POLICY_FUNCTION="tpcc_sql_wh"
PROTOCOL="sintr"
STORE=${PROTOCOL}store
DURATION=5
ZIPF=0.0
NUM_OPS_TX=1
NUM_KEYS_IN_DB=10000
KEY_PATH="keys"

STORE_MODE="true"
SQL_BENCH="true"

# BENCHMARK="tpcc-sync"
# BENCHMARK="rw-sync"

# BENCHMARK="rw-sql"
# FILE_PATH="0_local_test_outputs/rw-sql/rw-sql.json"

BENCHMARK="tpcc-sql"
FILE_PATH="store/benchmark/async/sql/tpcc/sql-tpcc-tables-schema.json"

#BENCHMARK="seats-sql"
#FILE_PATH="store/benchmark/async/sql/seats/sql-seats-tables-schema.json"

#BENCHMARK="auctionmark-sql"
#FILE_PATH="store/benchmark/async/sql/auctionmark/sql-auctionmark-tables-schema.json"


while getopts c:f:g:p:s:d:z:o:k:b: option; do
case "${option}" in
c) CLIENTS=${OPTARG};;
f) F=${OPTARG};;
g) NUM_GROUPS=${OPTARG};;
p) CONFIG=${OPTARG};;
s) PROTOCOL=${OPTARG};;
d) DURATION=${OPTARG};;
z) ZIPF=${OPTARG};;
o) NUM_OPS_TX=${OPTARG};;
k) NUM_KEYS_IN_DB=${OPTARG};;
b) BENCHMARK=${OPTARG};;
esac;
done

N=$((5*$F+1))

DEBUG_FILES="store/$STORE/"
# gdb -ex r -ex bt --args 
# valgrind --tool=callgrind
# array of process ids
pids=()

echo '[1] Starting new clients'
for i in `seq 1 $((CLIENTS-1))` 0; do
  #valgrind
  DEBUG=$DEBUG_FILES store/benchmark/async/benchmark --config_path $CONFIG --clients_config_path $CLIENTS_CONFIG --num_groups $NUM_GROUPS \
    --num_shards $NUM_GROUPS \
    --protocol_mode $PROTOCOL --num_keys $NUM_KEYS_IN_DB --benchmark $BENCHMARK --sql_bench=$SQL_BENCH \
    --data_file_path $FILE_PATH \
    --num_ops_txn $NUM_OPS_TX --exp_duration $DURATION --client_id $i --num_client_hosts $CLIENTS --warmup_secs 0 \
    --value_size -1 --max_range 10 --scan_as_point=false --rw_secondary_condition=false \
    --cooldown_secs 0 --key_selector uniform --zipf_coefficient $ZIPF \
    --indicus_key_path $KEY_PATH \
    --indicus_sig_batch 1 \
    --tpcc_num_warehouses 2 \
    --store_mode=$STORE_MODE --indicus_hash_digest=true --indicus_verify_deps=false --indicus_parallel_CCC=false \
    --pequin_query_cache_read_set=false \
    --sintr_sign_finish_validation=true --sintr_sign_fwd_read_results=true --sintr_client_check_evidence=true \
    --sintr_debug_endorse_check=true \
    --sintr_max_val_threads=1 --sintr_policy_config_path $POLICY_CONFIG  --sintr_policy_function_name $POLICY_FUNCTION \
    --sintr_read_include_policy=0 --indicus_no_fallback=false --sintr_min_enable_pull_policies=0 \
    --sintr_c2c_send_thread=false --sintr_c2c_receive_thread=false --sintr_parallel_endorsement_check=false \
    --sintr_hash_endorsements=true \
    --sintr_optimistic_receive_endorsement=false \
    --sintr_parallel_query_sigs_check=false --sintr_max_client_sig_check_threads 0 \
    --sintr_val_client_selector "ring" --sintr_hide_timestamps=false &> ./0_local_test_outputs/client-$i.out &
  pids+=($!)
done;

sleep $((DURATION+4))
echo '[2] Shutting down possibly open servers and clients'
killall store/benchmark/async/benchmark
#callgrind_control --dump
killall store/server

# sometimes killall doesn't work so make sure we stop all processes
for pid in "${pids[@]}"; do
  kill $pid
done