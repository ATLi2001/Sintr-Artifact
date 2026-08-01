# Reviewer-Reported Issues

Notes on the issues raised during artifact review, what causes each one, and how to fix it.
Everything here was reproduced and fixed on a CloudLab control-machine deployment; where a fix
has *not* been verified on `fsp-dev`, that is called out explicitly.

Related documents:
- [CloudlabSetup.md](CloudlabSetup.md) — provisioning the CloudLab experiment
- [cloudlab-control-machine.md](cloudlab-control-machine.md) — disk layout, build, and SSH notes for the control machine
- [RunningExperiments.md](RunningExperiments.md) — the full experiment workflow

# Table of Contents
1. [Compilation fails on a fresh CloudLab image (`statements.h`)](#statements)
2. [Smallbank Tx-HS crashes on `local_config`](#localconfig)
3. [`analyze_stats_file.py -b` produces empty plots](#emptyplots)
4. [Missing control-machine dependencies](#deps)
5. [One-shot experiment setup](#oneshot)
6. [Known remaining issues](#remaining)

---

## (1) Compilation fails on a fresh CloudLab image <a name="statements"></a>

### Symptom

`make -j` fails on the first compilation after instantiating a new CloudLab experiment and
loading the disk image. The errors are missing-type errors out of the parser headers, e.g.

```
error: 'SQLStatementList' in namespace 'peloton::parser' does not name a type;
did you mean 'SQLStatement'?
```

The same tree compiles cleanly on `fsp-dev`.

### Cause

There are three copies of `statements.h` in the tree:

```
src/store/pequinstore/query-engine/parser/statements.h
src/store/sintrstore/query-engine/parser/statements.h
src/store/pelotonstore/peloton/parser/statements.h
```

The `pequinstore` and `sintrstore` copies are **byte-for-byte identical**, and both open with
`#pragma once` (line 13). When the preprocessor decides those two files are "the same file", it
includes the first and silently skips the second. Whichever store loses the race is left without
its parser statement types, and every use of them fails to compile.

Two distinct mechanisms collapse the two files into one identity, and the difference between
them is what makes this machine-dependent:

1. **Content-based collapse.** When two files are byte-identical, GCC's fallback heuristic — used
   when it cannot trust inode numbers — treats them as the same include and skips the second.
   This heuristic activates specifically on filesystems where inode identity is considered
   unreliable, i.e. network mounts.
2. **Inode-based collapse.** On some setups the two paths genuinely resolve to the same inode,
   via a hardlink, a symlink, or an overlay/dedup layer in the disk image. Here `#pragma once`
   is behaving correctly — it really is the same inode — but the result is the same.

The machine-dependence follows from where the tree physically lives and what filesystem backs it:

- On a plain local ext4 disk holding two distinct copies, the inodes differ and the content
  heuristic never fires, so both headers get included and the build succeeds. This is the
  `fsp-dev` case.
- On a fresh CloudLab experiment, before the tree has been moved to `/mydata`, it typically sits
  on the root image, an overlay, or an NFS-backed path (`/proj`, home). There either inode
  information is treated as unreliable (content heuristic collapses the identical files) or the
  image layer dedups them (same inode). Either way the second include is skipped and `make -j`
  fails.

> 📓 This is consistent with the observation that the failure appears on the *first* compilation
> after loading a new image, and does not recur afterwards.

### Fix

Break the byte-identity of the two files. Adding a single comment to one of them is enough:

```bash
# add any distinguishing comment near the top of one of the two files
src/store/sintrstore/query-engine/parser/statements.h
```

This is simpler than the alternative and is the recommended fix.

> :warning: You only need to do this **once**, the first time you compile on the control machine.

### Alternative fix

Forward-declaring the parser statement types in
`src/store/pequinstore/query-engine/parser/postgresparser.h` also resolves the build. See
[cloudlab-control-machine.md](cloudlab-control-machine.md) section 3 for the exact declaration
block.

---

## (2) Smallbank Tx-HS crashes on `local_config` <a name="localconfig"></a>

### Symptom

Smallbank runs on Tx-HS (HotStuff) fail on the control machine when the replica command is
assembled.

### Cause

`experiment-scripts/lib/indicus_codebase.py` reads the setting unconditionally when building the
replica command line:

```python
replica_command += ' --local_config=%s' % str(config['replication_protocol_settings']['local_config']).lower()
```

There is no default, so any config in the Basil/Indicus family that omits
`local_config` under `replication_protocol_settings` fails outright.

### Fix

Add the field to `replication_protocol_settings` in the affected configs:

```json
"local_config": "false",
```

This is already applied to the Smallbank Tx-HS configs under
`experiment-configs/Sintr/1-Workloads/Smallbank/Hotstuff/`.

> :warning: Fixed and tested on the CloudLab control machine. **Not** tested on `fsp-dev`.

---

## (3) `analyze_stats_file.py -b` produces empty plots <a name="emptyplots"></a>

### Symptom

Running the analysis with a benchmark selected produces a plot with axes but no data:

```bash
python3 experiment-scripts/analyze_stats_file.py -b "smallbank" -o <csv-dir> -p <plot-dir>
```

The generated CSV contains the runs, but the PDF is empty. Dropping `-b` produces a
correctly populated (but auto-scaled and differently coloured) plot.

### Cause

The `-b` flag does more than pick colours: it selects a fixed series ordering from
`BENCHMARK_PLOT_CONFIGS` in `experiment-scripts/analyze_stats_file.py`, and applies it by
casting the series column to an ordered categorical:

```python
df["experiment_name"] = pd.Categorical(df["experiment_name"], categories=order, ordered=True)
...
for series_idx, (experiment_name, group) in enumerate(df.groupby("experiment_name", observed=True)):
```

`experiment_name` comes from the `analysis_name` field of each run's config JSON. Any
`analysis_name` **not** present in that benchmark's `order` list becomes `NaN` under the
categorical cast, and `groupby(..., observed=True)` then drops it. If none of the names match,
every series is dropped and the plot comes out empty.

The `analysis_name` values in the configs did not match the labels used in the paper, so nothing survived the cast.

### Fix

Make each config's `analysis_name` match the label its benchmark expects. The accepted values are:

| `-b` value | Accepted `analysis_name` values |
|---|---|
| `tpcc` | `Pesto`, `Pesto-P-1`, `Pesto-P-2`, `Peloton-HS`, `Peloton-HS-P-1`, `Peloton-HS-P-2`, `Peloton-Smart`, `Peloton-Smart-P-1`, `Peloton-Smart-P-2` |
| `seats` | same as `tpcc` |
| `smallbank` | `Basil`, `Basil-P-1`, `Basil-P-2`, `Tx-HS`, `Tx-HS-P-1`, `Tx-HS-P-2`, `Tx-Smart`, `Tx-Smart-P-1`, `Tx-Smart-P-2` |
| `rw-sql` | no fixed ordering; any name is kept |

The configs under `experiment-configs/Sintr/1-Workloads/` have been updated accordingly and the
plots now render correctly.

> 📓 `P-x` denotes a transaction requiring `x` endorsements, so the baseline (`P-0`) is the bare
> system name. The script's default labels (e.g. `sintr-policy1`) do **not** match, which is why
> newly added configs need their `analysis_name` set explicitly.

> :warning: Names are matched exactly, including case and hyphens. A run whose `analysis_name`
> is missing entirely falls back to `f"{protocol}-{benchmark}"`, which will also be dropped.

---

## (4) Missing control-machine dependencies <a name="deps"></a>

The following are needed on the control machine and are not installed by the image.

Uploading benchmark data — `src/upload_data_remote.sh` drives its transfers with GNU parallel:

```bash
sudo apt update && sudo apt install parallel
```

Running `experiment-scripts/analyze_stats_file.py`:

```bash
pip install pandas
pip install matplotlib
```

Plot rendering uses LaTeX text:

```bash
sudo apt install texlive-latex-base texlive-fonts-recommended texlive-fonts-extra
```

> 📓 `texlive-fonts-extra` may not be necessary; it was installed together with the rest and not
> tested in isolation.

---

## (5) One-shot experiment setup <a name="oneshot"></a>

Setting up configs by hand is the most tedious part of the workflow: `update_configs.py` has to
be pointed at each config directory with the matching override file, benchmark data has to be
generated and uploaded per benchmark, and `experiment-results/original` has to be cleared between
runs. `experiment-scripts/run_all_benchmarks.sh` does all of it in a single invocation.

```bash
./experiment-scripts/run_all_benchmarks.sh -u <cloudlab-user> -e <experiment-name> -c <cluster> -o <archive-root>
```

It performs, in order:

1. **Update configs.** Runs `update_configs.py` over every config directory listed in the
   [RunningExperiments.md](RunningExperiments.md) override table, pairing each with its override
   file from `experiment-scripts/example_user_overrides/`.
2. **Generate benchmark data.** Runs `src/generate_benchmark_data.sh` for seats, tpcc, and
   tpcc-lifting.
3. **Upload benchmark data.** Runs `src/upload_data_remote.sh` per benchmark, forwarding the
   connection arguments given to the script.
4. **Run each experiment.** For every benchmark: clears `output/` and
   `experiment-results/original`, runs the configs, runs `collect_results.sh`, runs
   `analyze_stats_file.py` with that experiment's flags, then moves the raw output and collected
   stats into `<archive-root>/<timestamp>/<benchmark>/` and clears both directories for the next
   benchmark.

Both the workload experiments and all microbenchmarks are run by default. Useful options:

| Option | Meaning |
|---|---|
| `-b "<list>"` | Subset to run. Accepts individual names or the groups `all` (default), `workloads`, `micro` |
| `-t <count>` | Trials per config (default 1; `random-policy` defaults to 3 per the paper) |
| `-w <count>` | TPC-C warehouses (default 20) |
| `-S <factor>` | Seats scale factor (default 1) |
| `--dry-run` | Print every command instead of executing it |
| `--skip-configs` / `--skip-generate` / `--skip-upload` / `--skip-run` | Skip individual phases |
| `--rootdir` (on `collect_results.sh` / `run_many_experiment_configs.sh`) | Artifact root, if not the directory the scripts live in |

Run `./experiment-scripts/run_all_benchmarks.sh -h` for the full list.

> :warning: **HotStuff and BFT-SMaRt pre-configuration must be done beforehand.** The script does
> not run `pghs_config_remote.sh` or `bftsmart-configs/one_step_config.sh`. See
> [RunningExperiments.md](RunningExperiments.md) section 2.

> :warning: Uploading benchmark data **is** handled inside `run_all_benchmarks.sh` — do not run
> `upload_data_remote.sh` separately first.

> 📓 Start with `--dry-run` to confirm the resolved paths, and consider a single cheap benchmark
> (e.g. `-b vary-policy-z`) before committing to a full run. The complete default run is 151
> experiment invocations.

### Remaining rough edges

Full automation of config setup is limited by two properties of the configs themselves:

- `benchmark_schema_file_path` depends on **both** the CloudLab user and the baseline being run,
  so it cannot be set from a single global override. This is why there are separate
  `example_user_override-<benchmark>.json` files rather than one file.
- The configs inherit a large number of fields from Basil/Pesto that are irrelevant to Sintr,
  which makes it hard to tell which fields actually need changing.

Two benchmark families additionally have data that the artifact cannot provision:

- **Smallbank** data is pre-provisioned on the CloudLab image at `/usr/local/etc/smallbank_data`;
  there is no generator or uploader for it.
- **rw-sql** (all microbenchmarks except lifting) points `benchmark_schema_file_path` at
  `/users/<cloudlab-user>/rw-sql.json`, which likewise has no generator or uploader. The copy in
  the repo (`src/0_local_test_outputs/rw-sql/rw-sql.json`) is a 0-byte placeholder — the rw-sql
  table schemas are produced at run time as `rw-sql-gen-*-tables-schema.json` — so the file
  appears only to need to *exist* at that path. Confirm it is present before a microbenchmark run.

`run_all_benchmarks.sh` skips the data phase for both, but the runs will fail if those files are not already present on the machines.

---

## (6) Known remaining issues <a name="remaining"></a>

- **Instability at high load.** Runs at high client counts can still hit bugs in the program.
  We have not tracked down the cause.
- **Flaky CloudLab nodes.** Nodes occasionally hang or fail silently, producing throughput far
  below expectation or a missing `stats.json`. See the troubleshooting section at the end of
  [RunningExperiments.md](RunningExperiments.md) for how to identify and reboot the offending node.
- **BFT-SMaRt and Hotstuff at low load.** BFT-SMaRt and Hotstuff will fail to make progress at low load. Please run these two systems with at least 33 clients total.
