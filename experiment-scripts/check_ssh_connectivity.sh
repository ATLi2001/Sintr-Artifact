#!/bin/bash

## declare an array variable
#declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2"
#                        "eu-west-1-0")

declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2"
                        "eu-west-1-0" "eu-west-1-1" "eu-west-1-2")

#declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0"
#                        "client-3-0")

declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0"
                        "client-3-0" "client-4-0" "client-5-0")
USER="dhl001"
EXP_NAME="sintr-rw"
CLIENTS_PER_SERVER=1

while getopts u:e:n: option; do
case "${option}" in
    u) USER=${OPTARG};;
    e) EXP_NAME=${OPTARG};;
    n) CLIENTS_PER_SERVER=${OPTARG};;
esac
done

# Expand client list if CLIENTS_PER_SERVER > 1
additional_clients=()
for (( i=1; i<$CLIENTS_PER_SERVER; i++ )); do
    for host in "${arr_clients[@]}"; do
        additional_clients+=("${host:0:-1}$i")
    done
done
arr_clients+=("${additional_clients[@]}")

echo "Client list: ${arr_clients[@]}"

unreachable_hosts=()
bad_banner_hosts=()

### --- NEW: FUNCTION TO CHECK LOGIN BANNER ---
check_banner() {
    local host=$1
    local fqdn="${USER}@${host}.${EXP_NAME}.pequin-pg0.utah.cloudlab.us"

    echo -n "[*] Checking SSH banner for $host ... "

    # Capture banner but suppress command output
    # -T avoids pseudo-terminal issues
    banner=$(ssh -T -o StrictHostKeyChecking=accept-new \
                 -o ConnectTimeout=10 \
                 "$fqdn" 2>/dev/null <<< "")

    if [[ -z "$banner" ]]; then
        echo "❌  NO OUTPUT"
        bad_banner_hosts+=("$host")
        return
    fi

    if echo "$banner" | grep -q "Welcome to Ubuntu"; then
        echo "✔️  correct banner"
    else
        echo "❌  unexpected banner"
        bad_banner_hosts+=("$host")
    fi
}

### --- CHECK SERVERS ---
echo "=== Checking Server Banners ==="
for host in "${arr_servers[@]}"; do
    check_banner "$host"
done

### --- CHECK CLIENTS ---
echo "=== Checking Client Banners ==="
for host in "${arr_clients[@]}"; do
    check_banner "$host"
done

### --- SUMMARY ---
echo ""
if [ ${#bad_banner_hosts[@]} -eq 0 ]; then
    echo "✅ All hosts produced correct Ubuntu login banners!"
else
    echo "❌ Hosts with missing or incorrect banners:"
    for host in "${bad_banner_hosts[@]}"; do
        echo "   - $host"
    done
    exit 1
fi