#!/bin/bash
#
# One-shot driver for the Sintr workload benchmarks.
#
# Does, in order:
#   1. Updates every experiment config with the matching override from
#      experiment-scripts/example_user_overrides (per the table in RunningExperiments.md).
#   2. Generates the benchmark data locally (seats, tpcc, tpcc-lifting).
#   3. Uploads the generated data to the CloudLab machines via src/upload_data_remote.sh,
#      forwarding the connection arguments given to this script.
#   4. For each benchmark: clears output/ + experiment-results/original, runs the configs,
#      collects results, runs analyze_stats_file.py, then archives everything into a fresh
#      directory under the archive root and clears experiment-results/original again.
#
# Usage:
#   ./experiment-scripts/run_all_benchmarks.sh -u <cloudlab-user> -o <archive-root> [options]
#
# See usage() below for the full option list.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOTDIR="${ROOTDIR:-$(cd "$SCRIPT_DIR/.." && pwd)}"

SRC_DIR="$ROOTDIR/src"
CONFIGS_DIR="$ROOTDIR/experiment-configs"
OVERRIDES_DIR="$SCRIPT_DIR/example_user_overrides"
OUTPUT_DIR="$ROOTDIR/output"
ORIGINAL_DIR="$ROOTDIR/experiment-results/original"

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------

CLOUDLAB_USER=""
EXP_NAME=""
CLUSTER_NAME=""
CLIENTS_PER_SERVER=""
NUM_SHARDS=""
FIRST_TIME_CONNECTION=""
PG_MODE=""
UPLOAD_EXTRA=""

ARCHIVE_ROOT=""
NUM_WAREHOUSES=20
SEATS_SCALE_FACTOR=1
BENCHMARKS_ARG=""
NUM_TRIALS=1
NUM_TRIALS_SET=0

DO_CONFIGS=1
DO_GENERATE=1
DO_UPLOAD=1
DO_RUN=1
BACKUP_CONFIGS=0
DRY_RUN=0

usage() {
    cat <<EOF
Usage: $0 -u <cloudlab-user> -o <archive-root> [options]

Required:
  -u <user>         CloudLab username (forwarded to upload_data_remote.sh -u)
  -o <dir>          Archive root. Each run creates <dir>/<timestamp>/<benchmark>/

Forwarded to src/upload_data_remote.sh (only passed when given):
  -e <name>         CloudLab experiment name          (-e)
  -c <cluster>      CloudLab cluster, e.g. utah       (-c)
  -n <count>        Clients per server                (-n)
  -s <count>        Number of shards                  (-s)
  -f <0|1>          First-time SSH connection         (-f)
  -p <0|1>          Postgres client naming mode       (-p)
  --upload-extra "<args>"   Extra raw args appended to every upload invocation

Benchmark data generation:
  -w <count>        TPC-C warehouses (default: $NUM_WAREHOUSES)
  -S <factor>       Seats scale factor (default: $SEATS_SCALE_FACTOR)

Experiment selection / behaviour:
  -b "<list>"       Space/comma separated benchmarks and/or groups (default: all).
                    Groups:     all, workloads, micro
                    Workloads:  ${WORKLOAD_BENCHMARKS:-}
                    Micro:      ${MICRO_BENCHMARKS:-}
  -t <count>        Trials per config (default 1, except random-policy which the paper
                    runs 3 times; passing -t explicitly overrides that)
  --skip-configs    Do not run update_configs.py
  --skip-generate   Do not generate benchmark data locally
  --skip-upload     Do not upload benchmark data to the remote machines
  --skip-run        Do everything except running/collecting/analyzing experiments
  --backup-configs  Pass --backup to update_configs.py
  --dry-run         Print every command instead of executing it
  -h, --help        Show this help

Example:
  $0 -u dhl001 -e sintr -c utah -n 1 -o "\$HOME/sintr-archive"
EOF
}

# ----------------------------------------------------------------------------
# Benchmark definitions
# ----------------------------------------------------------------------------

# Section 1 of RunningExperiments.md
WORKLOAD_BENCHMARKS="smallbank seats tpcc-sql"
# Section 2 of RunningExperiments.md, in the order the sub-sections appear
MICRO_BENCHMARKS="vary-policy-u vary-policy-z gov-txn client-failures-u client-failures-z tpcc-sql-lifting random-policy"
BENCH_ORDER_DEFAULT="$WORKLOAD_BENCHMARKS $MICRO_BENCHMARKS"

