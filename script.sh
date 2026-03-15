#!/bin/bash

# --- CONFIGURATION ---
SOURCE_FILE="lab02.c"       # Your C file name
OUTPUT_EXE="lab02"         # The executable name
CSV_FILE="results-fullrun.csv"    # The output CSV file name
SIZE_INPUTS=(25000 30000 40000 50000 100000) # Add the sizes you want to test here
T_INPUTS=(1 2 4 8 16 32 64)
# ---------------------

# 1. Compile the program
echo "Compiling $SOURCE_FILE..."
gcc -pthread "$SOURCE_FILE" -o "$OUTPUT_EXE"

# Check if compilation succeeded
if [ $? -ne 0 ]; then
    echo "Compilation failed! Exiting."
    exit 1
fi

# 2. Prepare the CSV file (Header)
# This creates columns: Input Size, Thread Count, Run 1, Run 2, Run 3
echo "Input Size,Thread Count,Run 1,Run 2,Run 3" > "$CSV_FILE"

# 3. Loop through each input size (3, 500, etc.)
for n in "${SIZE_INPUTS[@]}"; do
    for t in "${T_INPUTS[@]}"; do
        # Start the row with the input size and thread count
        row_data="$n,$t"
        echo "Testing input size $n and thread count $t"

        # Run the program 3 times for this specific size
        for i in {1..3}; do
            #Recompile
            echo "Recompiling $SOURCE_FILE..."
            gcc -pthread "$SOURCE_FILE" -o "$OUTPUT_EXE"
            # Run the program with input redirection (<<<)
            # We capture the output into a variable.
            # 'tail -n 1' ensures we grab the LAST line printed (usually the result/time)
            result=$(echo -e "$n\n$t" | ./"$OUTPUT_EXE" | tail -n 1)
        
            # Append the result to our current CSV row
            row_data="$row_data,$result"
        done
        
        # Save the completed row to the CSV file
        echo "$row_data" >> "$CSV_FILE"
    done
done

echo "Done! Results saved to $CSV_FILE"
