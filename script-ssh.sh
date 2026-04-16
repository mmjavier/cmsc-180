#!/bin/bash

# Usage:
#   ./script-ssh.sh standard   # uses lab04.c (default)
#   ./script-ssh.sh affined    # uses lab04-core-affined.c
MODE="${1:-standard}"

# --- SSH MULTI-PC CONFIGURATION ---
# Replace with the actual username used to SSH into the machines
REMOTE_USER="mykoo" 
# Replace with the shared password for the remote machines
REMOTE_PASS="your_password_here"

# Replace with the actual IP addresses of your network machines
REMOTE_IPS=(
    "192.168.1.10"
    "192.168.1.11"
    "192.168.1.12"
)

# Directory where the files will be copied and executed on remote machines
REMOTE_DIR="/tmp/cmsc180_slaves"

CSV_FILE="results_ssh.csv"
N_INPUTS=(1000 5000 10000)
T_INPUTS=(1 2 4 8)
# ----------------------------------

if [ "$MODE" = "affined" ]; then
    SOURCE_FILE="lab04-core-affined.c"
    OUTPUT_EXE="lab04-core-affined"
elif [ "$MODE" = "standard" ]; then
    SOURCE_FILE="lab04.c"
    OUTPUT_EXE="lab04"
else
    echo "Invalid mode: $MODE"
    echo "Usage: ./script-ssh.sh [standard|affined]"
    exit 1
fi

echo "Compiling $SOURCE_FILE locally for the master..."
gcc -pthread "$SOURCE_FILE" -o "$OUTPUT_EXE" -lm -O2

if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
    exit 1
fi

echo "Mode,N,T,Run1,Run2,Run3" > "$CSV_FILE"

for n in "${N_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        
        row_data="$MODE,$n,$t"
        echo "Testing $MODE with n=$n and t=$t slaves over SSH"

        for i in {1..3}; do
            echo "  Run $i..."
            
            # 1. Generate config.txt dynamically based on t and the available remote IPs
            echo "$t" > config.txt
            
            # Array to keep track of unique IPs we are using this run to deploy files efficiently
            declare -A unique_ips
            
            for (( j=0; j<t; j++ )); do
                # Loop through the available IPs via modulo if T > length of REMOTE_IPS
                ip_index=$(( j % ${#REMOTE_IPS[@]} ))
                ip="${REMOTE_IPS[$ip_index]}"
                port=$(( 8000 + j ))
                
                echo "$ip $port" >> config.txt
                unique_ips["$ip"]=1
            done
            
            # 2. Deploy files and compile on each uniquely involved remote PC
            for ip in "${!unique_ips[@]}"; do
                # Create remote directory and quietly scp the files
                sshpass -p "$REMOTE_PASS" ssh -o StrictHostKeyChecking=no "$REMOTE_USER@$ip" "mkdir -p $REMOTE_DIR"
                sshpass -p "$REMOTE_PASS" scp -o StrictHostKeyChecking=no -q "$SOURCE_FILE" config.txt "$REMOTE_USER@$ip:$REMOTE_DIR/"
                
                # Compile the code securely on the remote machine's native OS to prevent architecture mismatches
                sshpass -p "$REMOTE_PASS" ssh -o StrictHostKeyChecking=no "$REMOTE_USER@$ip" "cd $REMOTE_DIR && gcc -pthread $SOURCE_FILE -o $OUTPUT_EXE -lm -O2"
            done
            
            # 3. Spawn t instances of the slave in the background across the network
            for (( j=0; j<t; j++ )); do
                ip_index=$(( j % ${#REMOTE_IPS[@]} ))
                ip="${REMOTE_IPS[$ip_index]}"
                port=$(( 8000 + j ))
                
                # ssh -f pushes the ssh session to the background, nohup detaches the process completely
                sshpass -p "$REMOTE_PASS" ssh -o StrictHostKeyChecking=no -f "$REMOTE_USER@$ip" "cd $REMOTE_DIR && nohup ./$OUTPUT_EXE $n $port 1 > /dev/null 2>&1 &"
            done
            
            # Sleep a bit to allow SSH background processes to spawn and sockets to bind globally
            echo "    Waiting 3s for native slaves to bind..."
            sleep 3
            
            # 4. Run the master locally in the current foreground
            result=$(./"$OUTPUT_EXE" "$n" 0 0 | tail -n 1)
            
            # Clean up processes on slaves blindly (optional, ensures no zombies if master failed)
            for ip in "${!unique_ips[@]}"; do
                sshpass -p "$REMOTE_PASS" ssh -o StrictHostKeyChecking=no -f "$REMOTE_USER@$ip" "pkill -f $OUTPUT_EXE" > /dev/null 2>&1
            done
            
            sleep 1
            row_data="$row_data,$result"
        done
        
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
