import sys

if len(sys.argv) != 2:
    print("Usage: python compute_consensus_latency.py <log_file>")
    sys.exit(1)

# log file format: <timestamp>,<tx_id>,<event>
# event: begin, end
# computes difference between begin and end for each tx_id and averages them

with open(sys.argv[1], "r") as f:
    lines = f.readlines()

    begin_lines = [line for line in lines if "begin" in line]
    end_lines = [line for line in lines if "end" in line]

    begin_times = [int(line.split(",")[0]) for line in begin_lines]
    end_times = [int(line.split(",")[0]) for line in end_lines]
    tx_ids = [line.split(",")[1] for line in begin_lines]

    tx_latency = []
    for tx_id in set(tx_ids):
        begin_time = [int(line.split(",")[0]) for line in begin_lines if line.split(",")[1] == tx_id][0]
        if tx_id in [line.split(",")[1] for line in end_lines]:
            end_time = [int(line.split(",")[0]) for line in end_lines if line.split(",")[1] == tx_id][-1]
            tx_latency.append(end_time - begin_time)

    # average
    avg_tx_latency = sum(tx_latency) / len(tx_latency)
    print(f"Average consensus latency: {avg_tx_latency} ms")