declare -A BENCH_CONFIG_DIR=(
    [smallbank]="Sintr/1-Workloads/Smallbank"
    [seats]="Sintr/1-Workloads/Seats"
    [tpcc-sql]="Sintr/1-Workloads/TPCC-SQL"
    [vary-policy-u]="Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Uniform-final"
    [vary-policy-z]="Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Zipf-final"
    [gov-txn]="Sintr/2-Microbenchmarks/2-Gov-Txn"
    [client-failures-u]="Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-U"
    [client-failures-z]="Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-Z"
    [tpcc-sql-lifting]="Sintr/2-Microbenchmarks/4-Lifting"
    [random-policy]="Sintr/2-Microbenchmarks/5-Random-Policy/RW-SQL-U"
)

# whether run_many_experiment_configs.sh gets --recursive
declare -A BENCH_RECURSIVE=(
    [smallbank]=1
    [seats]=1
    [tpcc-sql]=1
    [vary-policy-u]=0
    [vary-policy-z]=0
    [gov-txn]=0
    [client-failures-u]=1
    [client-failures-z]=1
    [tpcc-sql-lifting]=0
    [random-policy]=1
)

# COLLECT_LOGS argument for collect_results.sh
declare -A BENCH_COLLECT_LOGS=(
    [smallbank]=0
    [seats]=0
    [tpcc-sql]=0
    [vary-policy-u]=0
    [vary-policy-z]=0
    [gov-txn]=1
    [client-failures-u]=1
    [client-failures-z]=1
    [tpcc-sql-lifting]=0
    [random-policy]=0
)

# analyze_stats_file.py selection flags (see the tables in RunningExperiments.md)
declare -A BENCH_ANALYZE_FLAGS=(
    [smallbank]="-b smallbank"
    [seats]="-b seats"
    [tpcc-sql]="-b tpcc"
    [vary-policy-u]="-b rw-sql"
    [vary-policy-z]="-b rw-sql"
    [gov-txn]="-t throughput_time"
    [client-failures-u]="-t client_failures"
    [client-failures-z]="-t client_failures"
    [tpcc-sql-lifting]="-t tput_bar"
    [random-policy]="-t latency_percentiles_bar"
)

# Per-benchmark trial count, for experiments the paper repeats. Only listed where it
# differs from 1; an explicit -t on the command line overrides everything here.
declare -A BENCH_TRIALS=(
    [random-policy]=3   # section 2.5: "running each experiment 3 times"
)

# `-b` value handed to src/generate_benchmark_data.sh and src/upload_data_remote.sh.
# Empty means the benchmark has no locally generated data: smallbank's data is
# pre-provisioned on the CloudLab machines at /usr/local/etc/smallbank_data, and the
# rw-sql microbenchmarks need only the schema at /users/<user>/rw-sql.json, for which
# the artifact ships no generator or uploader.
declare -A BENCH_DATA_NAME=(
    [smallbank]=""
    [seats]="seats"
    [tpcc-sql]="tpcc"
    [vary-policy-u]=""
    [vary-policy-z]=""
    [gov-txn]=""
    [client-failures-u]=""
    [client-failures-z]=""
    [tpcc-sql-lifting]="tpcc-lifting"
    [random-policy]=""
)

SMALLBANK_DATA_NOTE="data is pre-provisioned on the CloudLab machines at /usr/local/etc/smallbank_data"
RW_SQL_DATA_NOTE="rw-sql needs only the schema at /users/<user>/rw-sql.json, which the artifact ships no generator or uploader for"

# Why a benchmark in BENCH_DATA_NAME has no data step; printed when it is skipped.
declare -A BENCH_DATA_NOTE=(
    [smallbank]="$SMALLBANK_DATA_NOTE"
    [vary-policy-u]="$RW_SQL_DATA_NOTE"
    [vary-policy-z]="$RW_SQL_DATA_NOTE"
    [gov-txn]="$RW_SQL_DATA_NOTE"
    [client-failures-u]="$RW_SQL_DATA_NOTE"
    [client-failures-z]="$RW_SQL_DATA_NOTE"
    [random-policy]="$RW_SQL_DATA_NOTE"
)

