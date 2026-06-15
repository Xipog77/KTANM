#!/bin/bash
# run_mysql_fuzz.sh — Start AFL++ fuzzing against MySQL 8.0.20 (Source)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

CORES=${1:-1}
OFFSET=${2:-0}
NUM_CPUS=$(nproc)

# Validate cores + offset doesn't exceed total CPU count
if [ "$((CORES + OFFSET))" -gt "$NUM_CPUS" ]; then
    echo "[!] Warning: Requested CORES=$CORES with OFFSET=$OFFSET exceeds available CPUS ($NUM_CPUS)."
    # Adjust CORES down to fit
    CORES=$((NUM_CPUS - OFFSET))
    if [ "$CORES" -lt 1 ]; then
        CORES=1
        OFFSET=0
    fi
    echo "[*] Adjusted to CORES=$CORES, OFFSET=$OFFSET."
fi

# Clean up any orphaned fuzzer or database processes from previous runs
pkill -f mysql_fuzzer 2>/dev/null || true
pkill -f mysql_install/bin/mysqld 2>/dev/null || true

# Clean stale output files or socket if needed
rm -rf outputs/mysql_fuzzer* 2>/dev/null
rm -f /tmp/mysql_fuzz_*.sock /tmp/mysql_fuzz_*.sock.lock /tmp/mysql_fuzz_*.pid 2>/dev/null

# Function to generate plot on exit
generate_plot() {
    if [ -f "outputs/mysql_fuzzer01/plot_data" ] && [ -s "outputs/mysql_fuzzer01/plot_data" ]; then
        echo "[*] Generating AFL plot..."
        mkdir -p outputs/mysql_fuzzer01/plot
        afl-plot outputs/mysql_fuzzer01 outputs/mysql_fuzzer01/plot || echo "[!] Failed to run afl-plot"
    else
        echo "[*] No plot_data found, skipping plot generation."
    fi
}

CHILD_PIDS=()
CLEANED_UP=0
cleanup() {
    if [ "$CLEANED_UP" -ne 0 ]; then
        return
    fi
    CLEANED_UP=1
    if [ ${#CHILD_PIDS[@]} -gt 0 ]; then
        echo "[*] Stopping all secondary fuzzers..."
        for pid in "${CHILD_PIDS[@]}"; do
            kill "$pid" 2>/dev/null || true
        done
    fi
    generate_plot
}
trap cleanup EXIT INT TERM

# Start secondary instances in background if CORES > 1
if [ "$CORES" -gt 1 ]; then
    for i in $(seq 2 $CORES); do
        ID=$(printf "%02d" $i)
        
        # Clone data directory if not exists
        if [ ! -d "/home/dokhanh/mysql_data_$ID" ]; then
            echo "[*] Initializing data directory /home/dokhanh/mysql_data_$ID..."
            cp -a /home/dokhanh/mysql_data "/home/dokhanh/mysql_data_$ID"
        fi
        
        # Start secondary fuzzer
        TARGET_CORE=$((i - 1 + OFFSET))
        echo "[*] Starting secondary fuzzer $ID on CPU core $TARGET_CORE..."
        FUZZ_ID=$ID AFL_MAP_SIZE=8388608 AFL_FORKSRV_INIT_TMOUT=15000 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
        AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
        AFL_CUSTOM_MUTATOR_ONLY=1 \
        AFL_CUSTOM_INFO_PROGRAM=./mysql_fuzzer \
        AFL_CUSTOM_INFO_PROGRAM_ARGV="" \
        AFL_CUSTOM_INFO_OUT="outputs/mysql_fuzzer$ID" \
        afl-fuzz -b $TARGET_CORE -m none -i inputs/ -o outputs/ -S "mysql_fuzzer$ID" -- ./mysql_fuzzer > "outputs/mysql_fuzzer${ID}.log" 2>&1 &
        
        CHILD_PIDS+=($!)
    done
fi

# Clone data directory for Main fuzzer if not exists
if [ ! -d "/home/dokhanh/mysql_data_01" ]; then
    echo "[*] Initializing data directory /home/dokhanh/mysql_data_01..."
    cp -a /home/dokhanh/mysql_data "/home/dokhanh/mysql_data_01"
fi

echo "[*] Starting main fuzzer 01 on CPU core $OFFSET..."
FUZZ_ID=01 AFL_MAP_SIZE=8388608 AFL_FORKSRV_INIT_TMOUT=15000 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
AFL_CUSTOM_MUTATOR_ONLY=1 \
AFL_CUSTOM_INFO_PROGRAM=./mysql_fuzzer \
AFL_CUSTOM_INFO_PROGRAM_ARGV="" \
AFL_CUSTOM_INFO_OUT="outputs/mysql_fuzzer01" \
afl-fuzz -b $OFFSET -m none -i inputs/ -o outputs/ -M mysql_fuzzer01 -- ./mysql_fuzzer

