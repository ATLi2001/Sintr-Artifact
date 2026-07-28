# Sintr Artifact — CloudLab Setup & Build Notes

Notes for getting the Sintr artifact to build and run on CloudLab with a control machine.
Covers the disk/space setup on the control machine, the source change needed to compile, the SSH `ControlPath` fix for the experiment scripts, and the experiment configuration that was run.

**Cluster used:** 6 client machines + 6 server machines + 1 control machine =
**13 machines total.**

---

## 1. Disk setup on the control machine

The artifact was built and run from `/mydata/Sintr-Artifact/`, **not** `/mnt`.

On this CloudLab node the OS boots on a small 16 GB root partition
(`/dev/nvme0n1p1`, mounted at `/`), and `/mnt` lives on that same root disk — it is *not* a separate volume. A full Peloton/Pequin build overflows 16 GB, which shows up as `No space left on device` errors while writing `.o`/`.s` files.

The bulk of the disk was sitting unused in an unmounted partition `/dev/nvme0n1p4` (~216 GB). Mount it and build there instead.

### Check what's available

```bash
df -h                       # confirm / is full and there's no separate /mnt disk
lsblk                       # look for a large unmounted partition (nvme0n1p4)
sudo blkid /dev/nvme0n1p4   # does it already have a filesystem?
```

### Mount the big partition

If `blkid` reports an existing `TYPE=` (e.g. `ext4`), just mount it — do **not**
reformat, or you'll wipe it:

```bash
sudo mkdir -p /mydata
sudo mount /dev/nvme0n1p4 /mydata
```

If `blkid` shows nothing (empty partition), format first, then mount:

```bash
sudo mkfs.ext4 /dev/nvme0n1p4
sudo mkdir -p /mydata
sudo mount /dev/nvme0n1p4 /mydata
df -h /mydata               # should show ~213 GB available
```

### Make the mount survive reboots

```bash
echo "UUID=$(sudo blkid -s UUID -o value /dev/nvme0n1p4) /mydata ext4 defaults 0 2" \
  | sudo tee -a /etc/fstab
```

### Clone the artifact onto the big disk

Clone the artifact fresh into `/mydata` (don't move the copy from the full root
disk — clone a clean tree):

```bash
cd /mydata
git clone https://github.com/ATLi2001/Sintr-Artifact.git Sintr-Artifact
sudo chown -R $(whoami):$(id -gn) /mydata/Sintr-Artifact
```

If root is already full and `git clone` or later steps complain about space,
free a little first:

```bash
sudo apt-get clean
# and remove any partial build tree left on the root disk, e.g.:
# rm -rf /mnt/Sintr-Artifact/src/.obj
```

### Point temp files at the big disk too

The compiler writes large intermediate `.s` files to `$TMPDIR` (default `/tmp`,
which is on the full root disk). Redirect it:

```bash
export TMPDIR=/mydata/tmp && mkdir -p "$TMPDIR"
```

> Note: if building with `sudo`, `TMPDIR` is stripped by sudo's `env_reset`.
> Either build as your normal user (after the `chown` above) or pass it through
> explicitly: `sudo TMPDIR=/mydata/tmp make -j$(nproc)`.

**Caveat:** `/mydata` is local to the CloudLab node — it survives reboots but is **wiped when the experiment is terminated**. Keep a source-of-truth copy on the persistent project share (`/proj/pequin-PG0/`, NFS) for anything you can't lose.

---

## 2. Getting to the artifact

```bash
cd /mydata/Sintr-Artifact/
# source lives under src/
cd src
```

Build:

```bash
export TMPDIR=/mydata/tmp && mkdir -p "$TMPDIR"
make -j$(nproc)
```

---

## 3. Source change to compile on the control machine

`postgresparser.h` failed to compile with:

```
error: 'SQLStatementList' in namespace 'peloton::parser' does not name a type;
did you mean 'SQLStatement'?
```

This was fixed by **forward-declaring the parser statement types** in
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

// in parser statement
```

---

## 4. SSH `ControlPath` fix for the experiment scripts

The provided link (modifying the *system* SSH configuration) pointed in the right
direction but did **not** resolve the issue on its own. The artifact's experiment
scripts specify SSH options explicitly, which override the system config.

**File:** `experiment-scripts/utils/remote_util.py`

Original options:

```python
'-o', 'ControlMaster=auto',
'-o', 'ControlPersist=2m',
'-o', 'ControlPath=~/.ssh/cm-%r@%h:%p',
```

Changed `ControlPath` to use the hashed connection identifier (`%C`) under
`/tmp` instead:

```python
'-o', 'ControlMaster=auto',
'-o', 'ControlPersist=2m',
'-o', 'ControlPath=/tmp/sintr-ssh-%C',
```

This resolved the SSH multiplexing problem.

## 6. Experiment configuration run

**Config file:**

```
experiment-configs/Sintr/2-Microbenchmarks/Sintr-RW-SQL-Uniform-base.json
```

**Overrides applied:**

- `client_total` set to **10**
- `client_nodes_per_server` set to **1**
- plus other required user overrides (paths, project name, etc.)
