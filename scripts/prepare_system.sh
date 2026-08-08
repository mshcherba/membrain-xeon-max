#!/bin/bash
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
