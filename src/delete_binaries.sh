#!/bin/bash


## declare an array variable
declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2"
						"eu-west-1-0")

declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0"
						"client-3-0")


USER="dhl001"
EXP_NAME="sintr-rw"
CLIENTS_PER_SERVER=1
BINARY_PATH="sintr"

while getopts u:e:n:b option; do
case "${option}" in
u) USER=${OPTARG};;
e) EXP_NAME=${OPTARG};;
n) CLIENTS_PER_SERVER=${OPTARG};;
b) BINARY_PATH=${OPTARG};;
esac;
done

additional_clients=()
for (( i=1; i<$CLIENTS_PER_SERVER; i++ )); do
	for host in "${arr_clients[@]}"
	do
		additional_clients+=("${host:0:-1}$i")
	done
done
arr_clients+=("${additional_clients[@]}")
echo "arr_clients: ${arr_clients[@]}"

## now loop through the above array
for host in "${arr_servers[@]}"
do
   echo "removing binary of $host"
   ssh ${USER}@$host.${EXP_NAME}.pequin-pg0.utah.cloudlab.us "sudo rm -rf ${BINARY_PATH}"
done

for host in "${arr_clients[@]}"
do
   echo "removing binary of $host"
   ssh ${USER}@$host.${EXP_NAME}.pequin-pg0.utah.cloudlab.us "sudo rm -rf ${BINARY_PATH}"
done

