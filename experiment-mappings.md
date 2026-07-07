# Sintr Experiment → Config & Result Path Mapping

This document maps each experiment in the paper to its configuration directory
and the corresponding result artifacts (graph PDF and CSV).

## 1. Workloads

| Experiment | Config Path | Result Graph (PDF) | Result CSV |
|---|---|---|---|
| TPC-C | `experiment-configs/Sintr/1-Workloads/TPCC-SQL` | `experiment-results/1-Workloads/TPCC-SQL/Combined/tpcc-sql-combined.pdf` | `experiment-results/1-Workloads/TPCC-SQL/Combined/tpcc-sql-combined.csv` |
| Smallbank | `experiment-configs/Sintr/1-Workloads/Smallbank` | `experiment-results/1-Workloads/Smallbank/Combined/smallbank-combined.pdf` | `experiment-results/1-Workloads/Smallbank/Combined/smallbank-combined.csv` |
| SEATS | `experiment-configs/Sintr/1-Workloads/Seats` | `experiment-results/1-Workloads/Seats/Combined/seats-combined.pdf` | `experiment-results/1-Workloads/Seats/Combined/seats-combined.csv` |

## 2. Microbenchmarks

### 2.1 Vary Policy

| Experiment | Config Path | Result Graph (PDF) | Result CSV |
|---|---|---|---|
| Uniform workload | `experiment-configs/Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Uniform-final` | `experiment-results/2-Microbenchmarks/1-Vary-Policy/RW-SQL-U/RW-SQL-U.pdf` | `experiment-results/2-Microbenchmarks/1-Vary-Policy/RW-SQL-U/RW-SQL-U.csv` |
| Zipfian workload | `experiment-configs/Sintr/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Zipf-final` | `experiment-results/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Z/RW-SQL-Z.pdf` | `experiment-results/2-Microbenchmarks/1-Vary-Policy/RW-SQL-Z/RW-SQL-Z.csv` |

### 2.2 Gov Txn (dynamically changing policies)

| Experiment | Config Path | Result Graph (PDF) | Result CSV |
|---|---|---|---|
| Gov Txn | `experiment-configs/Sintr/2-Microbenchmarks/2-Gov-Txn` | `experiment-results/2-Microbenchmarks/2-Gov-Txn/Gov-Txn.pdf` | `experiment-results/2-Microbenchmarks/2-Gov-Txn/Gov-Txn.csv` |

### 2.3 Client Failures

| Experiment | Config Path | Result Graph (PDF) | Result CSV |
|---|---|---|---|
| Client failures (uniform) | `experiment-configs/Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-U` | `experiment-results/2-Microbenchmarks/3-Client-Failures/RW-SQL-U/Client-Failures-U.pdf` | `experiment-results/2-Microbenchmarks/3-Client-Failures/RW-SQL-U/Client-Failures-U.csv` |
| Client failures (zipfian) | `experiment-configs/Sintr/2-Microbenchmarks/3-Client-Failures/RW-SQL-Z` | `experiment-results/2-Microbenchmarks/3-Client-Failures/RW-SQL-Z/Client-Failures-Z.pdf` | `experiment-results/2-Microbenchmarks/3-Client-Failures/RW-SQL-Z/Client-Failures-Z.csv` |

### 2.4 Policy Lifting

| Experiment | Config Path | Result Graph (PDF) | Result CSV |
|---|---|---|---|
| Leveraging policy lifting | `experiment-configs/Sintr/2-Microbenchmarks/4-Lifting` | `experiment-results/2-Microbenchmarks/4-Lifting/lifting-eval.pdf` | `experiment-results/2-Microbenchmarks/4-Lifting/lifting-eval.csv` |