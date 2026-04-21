#!/bin/bash

# --- CONFIGURATION ---
SOURCE_FILE="lab04.c"
OUTPUT_EXE="lab04"
CSV_FILE="results-lab04.csv"
SIZE_INPUTS=(4000 8000 16000)   # Matrix dimensions
T_INPUTS=(2 4 8 16)             # Number of slaves
# ---------------------

# 1. Compile the program
echo "Compiling $SOURCE_FILE..."
gcc "$SOURCE_FILE" -o "$OUTPUT_EXE"

# Check if compilation succeeded
if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
    exit 1
fi

# 2. Prepare the CSV file (Header)
echo "Input Size,Slave Count,Run 1,Run 2,Run 3" > "$CSV_FILE"

# 3. Loop through each input size and slave count
for n in "${SIZE_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        row_data="$n,$t"
        echo "Testing input size $n and slave count $t"

        # Run the program 3 times for this specific parameter set
        for run in {1..3}; do
            echo "  Run $run..."
            
            # --- Dynamically create config.txt ---
            echo "$t" > config.txt
            for (( i=0; i<t; i++ )); do
                port=$((5001 + i))
                echo "127.0.0.1 $port" >> config.txt
            done
            
            echo "Compiling $SOURCE_FILE..."
            gcc "$SOURCE_FILE" -o "$OUTPUT_EXE"
            # --- Start Slave Processes in the Background ---
            for (( i=0; i<t; i++ )); do
                port=$((5001 + i))
                # Arguments: n, port, s (1 for slave)
                ./"$OUTPUT_EXE" "$n" "$port" 1 > /dev/null 2>&1 &
            done
            
            # Give slaves a moment to start listening and bind ports
            sleep 0.5
            
            # --- Start Master Process ---
            temp_out=$(mktemp)
            # Arguments: n, port (0 since master doesn't listen), s (0 for master)
            ./"$OUTPUT_EXE" "$n" 0 0 > "$temp_out"
            
            # --- Extract time from Master Output ---
            # Output line format: "Total Time Elapsed: X.XXXXXX seconds"
            result=$(grep "Total Time Elapsed:" "$temp_out" | awk '{print $4}')
            
            # Append the result to our current CSV row
            row_data="$row_data,$result"
            
            # Clean up temp file
            rm -f "$temp_out"
            
            # Wait for all background slave processes to cleanly exit before next run
            wait
        done
        
        # Save the completed row to the CSV file
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
