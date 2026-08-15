#!/bin/bash
# Set HBM (NUMA Node 2) Available Free Capacity via 2MB Hugepage Pre-allocation
set -e

NODE=2
TOTAL_HBM_MB=65536

TARGET_CAP_MB="$1"

if [ -z "$TARGET_CAP_MB" ] || [ "$TARGET_CAP_MB" == "unconstrained" ] || [ "$TARGET_CAP_MB" == "0" ] || [ "$TARGET_CAP_MB" == "65536" ]; then
    echo "[HBM Capacity Manager] Setting HBM Node $NODE to UNCONSTRAINED (64 GB)..."
    echo 0 | sudo tee /sys/devices/system/node/node${NODE}/hugepages/hugepages-2048kB/nr_hugepages > /dev/null
    echo "[HBM Capacity Manager] Hugepages on Node $NODE set to 0."
else
    # Calculate hugepages to reserve
    TARGET_INT=$(printf "%.0f" "$TARGET_CAP_MB")
    RESERVE_MB=$((TOTAL_HBM_MB - TARGET_INT))
    if [ "$RESERVE_MB" -lt 0 ]; then
        RESERVE_MB=0
    fi
    NR_PAGES=$((RESERVE_MB / 2))
    
    echo "[HBM Capacity Manager] Constraining HBM Node $NODE to ~${TARGET_INT} MB free memory..."
    echo "[HBM Capacity Manager] Pre-allocating ${NR_PAGES} hugepages (${RESERVE_MB} MB reserved)..."
    
    # First reset to 0 to avoid fragmentation issues, then allocate
    echo 0 | sudo tee /sys/devices/system/node/node${NODE}/hugepages/hugepages-2048kB/nr_hugepages > /dev/null
    echo ${NR_PAGES} | sudo tee /sys/devices/system/node/node${NODE}/hugepages/hugepages-2048kB/nr_hugepages > /dev/null
    
    ACTUAL_PAGES=$(cat /sys/devices/system/node/node${NODE}/hugepages/hugepages-2048kB/nr_hugepages)
    ACTUAL_FREE_MB=$(numactl --hardware | awk -v node="node $NODE free:" '$0 ~ node {print $4}')
    echo "[HBM Capacity Manager] Verification: Actual hugepages = ${ACTUAL_PAGES}, Actual free memory on Node $NODE = ${ACTUAL_FREE_MB} MB."
    if [ "${ACTUAL_PAGES}" -ne "${NR_PAGES}" ]; then
        echo "[HBM Capacity Manager] WARNING: Requested ${NR_PAGES} hugepages but only ${ACTUAL_PAGES} were allocated. HBM capacity constraint may not match target (${TARGET_INT} MB)." >&2
    fi
fi
