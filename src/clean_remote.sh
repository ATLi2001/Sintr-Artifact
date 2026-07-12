#!/bin/bash

##use this if I want to upload things onto the machines..
##scp  -r file fs435@${machine}.indicus.morty-pg0.utah.cloudlab.us:/users/fs435/
## ssh ... sudo mv .. ...

## declare an array variable
declare -a arr_servers=("us-east-1-0" "us-east-1-1" "us-east-1-2"
						"eu-west-1-0" "eu-west-1-1" "eu-west-1-2"
			  		   )

declare -a arr_clients=("client-0-0" "client-1-0" "client-2-0"
						"client-3-0" "client-4-0" "client-5-0"
			  		   )


USER="fs435"
EXP_NAME="pequin"
CLIENTS_PER_SERVER=1
BINARY_NAME="sintr"

while getopts u:e:n:b option; do
case "${option}" in
u) USER=${OPTARG};;
e) EXP_NAME=${OPTARG};;
n) CLIENTS_PER_SERVER=${OPTARG};;
b) BINARY_NAME=${OPTARG};;
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
   echo "emptying experiments folder of $host"
   ssh ${USER}@$host.${EXP_NAME}.pequin-pg0.utah.cloudlab.us "sudo rm -rf /mnt/extra/experiments/*; sudo rm -rf $BINARY_NAME"
done

for host in "${arr_clients[@]}"
do
   echo "emptying experiments folder of $host"
   ssh ${USER}@$host.${EXP_NAME}.pequin-pg0.utah.cloudlab.us "sudo rm -rf /mnt/extra/experiments/*; sudo rm -rf $BINARY_NAME"
done