# config-directory -> override-file mapping, exactly as listed in RunningExperiments.md
CONFIG_OVERRIDE_MAP=(
    "Sintr/1-Workloads/TPCC-SQL/TPCC-SQL-final|example_user_override-tpcc.json"
    "Sintr/1-Workloads/TPCC-SQL/Hotstuff|example_user_override-tpcc.json"
    "Sintr/1-Workloads/TPCC-SQL/BFTSmart|example_user_override-tpcc-smart.json"
    "Sintr/1-Workloads/Seats/Pesto|example_user_override-seats.json"
    "Sintr/1-Workloads/Seats/Hotstuff|example_user_override-seats.json"
    "Sintr/1-Workloads/Seats/BFTSmart|example_user_override-seats-smart.json"
    "Sintr/1-Workloads/Smallbank/Basil|example_user_override-smallbank.json"
    "Sintr/1-Workloads/Smallbank/Hotstuff|example_user_override-smallbank.json"
    "Sintr/1-Workloads/Smallbank/BFTSmart|example_user_override-smallbank-smart.json"
    "Sintr/2-Microbenchmarks/1-Vary-Policy|example_user_override-micro.json"
    "Sintr/2-Microbenchmarks/2-Gov-Txn|example_user_override-gov-txn.json"
    "Sintr/2-Microbenchmarks/3-Client-Failures|example_user_override-micro.json"
    "Sintr/2-Microbenchmarks/4-Lifting|example_user_override-tpcc-lifting.json"
    # Section 2.5 has no row in the RunningExperiments.md override table; it is an
    # rw-sql experiment, so it follows the other rw-sql microbenchmarks.
    "Sintr/2-Microbenchmarks/5-Random-Policy/RW-SQL-U|example_user_override-micro.json"
)

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------

log()  { echo "[$(date '+%H:%M:%S')] $*"; }
warn() { echo "[$(date '+%H:%M:%S')] WARNING: $*" >&2; }
die()  { echo "[$(date '+%H:%M:%S')] ERROR: $*" >&2; exit 1; }

# Run a command, honouring --dry-run.
run() {
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  + $*"
        return 0
    fi
    "$@"
}

# ----------------------------------------------------------------------------
# Argument parsing
# ----------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
    case "$1" in
        -u) CLOUDLAB_USER="$2"; shift 2;;
        -e) EXP_NAME="$2"; shift 2;;
        -c) CLUSTER_NAME="$2"; shift 2;;
        -n) CLIENTS_PER_SERVER="$2"; shift 2;;
        -s) NUM_SHARDS="$2"; shift 2;;
        -f) FIRST_TIME_CONNECTION="$2"; shift 2;;
        -p) PG_MODE="$2"; shift 2;;
        -o) ARCHIVE_ROOT="$2"; shift 2;;
        -w) NUM_WAREHOUSES="$2"; shift 2;;
        -S) SEATS_SCALE_FACTOR="$2"; shift 2;;
        -b) BENCHMARKS_ARG="$2"; shift 2;;
        -t) NUM_TRIALS="$2"; NUM_TRIALS_SET=1; shift 2;;
        --upload-extra) UPLOAD_EXTRA="$2"; shift 2;;
        --skip-configs) DO_CONFIGS=0; shift;;
        --skip-generate) DO_GENERATE=0; shift;;
        --skip-upload) DO_UPLOAD=0; shift;;
        --skip-run) DO_RUN=0; shift;;
        --backup-configs) BACKUP_CONFIGS=1; shift;;
        --dry-run|-d) DRY_RUN=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown argument: $1" >&2; usage; exit 1;;
    esac
done

[[ -n "$CLOUDLAB_USER" ]] || { echo "Missing required -u <cloudlab-user>" >&2; usage; exit 1; }
[[ -n "$ARCHIVE_ROOT" ]]  || { echo "Missing required -o <archive-root>" >&2; usage; exit 1; }

[[ -d "$SRC_DIR" ]]     || die "src directory not found at $SRC_DIR (set ROOTDIR to the artifact root)"
[[ -d "$CONFIGS_DIR" ]] || die "experiment-configs not found at $CONFIGS_DIR"
[[ -d "$OVERRIDES_DIR" ]] || die "override directory not found at $OVERRIDES_DIR"

