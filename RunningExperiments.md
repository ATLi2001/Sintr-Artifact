# Running experiments <a name="experiments"></a>
Hurray! You have completed the tedious process of installing the binaries and setting up Cloudlab. 
Next, we will cover how to run experiments in order to re-produce all results. This is a straightforward but time-consuming process.

Ideally you have good network connectivity to quickly upload binaries to the remote machines and download experiment results. 
Uploading binaries on high speed connections (e.g at your university) takes a few minutes and needs to be done only once per instantiated Cloudlab experiment -- however, if your uplink speed is low it may take (as I have painstakingly experienced in preparing this documentation for you) several hours. Downloading experiment outputs requires a moderate amount of download bandwidth and is usually quite fast.

This section is split into 5 subsections: 
1. [Preparing Benchmarks](#prep)
2. [Pre-configurations for HotStuff, BFTSmart, and Postgres](#preconfig)
3. [Experiment script instructions](#scripts)
4. [Parsing outputs](#output)
5. [Reproducing our experiments 1-by-1](#exp)


Before you proceed, please confirm that your CloudLab credentials are accurate:
1. Cloudlab-username `<cloudlab-user>`: e.g. "fs435"
2. Cloudlab experiment name `<experiment-name>`: e.g. "pequin"
3. Cloudlab project name `<project-name`>: e.g. "pequin-pg0"  (May need the "-pg0" extension)

Confirm these by attempting to ssh into a machine you started (on the Utah cluster): `ssh <cloudlab-user>@us-east-1-0.<experiment-name>.<project-name>.utah.cloudlab.us`

## High level experiment checklist
Running experiments involves 5 steps. Refer back to this checklist to stay on track!

> :warning: Make sure to have set up a CloudLab experiment (with correct disk images matching your local/controllers package dependencies) and built all binaries locally before running!

1. The first step is to generate and upload initial data used by the benchmarks
2. Next, if you're running an SMR-based store (e.g. Peloton-HS or Peloton-Smart), you will need to pre-configure the SMR module. The exact procedure depends on the module you are using.
3. In order to run an experiment, you will need to write a configuration file (or copy and adjust our pre-supplied configs). This specifies the cluster setup, the benchmark to run, and the parameters of the system.
4. You're ready to run the experiment! Run the experiment script and supply it with your prepared config.
5. Finally, inspect the downloaded experiment run by checking the output data. 

## (1) Preparing Benchmarks <a name="prep"></a>

> :warning: Make sure that the names of your CloudLab machines match those in the helper scripts!

To generate benchmark data simple run the script `src/generate_benchmark_data.sh`. Configure it as follows:
1) specify the benchmark you want to generate, e.g. to run TPC-C use `-b 'tpcc'`
2) specify the benchmark parameters, e.g. to create 20 warehouses for TPC-C use `-n 20`

> 📓 You may need to enable permissions for the scripts before running: `cmod +x <scriptname>`

- Generate TPC-C data using: `./generate_benchmark_data -n 20` (tpcc is the default benchmark)
<!-- - Generate Auctionmark data using: `./generate_benchmark_data -b 'auctionmark'` (using default scale factor) -->
- Generate Seats data using: `./generate_benchmark_data -b 'seats'` (using default scale factor)

Once you created the benchmark data (you can create all data upfront), upload the respective benchmark data to your CloudLab cluster using `src/upload_data_remote.sh`.
Simply specify which benchmark you are uploading, and to how many clients per server you are uploading. 
You can also pass in your cloudlab user and experiment name.
- E.g. use `./upload_data_remote -b 'tpcc' -n 3 -u 'fs435'` to upload TPC-C data to 3 clients per replica, with cloudlab user `fs435`
- TPC-C and 1 client per server are default parameters. Check our the script for exact usage!

Note: Benchmark data, by default, is uploaded to `/users/<cloudlab-user>/benchmark_data/`. 

## (2) Pre-configurations for Hotstuff and BFTSmart <a name="preconfig"></a>

When evaluating Peloton-HS, Peloton-Smart, Tx-HS, or Tx-Smart you will need to complete the following pre-configuration steps before running an experiment script:

### **Hotstuff**
   1. Navigate to `Pequin-Artifact/src/scripts`
   2. [**OPTIONAL**] Run `./batch_size <batch_size>` to configure the internal batch size used by the HotStuff consensus module. See sub-section "1-by-1 experiment guide" for what settings to use. The default value is an *upper* cap of 200. Since we modified Hotstuff to use more efficient, dynamic batch sizes, changing the default batch cap is not necessary.
   3. Run `./pghs_config_remote.sh <cloudlab-user>` (e.g. `fs435`). This will upload the necessary configurations for the HotStuff Consensus module.

> :warning:  HotStuff is pre-configured to use the server names `us-east-1-0`, `us-east-1-1`, `us-east-1-2`, and `eu-west-1-0`. If you want to change the names of your servers you must also adjust the files `src/scripts/hosts_pg_smr` and `scr/scripts/config_pghs/shard0/hotstuff.gen.conf` accordingly.

   <!-- 3. Open file `config_remote.sh` and edit the following lines to match your Cloudlab credentials:
      - Line 3: `TARGET_DIR="/users/<cloudlab-user>/config/"`
      - Line 14: `rsync -rtuv config <cloudlab-user>@${machine}.<experiment-name>.<project-name>.utah.cloudlab.us:/users/<cloudlab-user>/`
   4. Finally, run `./config_remote.sh` 
   5. This will upload the necessary configurations for the Hotstuff Consensus module to the Cloudlab machines. -->

### **BFTSmart**
   1. Navigate to `Pequin-Artifact/src/scripts`
   2. Build BFT-Smart using `./build_bftsmart.sh`. You only need to do this *once*.
   3. Navigate to `Pequin-Artifact/src/scripts/bftsmart-configs` 
   4. Run `./one_step_config.sh <Local Pequin-Artifact directory> <cloudlab-user> <experiment-name> <project-name> <cluster-domain-name>`
   3. For example: `scripts/bftsmart-configs/one_step_config.sh ../../.. fs435 pequin pequin-pg0 utah.cloudlab.us`
   4. This will upload the necessary configurations for the BFTSmart Conesnsus module to the Cloudlab machines.
      - Troubleshooting: Make sure files `server-hosts` and `client-hosts` in `/src/scripts/bftsmart-configs/` do not contain empty lines at the end

> :warning: Do NOT use `src/scripts/one_step_config.sh` -- specifically use `src/scripts/bftsmart-configs/one_step_config.sh`. The scripts are identical, but for convenience reference different host file configurations.

   <!-- 2. Run `./one_step_config.sh <Local Pequin-Artifact directory> <cloudlab-user> <experiment-name> <project-name> <cluster-domain-name>`
   3. For example: `./one_step_config.sh /home/floriansuri/Research/Projects/Pequin/Pequin-Artifact fs435 pequin pequin-pg0 utah.cloudlab.us`
   4. This will upload the necessary configurations for the BFTSmart Conesnsus module to the Cloudlab machines.
      - Troubleshooting: Make sure files `server-hosts` and `client-hosts` in `/src/scripts/` do not contain empty lines at the end -->


## (3) Using the experiment scripts <a name="scripts"></a>

In this section we detail the steps for running an experiment in our artifact. 
For convenience, we have scripts which run multiple experiments consecutively and collect the results.
We list the exact scripts to run for reproducing each experiment in the final [section](#exp).

To run an experiment, you simply need to run: `python3 Pequin-Artifact/experiment-scripts/run_multiple_experiments.py <CONFIG>` using a specified configuration JSON file (see below). The script will load all binaries and configurations onto the remote Cloudlab machines, and collect experiment data upon completion. We have provided experiment configurations for all experiments claimed by the paper, which you can find under `Pequin-Artifact/experiment-configs`. In order for you to use them, you will need to make the following modifications to each file (Ctrl F and Replace in all the configs to save time):

 > **NOTE**: We've added a new option to directly update configuration parameters in all configs. This option has not been thoroughly vetted, so please sanity check that it is working correctly for yourself!!! `experiment-configs/Config-Override-Test` contains a script `update_configs.py` that allows users to specify the parameters they want to change (`user_override.json`). The usage is `python3 update_configs.py <path_to_configs> <override_file> [--dry-run] [--backup]`. In dry run mode no changes are made, and all files that would be changed are printed. In backup mode all existing files that would be changed are backed up to a folder `backup`.

#### Required Modifications:
1. `"project_name": "pequin-pg0"`
   - change the value field to the name of your Cloudlab project `<project-name>`. On cloudlab.us (utah cluster) you will generally need to add "-pg0" to your project_name in order to ssh into the machines. To confirm which is the case for you, try to ssh into a machine directly using `ssh <cloudlab-user>@us-east-1-0.<experiment-name>.<project-name>.utah.cloudlab.us`.  
2. `"experiment_name": "pequin"`
   - change the value field to the name of your Cloudlab experiment `<experiment-name>`.
3. `"base_local_exp_directory": “home/floriansuri/Research/Projects/Pequin/output”`
   - :warning: Some of our helper scripts assume that this directory is named `output` and directly under the root of the artifact.
   - Set the value field to be the local path (on your machine or the control machine) where experiment output files will be downloaded to and aggregated. 
4. `"base_remote_bin_directory_nfs": “users/<cloudlab-user>/indicus”` 
   - Set the field `<cloudlab-user>`. This is the directory on the Cloudlab machines where the binaries will be uploaded
5. `"src_directory" : “/home/floriansuri/Research/Projects/Pequin/Pequin-Artifact/src”` 
   - Set the value field to your local path (on your machine or the control machine) to the source directory 
6. `"emulab_user": "<cloudlab-username>"`
   - Set the field `<cloudlab-user>`. 
7. `"benchmark_schema_file_path": "/users/fs435/benchmark_data/sql-tpcc-tables-schema.json",`
    - change the user name (fs435) to your <cloudlab-username>
    - note: the file itself depends on which workload you are using
8. `"bftsmart_codebase_dir" : "/users/fs435"`
    - change the user name (fs435) to your <cloudlab-username>
    - this is only applicable to BFTSmart configs

#### **Optional** Modifications 
1. Experiment duration:
   - The provided configs are by default set to run for 60 seconds total, using a warmup and cooldown period of 15 seconds respectively. You may adjust the fields to shorten/lengthen experiments accordingly. For example:
      - "client_experiment_length": 30,
      - "client_ramp_down": 5,
      - "client_ramp_up": 5,
   - For cross-validation purposes shorter experiments likely suffice and save you time (and memory, since output files will be smaller).
   
2. Number of experiments:
   - The provided config files by default run the configured experiment once. If desired, experiments can instead be run several times, allowing us to report the mean throughput/latency as well as standard deviations across the runs. If you want to run the experiment multiple times, you can modify the config entry `num_experiment_runs: 1` to a repetition of your choice, which will automatically run the experiment the specified amount of times, and aggregate the joint statistics.

3. Number of clients:
   - The provided config files by default run a series of experiments for different number of clients. For simple cross-validation purposes this is probably not necessary, and you may want to shorten experiments to include only specific client points.
   - Client settings are defined by the following JSON entries. Each is an array, which allows you to specify a series of clients. Array sizes of the following three params need to match!
      - "client_total": [[30]],
         - "client_total" specifies the upper limit for total client *processes* used
      - "client_processes_per_client_node": [[8]],
         - "client_proccesses_per_client_node" specifies the number of client processes run on each server machine. 
      - "client_threads_per_process": [[1]],
         - "client_threads_per_process" specifies the number of client threads run by each client process.  
   - The *absolute total number* of clients used by an experiment is: 
    - **Total clients** *= max(client_total, num_servers x client_node_per_server x client_processes_per_client_node) *x client_threads_per_process*. 
    - For Pesto (1 shard) "num_servers" = 6, for Peloton (unreplicated) "num_servers" = 1, and for Peloton-SMR "num_servers" = 4.

   - An example client series:
      - "client_total": [[5, 10, 20, 30, 20]],
      - "client_processes_per_client_node": [[8, 8, 8, 8, 8]],
      - "client_threads_per_process": [[1, 1, 1, 1, 2]]

4. Server names:
   - The provided config files use default `server_names`. The name has no meaning in LAN deployments, and serves only as unique identifier (e.g. `us-east-1-0` does not imply where the server will be located). These server names must be consistent with the server names in your deployed CloudLab cluster.
   - If you change the default names, you must also adjust the `server_regions` and `region_rtt_latencies` parameters. Group server names into the region you want to assign them to. The `region_rtt_latencies` values do not matter for LAN deployments; they are placeholders for WAN simulation---see [WAN instructions](#wan-instructions).
  
#### Starting an experiment:
You are ready to start an experiment. The JSON configs we used can be found under `Pequin-Artifact/experiment-configs/<PATH>/<config>.json`. **Note that** all microbenchmark configs are Pesto (Pequin) exclusive.

Run: `python3 <PATH>/Pequin-Artifact/experiment-scripts/run_multiple_experiments.py <PATH>Pequin-Artifact/experiment-configs/<PATH>/<config>.json` and wait!

Optional: To monitor experiment progress you can ssh into a server machine (e.g., us-east-1-0) and run htop. During the experiment run-time the cpus will be loaded (to different degrees depending on contention and client count).
  
   
## (4) Parsing outputs <a name="output"></a>
After the experiment is complete, the scripts will generate an output folder at your specified `base_local_exp_directory`. Each folder is timestamped. 

To parse experiment results you have 2 options:
1. Looking at the `stats.json` file:
   1. Navigate into the timestamped folder, and keep following the timestamped folders until you enter folder `/out`. Open the file `stats.json`. When running multiple client settings, each setting will generate its own internal timestamped folder, with its own `stats.json` file. Multiple runs of the same experiment setting instead will directly be aggregated in a single `stats.json` file.
   2. In the `stats.json` file search for the Json field: `run_stats: ` 
   3. Then, search for the JSON field: `combined:`
   4. Finally, find Throughput measurements under `tput`, Latency measurements under `mean`, and Throughput per Correct client under `tput_s_honest` (**this will exist only for failure experiments**).
2. Looking at generated png plots:
   Alternatively, on your local machine you can navigate to `<time_stamped_folder>/plots/tput-clients.png` and `<time_stamped_folder>/plots/lat-tput.png` to look at the data points directly. Currently however, it shows as "Number of Clients" the number of total client **processes** (i.e. `client_total`) and not the number of **Total clients** specified above. Keep this in mind when viewing output that was generated for experiments with a list of client settings.

> :warning: The `stats.json` file contains aggregate throughput and latency statistics, as well as statistics for individual transaction types (e.g. `new-order` in TPC-C). Make sure that you are looking at the `combined` statistics as described above!!
   
 Find below, some example screenshots from looking at a provided experiment output from `Pequin-Artifact/sample-output/Pesto/1-Workloads/TPCC`:

 > **NOTE**: We've included a few sample results as illustrative examples. These are *not* the full experiment results. Please refer to section 5 *Running Experiments* to reproduce our results.

   Experiment output folder:

   ![image](https://github.com/user-attachments/assets/e8241b28-77af-42f6-886d-419c63e8e589)

   Contains the results (and configs) from a client series:

   ![image](https://github.com/user-attachments/assets/7e8aa53a-1399-44b5-a938-e31b15476edd)

   The plots folder contains some visualization.

   ![image](https://github.com/user-attachments/assets/89881415-cdf1-4073-8325-720b5e2b520a)

   Additionally, the plots folder contains csv files that automatically parse the Throughput and (mean) Latency for you

   ![image](https://github.com/user-attachments/assets/fa9ff226-6940-4af6-9e78-0f4f5d3f2bf0)


   For details on a specific experiment run, go to one of the experiment folders and inspect `stats.json`. To save space, we've removed all of the client/server logs shown in the picture (only `stats.json` remains)

   ![image](https://github.com/user-attachments/assets/75a526f4-ef72-4e07-be23-0ab8d8981c9b)

   Search for the JSON fields `run_stats` and `combined`. Note: `combined` might not be the first entry within `run_stats` in every config, so double check to get the right data.

   ![image](https://user-images.githubusercontent.com/42611410/129566877-87000119-c43b-4fa2-973a-2a9e571d9351.png)

   Throughput: 

   ![image](https://github.com/user-attachments/assets/7a4c841d-fded-4660-9ddb-0b71401233da)
   
   Latency: 

   ![image](https://github.com/user-attachments/assets/71878eec-8e34-4ded-b8b6-0b5aa98c6abb)


## (5) Reproducing experiment claims 1-by-1 <a name="exp"></a>

Next, we will go over each experiment individually to provide some pointers. All of our experiment configurations can be found under `experiment-configs`.

<!-- **TODO CHANGE ** 
We have included our experiment outputs for easy cross-validation of the claimed througput (and latency) numbers under `/sample-output/ValidatedResults`. 
To directly compare against the numbers reported in our paper please refer to the figures there or the supplied results -- we include rough numbers below as well.  -->

> :warning: Make sure to have set up a CloudLab experiment (with correct disk images matching your local/controllers package dependencies), built all binaries, and created benchmark data before running (see instructions above).

> :warning: Make sure you have correctly set the  `"benchmark_schema_file_path"` as described above!

> **Notice**: When running experiments with load load (i.e. few clients) we observe that the average latency is typically higher than at moderate load (this is the case for all systems). This appears to be a protocol-independent system artifact that we have been unable to resolve so far. CPU and/or network speeds seem to increase under load.

<!-- > **Notice**: Some of the systems have matured since the reported results (e.g. undergone minor bugfixes or experienced miscellaneous changes to debug logging). This should have very little impact on performance, but we acknowledge it nonetheless for completeness. The main claims remain consistent. -->

**Helper Experiment Scripts:** The following helper experiment scripts should help in automating many of the evaluation runs. 
You will need to modify the listed constants for each script.
- `experiment-scripts/run_many_experiment_configs.sh`
    - `ROOTDIR`: path to artifact directory
- `experiment-scripts/collect_results.sh`
    - `ROOTDIR`: path to artifact directory
    - `OUTPUT_DIR`: this should match `base_local_exp_directory` (see above); if you kept `base_local_exp_directory` named `output` under the path to the artifact directory, then no changes are necessary

**Before each run:** clear `experiment-results/original`. 
Results from `experiment-scripts/collect_results.sh` land there and are not overwritten between runs.

The root `output` directory is configured by default to store the complete raw experiment output. You should also clear or move its contents before running `collect_results.sh` after each experiment.

### **1 - Workloads**:
We report evaluation results for 3 workloads (TPCC, Seats, and Smallbank) over 4 baseline systems with Sintr integrated: 
1. **Basil** -- A BFT KVS.
2. **Pesto** -- A BFT DB.
3. **Peloton-SMR** -- Peloton, an SQL DB, (with reply signatures) run atop a BFT consensus protocol; we run with HotStuff (Peloton-HS) and BFTSmart (Peloton-Smart).
4. **Tx-SMR** -- A transactional KVS (with reply signatures) run atop a BFT consensus protocol; we run with HotStuff (Tx-HS) and BFTSmart (Tx-Smart).

Detailed explanations of the Pesto and Peloton-SMR baselines can be found in the [Pesto artifact](https://github.com/fsuri/Pequin-Artifact).
Simimlarly, detailed explanations of the Basil and Tx-SMR baselines can be found in the [Basil artifact](https://github.com/fsuri/Basil_SOSP21_artifact/).

All systems were evaluated using a single shard, but use different replication factors. For f=1, Basil/Pesto uses 6 replicas (5f+1), while the SMR based systems use 4 (3f+1).

<!-- All systems using signatures (Pesto, Pesto-unreplicated, Peloton-signed, Peloton-HS, Peloton-Smart) are augmented to make use of the reply batching scheme proposed in Basil: replicas may batch together replies to clients and create a single signature to amortize costs. We defer exact details to Basil. We use a varying reply batch size depending on the load; for low load, it is better to not batch to avoid incurring a batch timeout.  -->
<!-- we used very small batch timer by accident because the unit is *microseconds* and not *miliseconds*. However, due to a libevent artifact, timer granularity is only 4ms, so most of the time our timers are implicitly 4ms. -->

<!-- Peak throughput reported in the paper corresponds to maximum attained throughput; latency reported corresponds to latency measured at the "ankle" point, i.e. a bit before latency starts to spike. -->

> :warning: The `stats.json` file contains aggregate throughput and latency statistics, as well as statistics for individual transaction types (e.g. `new-order` in TPC-C). Make sure that you are looking at the `combined` statistics as described in section [Parsing Outputs](#output)!!

 
#### 1. **TPCC**:  
We evaluate Pesto and the Peloton-SMR systems.
We denote by `P-x` that a transaction requires `x` endorsements, excluding the initiating client. 
Baselines correspond to `P-0`. 
Initiating clients select validation clients uniformly in a round-robin manner.

```bash
./experiment-scripts/run_many_experiment_configs.sh experiment-configs/Sintr/1-Workloads/TPCC-SQL/ --recursive
```

#### 2. **Seats**:
    
