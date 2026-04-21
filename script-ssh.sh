#!/bin/bash

# ==============================================================================
# SSH Distributed Execution Script for CMSC 180
# Make sure to install sshpass before running this script:
#   e.g., sudo apt install sshpass OR brew install hudochenkov/sshpass/sshpass
# ==============================================================================

# --- CONFIGURATION ---
USERNAME="your_username"
PASSWORD="your_password"
REMOTE_DIR="~/cmsc180"          # The directory on the slaves where the executable is located
EXEC="./lab04"                  # The executable name (make sure it's compiled on the slaves)
MASTER_PORT=5000                # Dummy port for master (master acts as a client)

CSV_FILE="results-ssh-lab04.csv"
SLAVES_LIST_FILE="slaves_list.txt" # You will manually create this file!

SIZE_INPUTS=(4000 8000 16000)   # Matrix dimensions
T_INPUTS=(2 4 8 16)             # Number of slaves
# ---------------------

# Check if the manual list of IPs and ports exists
if [ ! -f "$SLAVES_LIST_FILE" ]; then
    echo "Error: $SLAVES_LIST_FILE not found!"
    echo "Please create a file named '$SLAVES_LIST_FILE' manually and list ALL 16 IP and Port pairings."
    echo "Example format (Space separated):"
    echo "192.168.1.1 5001"
    echo "192.168.1.2 5001"
    echo "192.168.1.3 5001"
    echo "192.168.1.4 5001"
    echo "192.168.1.1 5002"
    echo "..."
    exit 1
fi

# Prepare the CSV file (Header)
echo "Input Size,Slave Count,Run 1,Run 2,Run 3" > "$CSV_FILE"

# Loop through each input size and slave count
for n in "${SIZE_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        row_data="$n,$t"
        echo "=================================================="
        echo "Testing input size $n and slave count $t"
        echo "=================================================="

        # Run the program 3 times for this specific parameter set
        for run in {1..3}; do
            echo "  Run $run..."
            
            # --- Dynamically create config.txt for the C program ---
            # The C program expects the first line to be the slave count (t)
            echo "$t" > config.txt
            
            # Read exactly 't' lines from your manual SLAVES_LIST_FILE and append to config.txt
            head -n "$t" "$SLAVES_LIST_FILE" >> config.txt
            
            # --- Start Slave Processes in the Background using SSH ---
            head -n "$t" "$SLAVES_LIST_FILE" | while read -r IP PORT; do
                if [ -n "$IP" ] && [ -n "$PORT" ]; then
                    echo "    -> Spawning slave on $IP at port $PORT..."
                    # -o StrictHostKeyChecking=no prevents yes/no prompts
                    # nohup ... < /dev/null > /dev/null 2>&1 & thoroughly spins off the background process
                    sshpass -p "$PASSWORD" ssh -o StrictHostKeyChecking=no "$USERNAME@IP" \
                        "cd $REMOTE_DIR && nohup $EXEC $n $PORT 1 < /dev/null > /dev/null 2>&1 &" &
                fi
            done
            
            # Give remote slaves a moment to boot and start listening
            echo "    Waiting 3 seconds for slaves..."
            sleep 3
            
            # --- Start Master Process Locally ---
            echo "    Running Master..."
            temp_out=$(mktemp)
            
            # Arguments: n, port (dummy), s (0 for master)
            $EXEC "$n" "$MASTER_PORT" 0 > "$temp_out"
            
            # --- Extract time from Master Output ---
            # Output line format: "Total Time Elapsed: X.XXXXXX seconds"
            result=$(grep "Total Time Elapsed:" "$temp_out" | awk '{print $4}')
            
            if [ -z "$result" ]; then
                result="ERR"
                echo "    Failed to get time/Error occurred!"
            else
                echo "    Time measured: $result seconds"
            fi
            
            # Append the result to our current CSV row
            row_data="$row_data,$result"
            
            # Clean up temp file
            rm -f "$temp_out"
            
            # Wait for local background tasks (ssh spawns) to finish terminating
            wait 
            
            # Brief pause before next run
            sleep 1
        done
        
        # Save the completed row to the CSV file
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
