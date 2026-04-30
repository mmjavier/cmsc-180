#!/bin/bash
# ============================================================
# LRP04 SSH Automation Script
# CMSC 180 - Distributing Parts of a Matrix over Sockets
# ============================================================
# SETUP BEFORE RUNNING:
#   1. Set MASTER_IP to this machine's LAN IP
#   2. Set SSH_USER to your username on the remote PCs
#   3. Install sshpass if missing:  sudo apt install sshpass
#   4. Compile first:               gcc -O2 -o lab04 lab04.c
#   5. Ensure the slave PCs' firewalls allow ports 5001-5016
# ============================================================

# NOTE: intentionally NO "set -e" — arithmetic like ((x++)) when x=0
# evaluates to 0 (falsy) and would silently kill the script under set -e.
# Error handling is done explicitly per command instead.

# ------- USER CONFIGURATION -------
MASTER_IP="10.0.4.181"     # <-- Set to THIS machine's LAN IP
SSH_USER="acer"             # <-- SSH username on all slave PCs
SSH_PASS="useruser"         # Password for all slave PCs
BINARY="./lab04"            # Compiled binary (must be in current dir)
REMOTE_DIR="~/Desktop"     # Scratch dir on slave PCs
OUTPUT_CSV="results_ssh.csv"
SLAVE_START_DELAY=3         # Seconds to wait for slaves to reach accept()
RUN_COOLDOWN=8              # Seconds between runs so TCP ports drain
# -----------------------------------

# sshpass wrappers — used everywhere instead of bare ssh/scp
SSH="sshpass -p ${SSH_PASS} ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10"
SCP="sshpass -p ${SSH_PASS} scp -o StrictHostKeyChecking=no"

# 4 slave PCs (round-robin: rank r -> PC index r%4)
SLAVE_IPS=(
    "10.0.4.184"
    "10.0.4.245"
    "10.0.5.28"
    "10.0.4.24"
)

# Port for slave rank r = BASE_PORT + r + 1  (ranks 0-15 -> ports 5001-5016)
BASE_PORT=5000

N_VALUES=(4000 8000 16000)
T_VALUES=(2 4 8 16)
RUNS=3