# Resolve the benchmark list, expanding the group aliases
read -r -a requested <<< "${BENCHMARKS_ARG//,/ }"
[[ ${#requested[@]} -eq 0 ]] && requested=(all)

BENCHMARKS=()
for item in "${requested[@]}"; do
    case "$item" in
        all)                        read -r -a expanded <<< "$BENCH_ORDER_DEFAULT";;
        workloads)                  read -r -a expanded <<< "$WORKLOAD_BENCHMARKS";;
        micro|microbenchmarks)      read -r -a expanded <<< "$MICRO_BENCHMARKS";;
        *)                          expanded=("$item");;
    esac
    for bench in "${expanded[@]}"; do
        [[ -v BENCH_CONFIG_DIR[$bench] ]] \
            || die "unknown benchmark '$bench' (known: $BENCH_ORDER_DEFAULT; groups: all workloads micro)"
        # keep the first occurrence so a group plus an explicit name doesn't run twice
        [[ " ${BENCHMARKS[*]-} " == *" $bench "* ]] || BENCHMARKS+=("$bench")
    done
done

RUN_STAMP="$(date '+%Y-%m-%d-%H-%M-%S')"
ARCHIVE_RUN_DIR="$ARCHIVE_ROOT/$RUN_STAMP"

log "Artifact root : $ROOTDIR"
log "Benchmarks    : ${BENCHMARKS[*]}"
log "Archive dir   : $ARCHIVE_RUN_DIR"
[[ $DRY_RUN -eq 1 ]] && log "DRY RUN - no commands will be executed"

# ----------------------------------------------------------------------------
# Step 1: update all configs with the example overrides
# ----------------------------------------------------------------------------

if [[ $DO_CONFIGS -eq 1 ]]; then
    log "=== Step 1/4: updating experiment configs from $OVERRIDES_DIR ==="
    backup_flag=()
    [[ $BACKUP_CONFIGS -eq 1 ]] && backup_flag=(--backup)

    for entry in "${CONFIG_OVERRIDE_MAP[@]}"; do
        cfg_rel="${entry%%|*}"
        override_name="${entry##*|}"
        cfg_path="$CONFIGS_DIR/$cfg_rel"
        override_path="$OVERRIDES_DIR/$override_name"

        if [[ ! -d "$cfg_path" ]]; then
            warn "config directory missing, skipping: $cfg_path"
            continue
        fi
        if [[ ! -f "$override_path" ]]; then
            warn "override file missing, skipping: $override_path"
            continue
        fi

        log "  $cfg_rel  <-  $override_name"
        run python3 "$SCRIPT_DIR/update_configs.py" "$cfg_path" "$override_path" "${backup_flag[@]}" \
            || die "update_configs.py failed for $cfg_path"

        # update_configs.py writes backups to <configs_folder>/backups, which a later
        # --recursive run would happily pick up as real configs. Move them out of the way.
        if [[ $BACKUP_CONFIGS -eq 1 && $DRY_RUN -eq 0 && -d "$cfg_path/backups" ]]; then
            backup_dest="$ROOTDIR/experiment-config-backups/$RUN_STAMP/$cfg_rel"
            mkdir -p "$(dirname "$backup_dest")"
            mv "$cfg_path/backups" "$backup_dest"
            log "    backups moved to $backup_dest"
        fi
    done
else
    log "=== Step 1/4: skipping config update (--skip-configs) ==="
fi

# ----------------------------------------------------------------------------
# Step 2: generate benchmark data locally
# ----------------------------------------------------------------------------

if [[ $DO_GENERATE -eq 1 ]]; then
    log "=== Step 2/4: generating benchmark data ==="
    for bench in "${BENCHMARKS[@]}"; do
        data_name="${BENCH_DATA_NAME[$bench]}"
        if [[ -z "$data_name" ]]; then
            log "  $bench: skipping - ${BENCH_DATA_NOTE[$bench]:-no local data to generate}"
            continue
        fi

        gen_args=(-b "$data_name")
        case "$data_name" in
            tpcc|tpcc-lifting) gen_args+=(-n "$NUM_WAREHOUSES");;
            seats)             gen_args+=(-s "$SEATS_SCALE_FACTOR");;
        esac

        log "  $bench: generate_benchmark_data.sh ${gen_args[*]}"
        if [[ $DRY_RUN -eq 1 ]]; then
            echo "  + (cd $SRC_DIR && ./generate_benchmark_data.sh ${gen_args[*]})"
        else
            ( cd "$SRC_DIR" && ./generate_benchmark_data.sh "${gen_args[@]}" ) \
                || die "benchmark data generation failed for $bench"
        fi
    done
