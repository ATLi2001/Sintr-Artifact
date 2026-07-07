# Reproducing Sintr Experiment Results

Complete the environment setup first. Run every command below from the `sintr-artifact` directory.

**Before each run:** clear `experiment-results/original`. Results from `collect_results.sh` land there and are not overwritten between runs.

The root `original` directory (distinct from `experiment-results/original`) is configured by default to store the complete raw experiment output. You may also need to clear or move its contents before running `collect_results.sh` after each experiment.

---

## Macrobenchmarks

For each benchmark, run the three steps below, substituting the config path and `-b` value from the table.

```bash
# 1. Run the experiment
./experiment-scripts/run_many_experiment_configs.sh <CONFIG> --recursive

# 2. Collect results into experiment-results/original
./experiment-scripts/collect_results.sh

# 3. Analyze and plot
python3 experiment-scripts/analyze_stats_file.py -b "<BENCH>" -o <output-dir> -p <plot-output-dir>
```

| Benchmark | `<BENCH>` | `<CONFIG>` (recursive run) | Per-system config base |
|-----------|-----------|-----------------------------|------------------------|
| TPC-C     | `tpcc`      | `experiment-configs/Sintr/1-Workloads/TPCC-SQL`   | `experiment-configs/Sintr/1-Workloads/TPCC`      |
| SEATS     | `seats`     | `experiment-configs/Sintr/1-Workloads/Seats`      | `experiment-configs/Sintr/1-Workloads/Seats`     |
| Smallbank | `smallbank` | `experiment-configs/Sintr/1-Workloads/Smallbank`  | `experiment-configs/Sintr/1-Workloads/Smallbank` |

**Hotstuff-Peloton and BFTSmart-Peloton** require different server configurations than Pesto, so you may need to run them individually. Instead of the recursive run in step 1, run each system's config from the per-system config base:

```bash
./experiment-scripts/run_many_experiment_configs.sh <PER-SYSTEM CONFIG BASE>/<BFTSmart|Pesto|Hotstuff>
```

---

## Microbenchmarks

Same three-step workflow, with a few differences per experiment (see table):

- **`--recursive`** — append it to the step 1 command only where the table says so.
- **`COLLECT_LOGS`** — where noted, set `COLLECT_LOGS=1` in `collect_results.sh` *before* running step 2.
- **Analyze flag** — some experiments select the benchmark with `-b`, others select a plot type with `-t`.

```bash
# 1. Run the experiment (append --recursive only where the table says so)
./experiment-scripts/run_many_experiment_configs.sh <CONFIG> [--recursive]

# 2. (If required) set COLLECT_LOGS=1 in collect_results.sh, then collect
./experiment-scripts/collect_results.sh

# 3. Analyze and plot with the flag from the table
python3 experiment-scripts/analyze_stats_file.py <ANALYZE FLAG> -o <output-dir> -p <plot-output-dir>
```

| Experiment | `<CONFIG>` | `--recursive`? | `COLLECT_LOGS=1`? | `<ANALYZE FLAG>` |
|------------|------------|:--------------:|:-----------------:|------------------|
| Vary policy — uniform    | `experiment-configs/Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Uniform-final` | –   | –   | `-b "rw-sql"`             |
| Vary policy — Zipfian    | `experiment-configs/Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Zipf-final`    | –   | –   | `-b "rw-sql"`             |
| Gov txn policy change    | `experiment-configs/Sintr/2-Microbenchmarks/2-Gov-Txn`                          | –   | yes | `-t "throughput_time"`    |
| Client failures — uniform| `experiment-configs/Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-U`         | yes | yes | `-t "client_failures"`    |
| Client failures — Zipfian| `experiment-configs/Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-Z`         | yes | yes | `-t "client_failures"`    |
| Lifting throughput       | `experiment-configs/Sintr/2-Microbenchmarks/4-Lifting`                          | –   | –   | `-t "tput_bar"`           |