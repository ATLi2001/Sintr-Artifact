import sys

if len(sys.argv) != 2:
    print("Usage: python compute_consensus_latency.py <log_file>")
    sys.exit(1)

# log file format: <timestamp>,<tx_id>,<event>
# event: begin, end
# computes difference between begin and end for each tx_id and averages them

begin_times = {}  # tx_id -> timestamp
end_times = {}    # tx_id -> timestamp

with open(sys.argv[1], "r") as f:    
    # Single pass through the file
    for line in f:
        line = line.strip()
        if not line:
            continue
            
        parts = line.split(",")
        if len(parts) != 3:
            continue
        if not "begin" in parts and not "end" in parts:
            continue

        timestamp = int(parts[0])
        tx_id = parts[1]
        event = parts[2]
        
        if event == "begin":
            begin_times[tx_id] = timestamp
        elif event == "end":
            end_times[tx_id] = timestamp

# Calculate latencies
tx_latencies = []
for tx_id, begin_time in begin_times.items():
    if tx_id in end_times:
        # Use the last end time for this transaction
        end_time = end_times[tx_id]
        tx_latencies.append(end_time - begin_time)

# Calculate average
if tx_latencies:
    avg_tx_latency = sum(tx_latencies) / len(tx_latencies)
    print(f"Average consensus latency: {avg_tx_latency} ms")
else:
    print("No completed transactions found")