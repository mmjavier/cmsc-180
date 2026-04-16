#!/bin/bash

# Usage:
#   ./script.sh standard   # uses lab04.c (default)
#   ./script.sh affined    # uses lab04-core-affined.c
MODE="${1:-standard}"

# --- CONFIGURATION ---
CSV_FILE="results.csv"    # The output CSV file name
N_INPUTS=(1000 5000 10000) # Customize N values here
T_INPUTS=(1 2 4 8)         # Customize T (slave count) values here
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

echo "Mode,N,T,Run1,Run2,Run3" > "$CSV_FILE"

for n in "${N_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        
        row_data="$MODE,$n,$t"
        echo "Testing $MODE with n=$n and t=$t slaves"

        for i in {1..3}; do
            echo "  Run $i..."
            
            # Generate config.txt
            > config.txt
            for (( j=1; j<=t; j++ )); do
                port=$(( 8000 + j ))
                echo "127.0.0.1 $port" >> config.txt
            done
            
            # Spawn t instances of the slave in new Linux Terminals (xterm)
            for (( j=1; j<=t; j++ )); do
                port=$(( 8000 + j ))
                # Open a new Linux Terminal window (xterm) specifically for each slave
                xterm -hold -e "cd \"$(pwd)\" && ./$OUTPUT_EXE $n $port 1" &
            done
            
            # Sleep a bit longer (2s) to allow Terminal windows to open and sockets to bind
            sleep 2
            
            # Run the master locally in the current background
            # Ensure master's port doesn't conflict or is ignored (using 0 here per instructions)
            result=$(./"$OUTPUT_EXE" "$n" 0 0 | tail -n 1)
            
            # Note: We can't wait on PIDs since they're in separate Terminal.app processes, 
            # but they will automatically finish their execution once the master completes.
            sleep 1
            
            # Append result
            row_data="$row_data,$result"
        done
        
        # Save the completed row to the CSV file
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
