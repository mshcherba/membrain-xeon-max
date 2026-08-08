#!/bin/bash
# ==============================================================================
# System Environment Preparation Script for Benchmarking on Intel Xeon Max.
#
# Description:
#   Configures kernel, CPU, and memory system parameters to prepare the host
#   for reproducible and isolated benchmark evaluations:
#     1. Disables Swapping (swapoff -a & vm.swappiness=0)
#     2. Disables Transparent Huge Pages (THP)
#     3. Disables automatic NUMA balancing
#     4. Disables SMT / Hyperthreading
#     5. Sets CPU frequency scaling governor to 'performance'
#
# Usage:
#   sudo ./scripts/prepare_system.sh
#
# Requirements:
#   Must be run with root privileges (sudo).
# ==============================================================================

set -e

if [ "$EUID" -ne 0 ]; then
    echo "[ERROR] Run with sudo: sudo $0"
    exit 1
fi

echo "[1/5] Disabling swapping..."
swapoff -a
sysctl -w vm.swappiness=0 > /dev/null

echo "[2/5] Disabling Transparent Huge Pages (THP)..."
echo never > /sys/kernel/mm/transparent_hugepage/enabled
echo never > /sys/kernel/mm/transparent_hugepage/defrag

echo "[3/5] Disabling NUMA balancing..."
sysctl -w kernel.numa_balancing=0 > /dev/null

echo "[4/5] Disabling SMT..."
echo off > /sys/devices/system/cpu/smt/control

echo "[5/5] Setting CPU scaling governor to performance..."
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$gov" 2>/dev/null || true
done

echo "System preparation complete."
