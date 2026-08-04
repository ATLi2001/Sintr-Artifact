#!/usr/bin/env python3
"""
Point every override file in experiment-scripts/example_user_overrides at your own
CloudLab account and local artifact checkout.

Those override files are the input to update_configs.py (see RunningExperiments.md
section 3); this script sets the handful of fields in them that are specific to you,
so you don't have to edit ten JSON files by hand.

    python3 experiment-scripts/update_user_overrides.py \
        <project_name> <experiment_name> <emulab_user> <artifact_parent_dir>

e.g.

    python3 experiment-scripts/update_user_overrides.py pequin-pg0 sintr atli /home/atli

The script is safe to re-run with different arguments. It rewrites each field from the
arguments you pass rather than by matching the values that happen to be there now, so
running it again just moves the fields to the new user/path -- it never resets anything
to a previous default, and it leaves every other field in the file untouched.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ARTIFACT_ROOT = SCRIPT_DIR.parent
DEFAULT_OVERRIDES_DIR = SCRIPT_DIR / "example_user_overrides"

# Set verbatim from the command line.
SCALAR_FIELDS = ("project_name", "experiment_name", "emulab_user")

# Local paths, rebuilt as "<artifact_root>/<tail>". The tail is fixed by the artifact
# layout, so these are reconstructed rather than pattern-matched -- that is what makes
# re-running with a new artifact directory work from any starting value.
# Note the trailing slash on gov_txn_config_path: update_configs.py treats a value with
# a suffix as a file and takes its parent, so the slash keeps it a directory.
LOCAL_PATH_TAILS = {
    "base_local_exp_directory": "output",
    "src_directory": "src",
    "sintr_policy_config_path": "src/0_local_test_outputs/configs",
    "gov_txn_config_path": "src/0_local_test_outputs/configs/",
}

# Remote paths all live under /users/<emulab_user>/. Whatever follows the username is
# preserved, so per-file values such as benchmark_data/sql-seats-tables-schema.json and
# rw-sql.json survive a change of user.
REMOTE_PREFIX = "/users/"


def rewrite_remote_path(value, emulab_user):
    """Replace the username component of a /users/<someone>/... path, keeping the tail."""
    if not isinstance(value, str) or not value.startswith(REMOTE_PREFIX):
        return value
    rest = value[len(REMOTE_PREFIX):]
    tail = rest.split("/", 1)[1] if "/" in rest else ""
    return REMOTE_PREFIX + emulab_user + ("/" + tail if tail else "")


def update_node(node, artifact_root, emulab_user, changes, prefix=""):
    """Recursively apply the updates to one JSON object, recording what changed."""
    for key, value in node.items():
        path = f"{prefix}{key}"

        if isinstance(value, dict):
            update_node(value, artifact_root, emulab_user, changes, prefix=f"{path}.")
            continue

        if key in LOCAL_PATH_TAILS:
            new_value = f"{artifact_root}/{LOCAL_PATH_TAILS[key]}"
        elif isinstance(value, str) and value.startswith(REMOTE_PREFIX):
            new_value = rewrite_remote_path(value, emulab_user)
        else:
            continue

        if new_value != value:
            changes.append((path, value, new_value))
            node[key] = new_value


def update_file(json_file, args, artifact_root, dry_run, make_backup, backup_root):
    try:
        with open(json_file, "r") as f:
            config = json.load(f)
    except json.JSONDecodeError as exc:
        print(f"  {json_file.name}: skipping (invalid JSON: {exc})")
        return False

    changes = []

    # Scalars are set verbatim, but only where the file already has the field -- these
    # override files are consumed by update_configs.py, so adding a key would silently
    # start overriding it in every experiment config.
    for field in SCALAR_FIELDS:
        if field not in config:
            continue
        new_value = getattr(args, field)
        if config[field] != new_value:
            changes.append((field, config[field], new_value))
            config[field] = new_value

    update_node(config, artifact_root, args.emulab_user, changes)

    if not changes:
        print(f"  {json_file.name}: already up to date")
        return False

    print(f"  {json_file.name}:")
    for field, old, new in changes:
        print(f"      {field}")
        print(f"        - {old}")
        print(f"        + {new}")

    if dry_run:
        return True

    if make_backup:
        backup_path = backup_root / json_file.name
        backup_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(json_file, backup_path)

    with open(json_file, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")

    return True


def resolve_artifact_root(artifact_parent_dir, artifact_name):
    """
    artifact_parent_dir is the folder *holding* the artifact directory, so the root is
    <artifact_parent_dir>/<artifact_name>. Passing the artifact directory itself is an
    easy mistake, so detect it and use it directly instead of nesting another level.
    """
    given = Path(artifact_parent_dir).expanduser()
    given_str = str(given).rstrip("/")

    looks_like_root = given.name == artifact_name or (
        (given / "experiment-scripts").is_dir() and (given / "src").is_dir()
    )
    if looks_like_root:
        print(f"Note: '{given_str}' looks like the artifact directory itself, not its parent.")
        print(f"      Using it as the artifact root.")
        return given_str

    return f"{given_str}/{artifact_name}"


def main():
    parser = argparse.ArgumentParser(
        description="Update every override JSON in example_user_overrides for your setup.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "example:\n"
            "  python3 experiment-scripts/update_user_overrides.py "
            "pequin-pg0 sintr atli /home/atli\n"
        ),
    )
    parser.add_argument("project_name", help='CloudLab project name, e.g. "pequin-pg0"')
    parser.add_argument("experiment_name", help='CloudLab experiment name, e.g. "sintr"')
    parser.add_argument("emulab_user", help='CloudLab username, e.g. "atli"')
    parser.add_argument(
        "artifact_parent_dir",
        help="Local folder holding the artifact directory, e.g. /home/atli or /mydata",
    )
    parser.add_argument(
        "--artifact-name",
        default=ARTIFACT_ROOT.name,
        help=f"Name of the artifact directory (default: {ARTIFACT_ROOT.name})",
    )
    parser.add_argument(
        "--overrides-dir",
        default=str(DEFAULT_OVERRIDES_DIR),
        help=f"Directory of override files (default: {DEFAULT_OVERRIDES_DIR})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would change without writing anything",
    )
    parser.add_argument(
        "--backup",
        action="store_true",
        help="Copy each modified file to a backup/ folder before writing",
    )
    args = parser.parse_args()

    overrides_dir = Path(args.overrides_dir).resolve()
    if not overrides_dir.is_dir():
        print(f"Error: {overrides_dir} is not a valid directory.", file=sys.stderr)
        sys.exit(1)

    json_files = sorted(overrides_dir.glob("*.json"))
    if not json_files:
        print(f"Error: no override files found in {overrides_dir}.", file=sys.stderr)
        sys.exit(1)

    artifact_root = resolve_artifact_root(args.artifact_parent_dir, args.artifact_name)

    print(f"project_name    : {args.project_name}")
    print(f"experiment_name : {args.experiment_name}")
    print(f"emulab_user     : {args.emulab_user}  (remote paths -> /users/{args.emulab_user}/...)")
    print(f"artifact root   : {artifact_root}")
    print(f"overrides dir   : {overrides_dir}")
    if args.dry_run:
        print("DRY RUN - no files will be written")
    print()

    backup_root = overrides_dir / "backup"
    if args.backup and not args.dry_run:
        backup_root.mkdir(exist_ok=True)

    updated = 0
    for json_file in json_files:
        if args.backup and backup_root in json_file.parents:
            continue
        if update_file(json_file, args, artifact_root, args.dry_run, args.backup, backup_root):
            updated += 1

    print()
    if args.dry_run:
        print(f"[OK] Dry run complete - {updated}/{len(json_files)} file(s) would change.")
    elif updated:
        print(f"[OK] Updated {updated}/{len(json_files)} file(s).")
        if args.backup:
            print(f"     Backups stored in: {backup_root}")
    else:
        print(f"[OK] All {len(json_files)} file(s) already up to date.")

    if not Path(artifact_root).is_dir():
        print(f"\nWarning: {artifact_root} does not exist on this machine.")
        print("         That is fine if you are configuring for a different machine.")


if __name__ == "__main__":
    main()
