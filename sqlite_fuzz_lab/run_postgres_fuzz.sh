#!/bin/bash
# run_postgres_fuzz.sh — Start AFL++ fuzzing against PostgreSQL 12.2 (Source single-user mode)
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

# Ensure correct ownership of outputs (following symlinks) if running with sudo
if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER":"$SUDO_USER" /home/dokhanh/fuzz_outputs 2>/dev/null || true
    chown -h "$SUDO_USER":"$SUDO_USER" outputs 2>/dev/null || true
fi

# Clean stale output files/directories of pg_fuzzer to avoid AFL++ reuse errors
rm -rf outputs/pg_fuzzer* 2>/dev/null || true

# Function to generate plot on exit
generate_plot() {
    if [ -f "outputs/pg_fuzzer01/plot_data" ] && [ -s "outputs/pg_fuzzer01/plot_data" ]; then
        echo "[*] Generating AFL plot..."
        mkdir -p outputs/pg_fuzzer01/plot
        afl-plot outputs/pg_fuzzer01 outputs/pg_fuzzer01/plot || echo "[!] Failed to run afl-plot"
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
        if [ ! -d "/home/dokhanh/pg_data_$ID" ]; then
            echo "[*] Initializing data directory /home/dokhanh/pg_data_$ID..."
            cp -a /home/dokhanh/pg_data "/home/dokhanh/pg_data_$ID"
        fi
        
        # Ensure correct permissions if running with sudo
        if [ -n "$SUDO_USER" ]; then
            chown -R "$SUDO_USER":"$SUDO_USER" "/home/dokhanh/pg_data_$ID"
        fi
        
        # Start secondary fuzzer
        TARGET_CORE=$((i - 1 + OFFSET))
        echo "[*] Starting secondary fuzzer $ID on CPU core $TARGET_CORE..."
        
        TARGET_CMD="AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
        AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
        AFL_CUSTOM_MUTATOR_ONLY=1 \
        AFL_CUSTOM_INFO_PROGRAM=/home/dokhanh/Desktop/data/Lab/KTANM/sqlite_fuzz_lab/pg_install/bin/postgres \
        AFL_CUSTOM_INFO_PROGRAM_ARGV=\"--single -F -c shared_buffers=16MB -c fsync=off -c synchronous_commit=off -c full_page_writes=off -D /home/dokhanh/pg_data_$ID fuzz_db\" \
        AFL_CUSTOM_INFO_OUT=\"outputs/pg_fuzzer$ID\" \
        afl-fuzz -b $TARGET_CORE -m none -i inputs/ -o outputs/ -S pg_fuzzer$ID -- \
          /home/dokhanh/Desktop/data/Lab/KTANM/sqlite_fuzz_lab/pg_install/bin/postgres \
          --single -F -c shared_buffers=16MB -c fsync=off -c synchronous_commit=off -c full_page_writes=off -D /home/dokhanh/pg_data_$ID fuzz_db"
          
        if [ -n "$SUDO_USER" ]; then
            sudo -u "$SUDO_USER" bash -c "$TARGET_CMD" > "outputs/pg_fuzzer${ID}.log" 2>&1 &
        else
            bash -c "$TARGET_CMD" > "outputs/pg_fuzzer${ID}.log" 2>&1 &
        fi
        
        CHILD_PIDS+=($!)
    done
fi

# Clone data directory for Main fuzzer if not exists
if [ ! -d "/home/dokhanh/pg_data_01" ]; then
    echo "[*] Initializing data directory /home/dokhanh/pg_data_01..."
    cp -a /home/dokhanh/pg_data "/home/dokhanh/pg_data_01"
fi

# Ensure correct permissions if running with sudo
if [ -n "$SUDO_USER" ]; then
    chown -R "$SUDO_USER":"$SUDO_USER" "/home/dokhanh/pg_data_01"
    chown -R "$SUDO_USER":"$SUDO_USER" outputs 2>/dev/null || true
fi

echo "[*] Starting main fuzzer 01 on CPU core $OFFSET..."

MAIN_CMD="AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 \
AFL_CUSTOM_MUTATOR_LIBRARY=/home/dokhanh/Desktop/data/Lab/KTANM/Grammar-Mutator/libgrammarmutator-sql.so \
AFL_CUSTOM_MUTATOR_ONLY=1 \
AFL_CUSTOM_INFO_PROGRAM=/home/dokhanh/Desktop/data/Lab/KTANM/sqlite_fuzz_lab/pg_install/bin/postgres \
AFL_CUSTOM_INFO_PROGRAM_ARGV=\"--single -F -c shared_buffers=16MB -c fsync=off -c synchronous_commit=off -c full_page_writes=off -D /home/dokhanh/pg_data_01 fuzz_db\" \
AFL_CUSTOM_INFO_OUT=\"outputs/pg_fuzzer01\" \
afl-fuzz -b $OFFSET -m none -i inputs/ -o outputs/ -M pg_fuzzer01 -- \
  /home/dokhanh/Desktop/data/Lab/KTANM/sqlite_fuzz_lab/pg_install/bin/postgres \
  --single -F -c shared_buffers=16MB -c fsync=off -c synchronous_commit=off -c full_page_writes=off -D /home/dokhanh/pg_data_01 fuzz_db"

if [ -n "$SUDO_USER" ]; then
    sudo -u "$SUDO_USER" bash -c "$MAIN_CMD"
else
    bash -c "$MAIN_CMD"
fi
