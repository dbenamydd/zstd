#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euo pipefail
IFS=$'\n\t'

for f in *.{c,h,go}; do
    sed -i 's/ZSTD_/ZSTD144_/g' "$f"
    sed -i 's/ZBUFF_/ZBUFF144_/g' "$f"
    sed -i 's/HUF_/HUF144_/g' "$f"
    sed -i 's/FSE_/FSE144_/g' "$f"
    sed -i 's/divsufsort(/divsufsort144(/g' "$f"
    sed -i 's/divbwt(/divbwt144(/g' "$f"
done

for f in *.go; do
    sed -i 's/Zstd/Zstd144/g' "$f"
    sed -i 's/package zstd/package zstd144/g' "$f"
done
