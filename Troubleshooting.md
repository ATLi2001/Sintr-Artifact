# Troubleshooting

## Manual Installation

This section covers issues with manual installation.
For full instructions, see [Installation](Installation.md).

### Problems with locating libraries:
   
1. You may need to export your `LD_LIBRARY_PATH` if your installations are in non-standard locations:
   The default install locations are:

   <!--- Hoard: usr/local/lib -->
   - Jemalloc: usr/local/lib
   - TaoPq:  /usr/local/lib
   - Nlohmann/JSON:  /usr/local/include
   - Secp256k1:  /usr/local/lib
   - CryptoPP: /usr/local/include  /usr/local/bin   /usr/local/share
   - Blake3: /usr/local/lib
   - Donna: /usr/local/lib
   - Googletest: /usr/local/lib /usr/local/include
   - Protobufs: /usr/local/lib
   - Intel TBB: /opt/intel/oneapi
   - CockroachDB: /usr/local/lib/cockroach  /usr/local/bin/cockroach

 Run `export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/share:/usr/local/include:$LD_LIBRARY_PATH` (adjusted depending on where `make install` puts the libraries) followed by `sudo ldconfig`.
   
2. If you installed more Intel API tools besides "Intel oneAPI Threading Building Blocks", then the Intel oneAPI installation might have installed a different protobuf binary. Since the application pre-pends the Intel install locations to `PATH`, you may need to manually pre-pend the original directories. Run: `export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH`

3. Building googletest differently:
   
   If you get an error: `make: *** No rule to make target '.obj/gtest/gtest-all.o', needed by '.obj/gtest/gtest_main.a'.  Stop.` try to install googletest directly into the `src` directory as follows:
   1. `git clone https://github.com/google/googletest.git`
   2. `cd googletest`
   3. `git checkout release-1.10.0`
   4. `rm -rf <Relative-Path>/Sintr-Artifact/src/.obj/gtest`
   5. `mkdir <Relative-Path>/Sintr-Artifact/src/.obj`
   6. `cp -r googletest <Relative-Path>/Sintr-Artifact/src/.obj/gtest`
   7. `cd <Relative-Path>/Sintr-Artifact/src/.obj/gtest`
   8. `cmake CMakeLists.txt`
   9. `make -j $(nproc)`
   10. `g++ -isystem ./include -I . -pthread -c ./src/gtest-all.cc`
   11. `g++ -isystem ./include -I . -pthread -c ./src/gtest_main.cc`

## CloudLab Control Machine Compilation

This section covers issues you may run into with getting Sintr to compile on a CloudLab control machine.
For full instructions, see our [CloudLab Setup](CloudlabSetup.md).

### Disk Space

Control machines may be low on disk space in the default home directory, and have insufficient space to clone the artifact. If this should become a problem, try to clone the artifact into a directory such as `dev` or `mnt`.

If the CloudLab node OS boots on a small 16 GB root partition
(`/dev/nvme0n1p1`, mounted at `/`), and `/mnt` lives on that same root disk — it is *not* a separate volume. 
A full build overflows 16 GB, which shows up as `No space left on device` errors while writing `.o`/`.s` files.

The bulk of the disk was sitting unused in an unmounted partition `/dev/nvme0n1p4` (~216 GB). Mount it and build there instead.

```bash
df -h                       # confirm / is full and there's no separate /mnt disk
lsblk                       # look for a large unmounted partition (nvme0n1p4)
sudo blkid /dev/nvme0n1p4   # does it already have a filesystem?
```

If `blkid` reports an existing `TYPE=` (e.g. `ext4`), just mount it — do **not**
reformat, or you'll wipe it:

```bash
sudo mkdir -p /mydata
sudo mount /dev/nvme0n1p4 /mydata
```

Otherwise (empty partition), format first, then mount:

```bash
sudo mkfs.ext4 /dev/nvme0n1p4
sudo mkdir -p /mydata
sudo mount /dev/nvme0n1p4 /mydata
df -h /mydata               # should show ~213 GB available
```

Next, make the mount survive reboots.

```bash
echo "UUID=$(sudo blkid -s UUID -o value /dev/nvme0n1p4) /mydata ext4 defaults 0 2" \
  | sudo tee -a /etc/fstab
```

