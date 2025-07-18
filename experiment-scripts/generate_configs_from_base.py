"""
 Copyright 2024 Austin Li <atl63@cornell.edu>

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use, copy,
 modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
"""

import json
import os
import argparse


# base_config_json is a dictionary loaded from a JSON file
# changes_dict is a dictionary from config keys to new values
def create_config_from_base(base_config_json, changes_dict):
    new_config = base_config_json.copy()
    for key, value in changes_dict.items():
        if key.startswith("_note:"):
            # Skip notes or comments in the changes dictionary
            continue
        if key in new_config:
            # if trying to replace some elements of a nested dictionary
            if isinstance(new_config[key], dict) and isinstance(value, dict):
                new_config[key] = create_config_from_base(new_config[key], value)
            else:
                new_config[key] = value
        else:
            print(f"Warning: Key '{key}' not found in base configuration. Skipping.")
    return new_config


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create multiple experiment configurations from base.")
    parser.add_argument(
        "-b", "--base_config",
        type=str,
        required=True,
        help="Path to the base configuration file (JSON format)"
    )
    parser.add_argument(
        "-o", "--output_dir",
        type=str,
        required=True,
        help=f"Where to write the output files"
    )
    parser.add_argument(
        "-c", "--changes",
        type=str,
        required=True,
        help="Path to the changes file that specifies the changes to apply to the base configuration"
    )

    args = parser.parse_args()

    base_config_path = args.base_config
    output_dir = args.output_dir
    changes_path = args.changes
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    with open(base_config_path, "r") as f:
        base_config_json = json.load(f)

    # changes file is a json file 
    # top level is change suffix to a dictionary of changes
    with open(changes_path, "r") as f:
        changes_dict = json.load(f)
        for suffix, changes in changes_dict.items():
            if suffix.startswith("_note:"):
                # Skip notes or comments in the changes dictionary
                continue
            new_config = create_config_from_base(base_config_json, changes)

            # write the new configuration to a file
            base_filename = os.path.basename(base_config_path)
            config_filename = f"{base_filename[:-5]}_{suffix}.json"
            out_path = os.path.join(output_dir, config_filename)
            with open(out_path, "w") as config_file:
                json.dump(new_config, config_file, indent=2)

            print(f"Generated configuration: {out_path}")