else
    log "=== Step 2/4: skipping data generation (--skip-generate) ==="
fi

# ----------------------------------------------------------------------------
# Step 3: upload benchmark data to the remote machines
# ----------------------------------------------------------------------------

# Assemble the shared upload arguments once; only forward what the caller supplied
# so upload_data_remote.sh keeps its own defaults for everything else.
UPLOAD_COMMON=(-u "$CLOUDLAB_USER")
[[ -n "$EXP_NAME" ]]              && UPLOAD_COMMON+=(-e "$EXP_NAME")
[[ -n "$CLUSTER_NAME" ]]          && UPLOAD_COMMON+=(-c "$CLUSTER_NAME")
[[ -n "$CLIENTS_PER_SERVER" ]]    && UPLOAD_COMMON+=(-n "$CLIENTS_PER_SERVER")
[[ -n "$NUM_SHARDS" ]]            && UPLOAD_COMMON+=(-s "$NUM_SHARDS")
[[ -n "$FIRST_TIME_CONNECTION" ]] && UPLOAD_COMMON+=(-f "$FIRST_TIME_CONNECTION")
[[ -n "$PG_MODE" ]]               && UPLOAD_COMMON+=(-p "$PG_MODE")
if [[ -n "$UPLOAD_EXTRA" ]]; then
    read -r -a upload_extra_arr <<< "$UPLOAD_EXTRA"
    UPLOAD_COMMON+=("${upload_extra_arr[@]}")
fi

if [[ $DO_UPLOAD -eq 1 ]]; then
    log "=== Step 3/4: uploading benchmark data ==="
    first_upload=1
    for bench in "${BENCHMARKS[@]}"; do
        data_name="${BENCH_DATA_NAME[$bench]}"
        if [[ -z "$data_name" ]]; then
            log "  $bench: nothing to upload - skipping"
            continue
        fi

        upload_args=("${UPLOAD_COMMON[@]}" -b "$data_name")
        # host authenticity only needs establishing once; later getopts values win,
        # so appending -f 0 overrides the -f carried in UPLOAD_COMMON.
        if [[ $first_upload -eq 0 && -n "$FIRST_TIME_CONNECTION" ]]; then
            upload_args+=(-f 0)
        fi
        first_upload=0

        log "  $bench: upload_data_remote.sh ${upload_args[*]}"
        if [[ $DRY_RUN -eq 1 ]]; then
            echo "  + (cd $SRC_DIR && ./upload_data_remote.sh ${upload_args[*]})"
        else
            ( cd "$SRC_DIR" && ./upload_data_remote.sh "${upload_args[@]}" ) \
                || die "upload failed for $bench"
        fi
    done
else
    log "=== Step 3/4: skipping upload (--skip-upload) ==="
fi

# ----------------------------------------------------------------------------
# Step 4: run / collect / analyze / archive each benchmark
# ----------------------------------------------------------------------------

