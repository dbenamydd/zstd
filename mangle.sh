#!/bin/bash
# http://redsymbol.net/articles/unofficial-bash-strict-mode/
set -euox pipefail
IFS=$'\n\t'

for f in *.{c,h,go}; do
    sed -i 's/ZSTDv0/ZSTD144v0/g' "$f"
    sed -i 's/BIT_/BIT144_/g' "$f"
    sed -i 's/BITv0_/BIT144v0_/g' "$f"
    sed -i 's/COVER_/COVER144_/g' "$f"
    sed -i 's/ERR_/ERR144_/g' "$f"
    sed -i 's/FASTCOVER_/FASTCOVER144_/g' "$f"
    sed -i 's/FSE_/FSE144_/g' "$f"
    sed -i 's/FSEv0/FSE144v0/g' "$f"
    sed -i 's/HIST_/HIST144_/g' "$f"
    sed -i 's/HUF_/HUF144_/g' "$f"
    sed -i 's/HUFv0/HUF144v0/g' "$f"
    sed -i 's/LL_/LL144_/g' "$f"
    sed -i 's/ML_/ML144_/g' "$f"
    sed -i 's/OF_/OF144_/g' "$f"
    sed -i 's/POOL_/POOL144_/g' "$f"
    sed -i 's/XXH/XXH_32/g' "$f"
    sed -i 's/XXH32/XXH144_32/g' "$f"
    sed -i 's/XXH64/XXH144_64/g' "$f"
    sed -i 's/ZBUFF_/ZBUFF144_/g' "$f"
    sed -i 's/ZBUFFv0/ZBUFF144v0/g' "$f"
    sed -i 's/ZDICT_/ZDICT144_/g' "$f"
    sed -i 's/ZSTD_/ZSTD144_/g' "$f"
    sed -i 's/ZSTDMT_/ZSTDMT144_/g' "$f"
    sed -i 's/divsufsort(/divsufsort144(/g' "$f"
    sed -i 's/divbwt(/divbwt144(/g' "$f"
done

for f in *.go; do
    sed -i 's/Zstd/Zstd144/g' "$f"
    sed -i 's/package zstd/package zstd144/g' "$f"
done
