# Sintr Artifact
This is the code artifact for the paper: "Sintr: Safe Interactive Transactions in the Presence of Byzantine Clients". (SOSP'26 paper 216)

For all questions about the artifact please e-mail Austin Li <atl63@cornell.edu>. For specific questions about the following topics please additionally CC:
1) HotStuff/BFT-SMaRt related baselines, experimental results: Daniel Lee <dhl93@cornell.edu>


# Table of Contents
1. [High Level Summary](#summary)
2. [Artifact Overview](#artifact-overview)
3. [Validating Paper Claims](#validating)
4. [Installing Dependencies and Building Binaries](Installation.md)
5. [Setting up CloudLab](CloudlabSetup.md)
6. [Running Experiments](RunningExperiments.md)
7. [Troubleshooting](Troubleshooting.md)


# Summary 
This artifact contains and enables the reproduction of all experiments corresponding to the figures in the paper "Sintr: Safe Interactive Transactions in the Presence of Byzantine Clients". 

It contains a prototype implementation of Sintr, a framework for preventing Byzantine clients from compromising database integrity by executing rogue transactions. 
Sintr can be used to augment any BFT database system supporting interactive (client-executed) transactions.
We implement it atop several baseline BFT systems: Basil, Pesto, and SMR-based transactional systems using HotStuff and BFT-SMaRt. 

# Artifact Overview

## Systems

This repository includes prototypes for Basil, Pesto, Peloton-HS, Peloton-Smart, Tx-HS, and Tx-Smart.
We implement Sintr atop each of these prototypes.
There are also several other prototypes which are not used for the evaluation of Sintr: Peloton, Postgres, CockroachDB, Tapir.

**Basil** is a sharded BFT key-value store (KVS) with interactive transactions.

**Pesto** is a BFT SQL database that extends Basil with Peloton's query engine.
Pesto supports a KVS-path that implements Basil's protocol.
We use the Pesto prototype (referred to as `pequin` in the codebase) for evaluating both Basil and Pesto.
For additional details, please refer to the original [Pesto artifact](https://github.com/fsuri/Pequin-Artifact).

**Peloton-SMR** has replicas execute SQL queries using Peloton after ordering them via BFT SMR.
[Peloton](https://github.com/cmu-db/peloton) is a fully fledged SQL database based on [Postgres](https://www.postgresql.org/).
We take the Peloton-SMR implementation from the open source Pesto artifact, which contains two versions: Peloton-HS which uses [HotStuff](https://github.com/hot-stuff/libhotstuff) for consensus; Peloton-Smart instead relies on [BFT-SMaRt](https://github.com/bft-smart/library).

**Tx-SMR** is a transactional KVS system layered over BFT SMR. 
We take the Tx-SMR implementation from the open source Basil artifact, which also includes two versions: Tx-HS uses HotStuff as its consensus engine, while Tx-Smart relies on BFT-SMaRt. 

## Benchmarks:
We use the Pesto artifact implementation of four benchmarks:

[**TPC-C**](https://tpc.org/tpc_documents_current_versions/pdf/tpc-c_v5.11.0.pdf) simulates the business of an online e-commerce application. It consists of 5 transaction types, allowing clients to issue new orders and payments, fulfill deliveries, and query current order status and item stocks.
TPC-C exhibits high contention as most transactions read and write to the Warehouse table. 
We configure it to use 20 warehouses, and instantiate indexes to retrieve orders by customer, as well as customers by their last name.

[**Seats**](https://github.com/cmu-db/benchbase/wiki/Seats) simulates an airline booking system. It consists of 6 transaction types, allowing clients to search for flights and open seats, create, update and delete reservations, as well as update customer information.
Access is distributed uniformly across customers and flights. The workload is characterized by a high fraction of range queries and cross-table joins, but exhibits overall low contention relative to TPC-C. 

[**Smallbank**](https://github.com/cmu-db/benchbase/wiki/SmallBank) simulates a simple KVS banking application.
It consists of 5 transaction types, allowing clients to move money between accounts and check their balances.
We configure it to run with 1M accounts, with a 90% skew to 1,000 hot keys.

**YCSB-Tables**. This is a custom read-modify-write microbenchmark based on [YCSB](https://github.com/brianfrankcooper/YCSB). The database can be instantiated with a configurable number of tables and rows; each row contains a key-value pair. Keys are unique (primary key), while values can either be random or fall within a configurable number of candidate categories.
Transactions read and/or write to a configurable number of rows; reads may optionally be conditioned on a secondary condition (e.g. value category). The access pattern to both tables and rows within tables is configurable: it may be uniformly random, or follow a YCSB-based Zipfian (coefficient configurable).


## Artifact Organization <a name="artifact"></a>

The core prototype logic of each system is located in the following folders: 
1. `src/store/pequinstore/`: Contains the source code implementing the Pesto prototype (Pequin).
The Pesto prototype supports a KVS-path that implements the Basil protocol. 
We use this source code to evaluate both Pesto and Basil baselines.
   - `src/store/sintrstore/`: Contains the source code implementing Sintr atop Basil/Pesto. 
   This code uses the `pequinstore` code as a starting point.
2. `src/store/pelotonstore/`: Contains the source code implementing the Peloton-SMR prototypes. Can be configured via `SMR_mode` flag to run unreplicated, with HotStuff, or with BFT-SMaRt.
The underlying HotStuff and BFT-SMaRt modules are found in
`src/store/hotstuffstore/libhotstuff/` and `src/store/bftsmartstore/library/` respectively.
   - The directory also contains code implementing Sintr atop Peloton-SMR. 
   Sintr-specific code will only run when the policy requires it.
3. `src/store/hotstuffstore/`: Contains the source code implementing the Tx-HS prototype.
   - The directory also contains code implementing Sintr atop Tx-HS.
   Sintr-specific code will only run when the policy requires it.
4. `src/store/bftsmartstore/`: Contains the source code implementing the Tx-Smart prototype.
   - The directory also contains code implementing Sintr atop Tx-Smart.
   Sintr-specific code will only run when the policy requires it.

Benchmarks are located under `src/store/benchmark/async/`. 
For each benchmark, there is a `sync/` and `validation/` subdirectory.
The `sync/` subdirectory contains wrappers that call the benchmark code as an initiating client would, while the `validation/` subdirectory contains wrappers that call the benchmark code as a validation client would (i.e., passing in fixed transaction inputs rather than allowing the code to generate new ones).

1. TPC-C: `src/store/benchmark/async/sql/tpcc/`
2. Seats: `src/store/benchmark/async/sql/seats/`
3. Smallbank: `src/store/benchmark/async/smallbank/`
4. YCSB: `src/store/benchmark/async/rw-sql/`
5. TPC-C with lifting: `src/store/benchmark/async/sql/tpcc-lifting/`

Networking and cryptography functionality is found in `src/lib/`.

The experiment scripts to run all prototypes on CloudLab are found in `experiment-scripts/`.
`src/` and `src/scripts/` contain additional helper scripts used to create/upload benchmark data, and pre-configure HotStuff/BFT-SMaRt.
Finally, `experiment-configs/` contains the configs we used in our experiments.


# Validating Paper Claims <a name="validating"></a>

## Concrete claims

- **Main claim**: Sintr imposes modest overheads on the BFT database systems it is applied to: 3%-16% in throughput and 3%-21% in latency.

   All comparisons for the main claim are made in the absence of failures.

- **Supplementary**: All other microbenchmarks reported realistically represent Sintr.

## Validation Overview

All our experiments were run using CloudLab (https://www.cloudlab.us/), specifically the CloudLab Utah cluster. To reproduce our results and validate our claims, you will need to 1) instantiate a matching CloudLab experiment, 2) build the prototype binaries, and 3) run the provided experiment scripts with the (supplied) configs we used to generate our results. 

You may go about 2) and 3) in two ways: You can either build and control the experiments from a local machine (easier to parse/record results & troubleshoot, but more initial installs necessary); or, you can build and control the experiments from a dedicated CloudLab control machine, using pre-supplied disk images (faster setup out of the box, but more overhead to parse/record results and troubleshoot). Both options are outlined in this ReadMe.

The ReadMe is organized into the following high level sections. Refer to each link for detailed documentation:

1. [*Installing pre-requisites and building binaries*](Installation.md)

   To build Sintr and baseline source code several dependencies must be installed. Refer to section "Installing Dependencies" for detailed instructions on how to install dependencies and compile the code. You may skip this step if you choose to use a dedicated CloudLab "control" machine using *our* supplied fully configured disk images. Note that, if you choose to use a control machine but not use our images, you will have to follow the Installation guide too, and additionally create your own disk images. More on disk images can be found in section "Setting up CloudLab".
  

2. [*Setting up experiments on CloudLab* ](CloudlabSetup.md)

   To re-run our experiments, you will need to instantiate a server (and client) cluster using CloudLab. We have provided a public profile as well as public disk images that capture the configurations we used to produce our results. Section "Setting up CloudLab" covers the necessary steps in detail. Alternatively, you may create a profile of your own and generate disk images from scratch (more work) - refer to section "Setting up CloudLab" as well for more information. Note, that you will need to use the same Cluster (Utah) and machine types (m510) to reproduce our results.


3. [*Running experiments*](RunningExperiments.md)

   To reproduce our experiments you will need to build the code, and run the supplied experiment scripts using the supplied experiment configurations. Section "Running Experiments" includes instructions for using the experiment scripts, modifying the configurations, and parsing the output. HotStuff and BFT-SMaRt baselines require additional configuration steps, also detailed in section "Running Experiments".


4. [*Troubleshooting*](Troubleshooting.md)

   If you run into any issues throughout the process, please refer to our troubleshooting section.