# Empty a directory's contents without removing the directory itself.
clear_dir() {
    local dir="$1"
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  + rm -rf $dir/*"
        return 0
    fi
    mkdir -p "$dir"
    find "$dir" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
}

# Move a directory's contents into a destination, creating the destination first.
move_contents() {
    local src="$1" dest="$2"
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  + mv $src/* $dest/"
        return 0
    fi
    mkdir -p "$dest"
    if [[ -d "$src" ]] && [[ -n "$(ls -A "$src" 2>/dev/null)" ]]; then
        find "$src" -mindepth 1 -maxdepth 1 -exec mv -t "$dest" {} +
    fi
}

FAILED_BENCHMARKS=()

if [[ $DO_RUN -eq 1 ]]; then
    log "=== Step 4/4: running experiments ==="
    run mkdir -p "$ARCHIVE_RUN_DIR"

    # Both output/ and experiment-results/original get wiped before every benchmark.
    # Anything already sitting there is from an earlier session, so park it in the
    # archive instead of deleting it.
    for pre in "$ORIGINAL_DIR:original" "$OUTPUT_DIR:raw-output"; do
        pre_dir="${pre%%:*}"
        pre_name="${pre##*:}"
        if [[ -d "$pre_dir" && -n "$(ls -A "$pre_dir" 2>/dev/null)" ]]; then
            log "  $pre_dir is not empty; moving existing contents to $ARCHIVE_RUN_DIR/pre-existing/$pre_name"
            move_contents "$pre_dir" "$ARCHIVE_RUN_DIR/pre-existing/$pre_name"
        fi
    done

    for bench in "${BENCHMARKS[@]}"; do
        config_dir="$CONFIGS_DIR/${BENCH_CONFIG_DIR[$bench]}"
        bench_archive="$ARCHIVE_RUN_DIR/$bench"

        log "--------------------------------------------------------------"
        log "Benchmark: $bench"
        log "  configs : $config_dir"
        log "  archive : $bench_archive"

        if [[ ! -d "$config_dir" ]]; then
            warn "config directory missing, skipping benchmark: $config_dir"
            FAILED_BENCHMARKS+=("$bench (missing configs)")
            continue
        fi

        # (a) start from a clean output/ and experiment-results/original
        log "  clearing $OUTPUT_DIR and $ORIGINAL_DIR"
        clear_dir "$OUTPUT_DIR"
        clear_dir "$ORIGINAL_DIR"

        # (b) run every config for this benchmark
        bench_trials="$NUM_TRIALS"
        if [[ $NUM_TRIALS_SET -eq 0 && -v BENCH_TRIALS[$bench] ]]; then
            bench_trials="${BENCH_TRIALS[$bench]}"
        fi

        run_args=("$config_dir" "$bench_trials" --rootdir "$ROOTDIR")
        [[ "${BENCH_RECURSIVE[$bench]}" -eq 1 ]] && run_args+=(--recursive)

        log "  run_many_experiment_configs.sh ${run_args[*]}"
        if ! run "$SCRIPT_DIR/run_many_experiment_configs.sh" "${run_args[@]}"; then
            warn "experiment run failed for $bench; continuing with the next benchmark"
            FAILED_BENCHMARKS+=("$bench (run failed)")
            continue
        fi

        # (c) collect raw stats into experiment-results/original
        log "  collect_results.sh ${BENCH_COLLECT_LOGS[$bench]} --rootdir $ROOTDIR"
        if ! run "$SCRIPT_DIR/collect_results.sh" "${BENCH_COLLECT_LOGS[$bench]}" --rootdir "$ROOTDIR"; then
            warn "collect_results.sh failed for $bench; continuing with the next benchmark"
            FAILED_BENCHMARKS+=("$bench (collect failed)")
            continue
        fi

        # (d) analyze; csv + plots are written straight into the archive
        read -r -a analyze_flags <<< "${BENCH_ANALYZE_FLAGS[$bench]}"
        log "  analyze_stats_file.py ${analyze_flags[*]}"
        run mkdir -p "$bench_archive/csv" "$bench_archive/plots"
        if ! run python3 "$SCRIPT_DIR/analyze_stats_file.py" \
                "${analyze_flags[@]}" \
                -s "$ORIGINAL_DIR" \
                -o "$bench_archive/csv" \
                -p "$bench_archive/plots"; then
            warn "analyze_stats_file.py failed for $bench; archiving raw data anyway"
            FAILED_BENCHMARKS+=("$bench (analyze failed)")
        fi

        # (e) archive the raw output and the collected stats, then clear both
        log "  archiving into $bench_archive"
        move_contents "$ORIGINAL_DIR" "$bench_archive/original"
        move_contents "$OUTPUT_DIR" "$bench_archive/raw-output"

        log "  clearing $ORIGINAL_DIR and $OUTPUT_DIR for the next benchmark"
        clear_dir "$ORIGINAL_DIR"
        clear_dir "$OUTPUT_DIR"

        log "Benchmark $bench done."
    done
else
    log "=== Step 4/4: skipping experiment runs (--skip-run) ==="
fi

log "=============================================================="
if [[ ${#FAILED_BENCHMARKS[@]} -gt 0 ]]; then
    warn "Completed with failures:"
    for f in "${FAILED_BENCHMARKS[@]}"; do warn "  - $f"; done
    log "Results archived under $ARCHIVE_RUN_DIR"
    exit 1
fi

log "All benchmarks completed. Results archived under $ARCHIVE_RUN_DIR"