You can then clone the artifact into `/mydata/` (don't move the copy from the full root disk — clone a clean tree):

```bash
sudo chown -R $(whoami):$(id -gn) /mydata/
cd /mydata
git clone https://github.com/ATLi2001/Sintr-Artifact.git Sintr-Artifact
```

If root is already full and `git clone` or later steps complain about space,
free a little first:

```bash
sudo apt-get clean
# and remove any partial build tree left on the root disk, e.g.:
# rm -rf /mnt/Sintr-Artifact/src/.obj
```

### Tmp File Space

You may see that compilation complains about not having enough space for tmp files. 
If this is the case, change the tmp directory before compiling.
```bash
export TMPDIR=/mydata/tmp && mkdir -p "$TMPDIR"
make -j $(nproc)
```

### Compilation issues

We have seen that the order of source compilation can vary depending on the machine.
If you see `postgresparser.h` failed to compile with:

```
error: 'SQLStatementList' in namespace 'peloton::parser' does not name a type;
did you mean 'SQLStatement'?
```

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
  heuristic never fires, so both headers get included and the build succeeds.
- On a fresh CloudLab experiment, before the tree has been moved to `/mydata`, it typically sits
  on the root image, an overlay, or an NFS-backed path (`/proj`, home). There either inode
  information is treated as unreliable (content heuristic collapses the identical files) or the
  image layer dedups them (same inode). Either way the second include is skipped and `make -j`
  fails.

#### Recommended Fix

Break the byte-identity of the two files. Adding a single comment to one of them is enough:

```bash
# add any distinguishing comment near the top of one of the two files
src/store/sintrstore/query-engine/parser/statements.h
```

> :warning: You only need to do this **once**, the first time you compile on the control machine.

#### Alternative Fix

**Forward-declaring the parser statement types** in
`src/store/pequinstore/query-engine/parser/postgresparser.h` (in the
`peloton::parser` namespace, before their first use):

```cpp
class AnalyzeStatement;
class CopyStatement;
class CreateStatement;
class CreateFunctionStatement;
class DeleteStatement;
class DropStatement;
class ExecuteStatement;
class FuncParameter;
class GroupByDescription;
class JoinDefinition;
class OrderDescription;
class PrepareStatement;
class ReturnType;
class SelectStatement;
class SQLStatementList;
struct TableRef;
class TransactionStatement;
class UpdateClause;
class UpdateStatement;
class VariableSetStatement;

// class PostgresParser { ...
```

## CloudLab Control Machine Running Experiments 

This section covers issues you may run into with running experiments from a CloudLab control machine.
For full instructions, see [Running Experiments](RunningExperiments.md).

### SSH `ControlPath` Too Long

When running `run_multiple_experiments.py` from the control machine, depending on your CloudLab username and group name, you may see the following warning.

```
unix_listener: path "/users/<my-account>/.ssh/cm-<my-account>@us-east-1-0.<my-labname>.<my-group>.utah.cloudlab.us:22.XXXX" too long for Unix domain socket
```

This can be resolved by changing `experiment-scripts/utils/remote_util.py`.

Original options in `ssh_args()` function:

```python
'-o', 'ControlMaster=auto',
'-o', 'ControlPersist=2m',
'-o', 'ControlPath=~/.ssh/cm-%r@%h:%p',
```

Changed `ControlPath` to use the hashed connection identifier (`%C`) under `/tmp` instead:

```python
'-o', 'ControlMaster=auto',
'-o', 'ControlPersist=2m',
'-o', 'ControlPath=/tmp/sintr-ssh-%C',
```

### Analysis Script Missing Dependencies

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

## CloudLab Experiment Nodes

This section covers issues you may run into with the nodes running the experiment (not the control machine).
For full instructions, see [Running Experiments](RunningExperiments.md).

### Experiment Start Hangs

If you notice the experiment is not starting for a long time, stop the current run and perform the following steps to identify which node is the problem.

1. In `experiment-scripts/utils/experiment_util.py`, modify the function `copy_binaries_to_nfs()` to wait on uploading to each set of replica and its clients. 
That is, at the end of the for loop on line 559, add in `concurrent.futures.wait(futures)`.
2. Rerun an experiment. 
You will notice that the script copies binaries to the CloudLab nodes and waits after each replica, rather than doing all in parallel. 
At some point, one of these will hang.
3. You can then check the hanging replica and its clients individually by attempting to ssh into them.
Either you will be unable to ssh into it, or the node will not appear to have bash as its shell (we have seen both happen).
Reboot the node that has the problem from the CloudLab web interface. 

### Output Numbers Too Low

Other times, if a node fails to initialize during an experiment, you may notice the numbers are drastically lower than expected.
You can then go and check the logs for the run (located in the timestamped output folder).
You may notice that a particular node does not have a `stats.json` output.
This node likely experienced an issue and may need to be rebooted.

## Known Remaining Issues

- **Instability at high load.** Runs at high client counts can still hit bugs in the program, usually indicated by a segfault or a `Panic` statement triggering within the logs.
  We have not tracked down the cause.
- **BFT-SMaRt and Hotstuff at low load.** BFT-SMaRt and Hotstuff will fail to make progress at low load. Please run these two systems with at least 33 clients total.

