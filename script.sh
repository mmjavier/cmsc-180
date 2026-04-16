#!/bin/bash

# Usage:
#   ./script.sh standard   # uses lab04.c (default)
#   ./script.sh affined    # uses lab04-core-affined.c
MODE="${1:-standard}"

# --- CONFIGURATION ---
CSV_FILE="results.csv"    # The output CSV file name
N_INPUTS=(1000 5000 10000) # Customize N values here
T_INPUTS=(1 2 4 8)         # Customize T (slave count) values here
MASTER_CONFIG="configuration.txt" # Maximum list of IPs and ports
# ---------------------

if [ "$MODE" = "affined" ]; then
    SOURCE_FILE="lab04-core-affined.c"
    OUTPUT_EXE="lab04-core-affined"
elif [ "$MODE" = "standard" ]; then
    SOURCE_FILE="lab04.c"
    OUTPUT_EXE="lab04"
else
    echo "Invalid mode: $MODE"
    echo "Usage: ./script.sh [standard|affined]"
    exit 1
fi

echo "Compiling $SOURCE_FILE..."
gcc -pthread "$SOURCE_FILE" -o "$OUTPUT_EXE" -lm -O2

if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
    exit 1
fi

if [ ! -f "$MASTER_CONFIG" ]; then
    echo "$MASTER_CONFIG not found! Please create it with IPs and ports (one per line) for your max slaves."
    exit 1
fi

echo "Mode,N,T,Run1,Run2,Run3" > "$CSV_FILE"

for n in "${N_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        row_data="$MODE,$n,$t"
        echo "Testing $MODE with n=$n and t=$t slaves"

        # Generate config.txt for the C program dynamically based on t
        # Takes the first t lines from the master configuration.txt
        echo "$t" > config.txt
        head -n "$t" "$MASTER_CONFIG" >> config.txt

        for i in {1..3}; do
            echo "  Run $i..."
            
            # Pause to let the user manually start the slaves
            read -p "Please ensure the first $t slaves are running, then press [Enter] to start Master..."
            
            # Run the master locally in the current foreground
            # Master's port is ignored (using 0 here)
            result=$(./"$OUTPUT_EXE" "$n" 0 0 | tail -n 1)
            
            # Append result
            row_data="$row_data,$result"
        done
        
        # Save the completed row to the CSV file
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
