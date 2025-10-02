import sys

def calculate_latency(begin_times, end_times):
    latencies = []
    for id, begin_time in begin_times.items():
        if id in end_times:
            end_time = end_times[id]
            latencies.append(end_time - begin_time)
    if latencies:
        return sum(latencies) / len(latencies)
    else:
        return None

if __name__ == "__main__":

    if len(sys.argv) != 2:
        print("Usage: python compute_consensus_latency.py <log_file>")
        sys.exit(1)

    # log file format: <timestamp>,<tx_id>,<event>
    # event: begin, end
    # computes difference between begin and end for each tx_id and averages them

    begin_times = {}  # tx_id -> timestamp
    end_times = {}    # tx_id -> timestamp
    header_begin_times = {}  # header_id -> timestamp
    header_end_times = {}    # header_id -> timestamp

    with open(sys.argv[1], "r") as f:    
        # Single pass through the file
        for line in f:
            line = line.strip()
            if not line:
                continue

            if not "begin" in line and not "end" in line:
                continue

            parts = line.split(",")
            if len(parts) != 3:
                continue

            timestamp = int(parts[0])
            id = parts[1]
            event = parts[2]
            
            if event == "begin":
                begin_times[id] = timestamp
            elif event == "end":
                end_times[id] = timestamp
            elif event == "header_begin":
                header_begin_times[id] = timestamp
            elif event == "header_end":
                header_end_times[id] = timestamp

    # Calculate average
    tx_avg_latency = calculate_latency(begin_times, end_times)
    header_avg_latency = calculate_latency(header_begin_times, header_end_times)
    print(f"Average transaction latency: {tx_avg_latency} ms")
    print(f"Average header latency: {header_avg_latency} ms")