# ---- Terminal colors ----
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
# All logging goes to stderr so it never pollutes captured stdout in run_one()
info()    { echo -e "${CYAN}[INFO]${NC}  $*" >&2; }
success() { echo -e "${GREEN}[OK]${NC}    $*" >&2; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*" >&2; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# -------------------------------------------------------
# Helpers: map rank -> IP / port
# -------------------------------------------------------
get_slave_ip()   { echo "${SLAVE_IPS[$(( $1 % 4 ))]}"; }
get_slave_port() { echo "$(( BASE_PORT + $1 + 1 ))"; }

# -------------------------------------------------------
# generate_configs <t>
# -------------------------------------------------------
generate_configs() {
    local t=$1
    {
        echo "$t"
        for (( rank=0; rank<t; rank++ )); do
            echo "$(get_slave_ip $rank) $(get_slave_port $rank)"
        done
    } > config.txt
    echo "$MASTER_IP" > config_master.txt

    info "Config for t=$t:"
    cat config.txt
}

# -------------------------------------------------------
# deploy_to_slaves <t>
# -------------------------------------------------------
deploy_to_slaves() {
    local t=$1
    declare -A seen

    for (( rank=0; rank<t; rank++ )); do
        local ip
        ip=$(get_slave_ip $rank)
        if [[ -z "${seen[$ip]}" ]]; then
            seen[$ip]=1
            info "Deploying to $ip ..."
            $SSH "${SSH_USER}@${ip}" "mkdir -p ${REMOTE_DIR}"
            $SCP -q "$BINARY" config.txt \
                "${SSH_USER}@${ip}:${REMOTE_DIR}/"
            success "Deployed to $ip"
        fi
    done
}

# -------------------------------------------------------
# start_slaves <n> <t>
#   Launches each slave in the background via SSH.
#   Populates global SLAVE_PIDS array.
# -------------------------------------------------------
declare -a SLAVE_PIDS
start_slaves() {
    local n=$1 t=$2
    SLAVE_PIDS=()

    for (( rank=0; rank<t; rank++ )); do
        local ip port
        ip=$(get_slave_ip $rank)
        port=$(get_slave_port $rank)
        info "Starting slave rank=$rank on $ip:$port ..."
        $SSH "${SSH_USER}@${ip}" \
            "cd ${REMOTE_DIR} && ./lab04 ${n} ${port} 1 \
             > slave_rank${rank}.log 2>&1" &
        SLAVE_PIDS+=($!)
    done

    info "Waiting ${SLAVE_START_DELAY}s for slaves to reach accept()..."
    sleep "$SLAVE_START_DELAY"
}

# -------------------------------------------------------
# wait_slaves — blocks until all background SSH jobs finish
# -------------------------------------------------------
wait_slaves() {
    for pid in "${SLAVE_PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
}

# -------------------------------------------------------
# kill_slaves <t> — emergency cleanup
# -------------------------------------------------------
kill_slaves() {
    local t=$1
    declare -A seen
    for (( rank=0; rank<t; rank++ )); do
        local ip
        ip=$(get_slave_ip $rank)
        if [[ -z "${seen[$ip]}" ]]; then
            seen[$ip]=1
            $SSH "${SSH_USER}@${ip}" "pkill -f lab04 2>/dev/null; true" &
        fi
    done
    wait
}

# -------------------------------------------------------
# collect_slave_logs <t>
#   Fetches each slave's log into ./slave_logs/.
#   Also sets the global LAST_SLAVE_LOGS array so
#   get_slave_max_time() can read the just-fetched files.
# -------------------------------------------------------
declare -a LAST_SLAVE_LOGS
collect_slave_logs() {
    local t=$1
    mkdir -p slave_logs
    LAST_SLAVE_LOGS=()

    for (( rank=0; rank<t; rank++ )); do
        local ip dest
        ip=$(get_slave_ip $rank)
        dest="slave_logs/rank${rank}_${ip}.log"
        $SCP -q "${SSH_USER}@${ip}:${REMOTE_DIR}/slave_rank${rank}.log" \
            "$dest" 2>/dev/null || true
        LAST_SLAVE_LOGS+=("$dest")
    done

    info "Slave logs saved to ./slave_logs/"
}

# -------------------------------------------------------
# get_slave_max_time <t>
#   Reads LAST_SLAVE_LOGS (populated by collect_slave_logs),
#   extracts each slave's computation time, and echoes the
#   maximum value.  Echoes "ERROR" if no valid time is found.
#
#   Adjust the grep/awk pattern below to match the exact line
#   your lab04 slave binary prints, e.g.:
#     "Total Time Elapsed: 1.234567 seconds"
#     "Computation time: 1.234567"
#     "MMT time: 1.234567 s"
# -------------------------------------------------------
get_slave_max_time() {
    local t=$1
    local max_time="0"
    local found=false

    for (( rank=0; rank<t; rank++ )); do
        local logfile="${LAST_SLAVE_LOGS[$rank]}"
        if [[ ! -f "$logfile" ]]; then
            warn "Slave log not found: $logfile" >&2
            continue
        fi

        # ── EDIT THIS PATTERN to match your slave's output line ──
        # Current pattern matches lines like:
        #   "Total Time Elapsed: 1.234567 seconds"
        # If your slave prints something different, change the grep
        # pattern and the awk field index accordingly.
        local stime
        stime=$(grep -i 'Total Time Elapsed:' "$logfile" \
                | awk '{print $4}' \
                | tail -1)

        if [[ -z "$stime" ]]; then
            warn "Could not parse time from slave log: $logfile" >&2
            continue
        fi

        info "  Slave rank=$rank time=${stime}s" >&2
        found=true

        # Keep running maximum using awk for float comparison
        max_time=$(awk -v a="$max_time" -v b="$stime" \
                   'BEGIN { print (b+0 > a+0) ? b : a }')
    done

    if $found; then
        echo "$max_time"
    else
        echo "ERROR"
    fi
}

# -------------------------------------------------------
# run_one <n> <t> <run#>
#   Runs a single experiment. Echoes two space-separated
#   values on stdout:  <master_elapsed> <slave_max_time>
#   (or ERROR tokens on failure).
#   All human-readable output goes to stderr.
# -------------------------------------------------------
run_one() {
    local n=$1 t=$2 run=$3
    echo "" >&2
    info "---------- n=$n  t=$t  run=$run ----------" >&2

    start_slaves "$n" "$t"

    info "Starting master (n=$n)..." >&2
    local master_out elapsed
    master_out=$("$BINARY" "$n" "$MASTER_PORT" 0 2>&1)
    local rc=$?

    wait_slaves

    # Always collect logs so slave times are available even on error
    collect_slave_logs "$t"

    if [[ $rc -ne 0 ]]; then
        error "Master exited with code $rc" >&2
        echo "ERROR ERROR"
        return
    fi

    elapsed=$(echo "$master_out" | grep 'Total Time Elapsed:' | awk '{print $4}')
    if [[ -z "$elapsed" ]]; then
        warn "Could not parse elapsed time. Master output:" >&2
        echo "$master_out" >&2
        echo "PARSE_ERROR PARSE_ERROR"
        return
    fi

    local slave_max
    slave_max=$(get_slave_max_time "$t")

    success "n=$n  t=$t  run=$run  ->  master=${elapsed}s  slave_max=${slave_max}s" >&2
    echo "$elapsed $slave_max"

    info "Cooling down ${RUN_COOLDOWN}s ..." >&2
    sleep "$RUN_COOLDOWN"
}

# -------------------------------------------------------
# check_prerequisites
# -------------------------------------------------------
check_prerequisites() {
    info "Checking prerequisites..."

    if [[ "$MASTER_IP" == *"X.XXX"* ]]; then
        error "Set MASTER_IP at the top of this script to this machine's LAN IP."
        exit 1
    fi

    if [[ ! -x "$BINARY" ]]; then
        error "Binary '$BINARY' not found. Compile first: gcc -O2 -o lab04 lab04.c"
        exit 1
    fi

    if ! command -v sshpass &>/dev/null; then
        error "sshpass not found. Install: sudo apt install sshpass"
        exit 1
    fi

    declare -A seen
    for ip in "${SLAVE_IPS[@]}"; do
        [[ -n "${seen[$ip]}" ]] && continue
        seen[$ip]=1
        if ! sshpass -p "${SSH_PASS}" ssh -o StrictHostKeyChecking=no \
                -o ConnectTimeout=5 "${SSH_USER}@${ip}" "true" 2>/dev/null; then
            error "Cannot reach ${SSH_USER}@${ip} — wrong password or host down?"
            exit 1
        fi
        success "SSH OK: $ip"
    done
}

# -------------------------------------------------------
# MAIN
# -------------------------------------------------------
main() {
    echo -e "${CYAN}"
    echo "================================================="
    echo "  LRP04 — Distributed Matrix via Sockets (SSH)"
    echo "  CMSC 180 | $(date)"
    echo "================================================="
    echo -e "${NC}"

    check_prerequisites

    # CSV header
    echo "n,t,Run 1,Run 2,Run 3,Average,slave_max_run1,slave_max_run2,slave_max_run3,slave_max_ave" \
        > "$OUTPUT_CSV"

    local total=$(( ${#N_VALUES[@]} * ${#T_VALUES[@]} ))
    local done_count=0

    for n in "${N_VALUES[@]}"; do
        for t in "${T_VALUES[@]}"; do
            done_count=$(( done_count + 1 ))
            echo ""
            info "========== n=$n  t=$t  ($done_count / $total combos) =========="

            generate_configs "$t"
            deploy_to_slaves "$t"

            local master_times=()
            local slave_max_times=()
            local master_sum=0
            local slave_sum=0
            local master_all_ok=true
            local slave_all_ok=true

            for (( run=1; run<=RUNS; run++ )); do
                # run_one echoes "<master_elapsed> <slave_max>" on stdout
                local result_line
                result_line=$(run_one "$n" "$t" "$run")

                local mtime stime
                mtime=$(echo "$result_line" | awk '{print $1}')
                stime=$(echo "$result_line" | awk '{print $2}')

                master_times+=("$mtime")
                slave_max_times+=("$stime")

                # Accumulate master sum
                if echo "$mtime" | grep -qE '^[0-9]+\.[0-9]+$'; then
                    master_sum=$(awk "BEGIN { printf \"%.6f\", $master_sum + $mtime }")
                else
                    master_all_ok=false
                fi

                # Accumulate slave max sum
                if echo "$stime" | grep -qE '^[0-9]+\.[0-9]+$'; then
                    slave_sum=$(awk "BEGIN { printf \"%.6f\", $slave_sum + $stime }")
                else
                    slave_all_ok=false
                fi
            done

            # Compute averages
            local master_avg slave_avg
            if $master_all_ok; then
                master_avg=$(awk "BEGIN { printf \"%.6f\", $master_sum / $RUNS }")
            else
                master_avg="ERROR"
            fi

            if $slave_all_ok; then
                slave_avg=$(awk "BEGIN { printf \"%.6f\", $slave_sum / $RUNS }")
            else
                slave_avg="ERROR"
            fi

            # One CSV row for this (n, t) pair
            echo "${n},${t},${master_times[0]},${master_times[1]},${master_times[2]},${master_avg},${slave_max_times[0]},${slave_max_times[1]},${slave_max_times[2]},${slave_avg}" \
                >> "$OUTPUT_CSV"

            success "Row written -> n=$n t=$t"
            success "  master runs : ${master_times[0]}, ${master_times[1]}, ${master_times[2]} | avg: ${master_avg}"
            success "  slave maxes : ${slave_max_times[0]}, ${slave_max_times[1]}, ${slave_max_times[2]} | avg: ${slave_avg}"
        done
    done

    echo ""
    success "All done! Results saved to: $OUTPUT_CSV"
    echo ""

    # Pretty-print the table
    echo "===== RESULTS ====="
    column -t -s',' "$OUTPUT_CSV"
}

# Cleanup on Ctrl-C / TERM
trap 'echo ""; warn "Interrupted — cleaning up slaves..."; kill_slaves 16; exit 1' INT TERM

main "$@"