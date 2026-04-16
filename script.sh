#!/bin/bash

# Usage:
#   ./script.sh standard   # uses lab04.c (default)
#   ./script.sh affined    # uses lab04-core-affined.c
MODE="${1:-standard}"

# --- CONFIGURATION ---
CSV_FILE="results.csv"    # The output CSV file name
N_INPUTS=(1000 5000 10000) # Customize N values here
# t is now explicitly read from config.txt
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

if [ ! -f "config.txt" ]; then
    echo "config.txt not found! Please create it with t on the first line, followed by IPs and ports."
    exit 1
fi

t=$(head -n 1 config.txt | tr -d '\r')

echo "Mode,N,T,Run1,Run2,Run3" > "$CSV_FILE"

for n in "${N_INPUTS[@]}"; do
    row_data="$MODE,$n,$t"
    echo "Testing $MODE with n=$n and t=$t slaves"

    for i in {1..3}; do
        echo "  Run $i..."
        
        # Pause to let the user manually start the slaves in their own terminals/machines
        read -p "Please ensure all $t slaves are running, then press [Enter] to start Master..."
        
        # Run the master locally in the current background
        # Ensure master's port doesn't conflict or is ignored (using 0 here per instructions)
        result=$(./"$OUTPUT_EXE" "$n" 0 0 | tail -n 1)
        
        # Append result
        row_data="$row_data,$result"
    done
    
    # Save the completed row to the CSV file
    echo "$row_data" >> "$CSV_FILE"
done

echo "Done! Results saved to $CSV_FILE"
