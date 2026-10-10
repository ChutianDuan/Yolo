#!/usr/bin/env bash
# Reuse existing models; never overwrite prior experiment outputs.
set -euo pipefail
if [ "$#" -ne 1 ] || [ -e "$1" ]; then
    echo "Usage: bash reproduce.sh NEW_OUTPUT_DIRECTORY (must not exist)" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(git -C "$script_dir" rev-parse --show-toplevel)
experiment_dir=$1
experiment_build=$(mktemp -d /tmp/high-stage-cpu.XXXXXX)
export CUDA_VISIBLE_DEVICES=4,5
export LD_LIBRARY_PATH="/root/vcpkg/.toolchains/gcc15/lib:${LD_LIBRARY_PATH:-}"
conda run -n yolo python "$script_dir/instrument_video.py" \
    --repo "$repo" --output-dir "$experiment_build/instrumented"
cd -- "$repo/yolo_onnx_cpp"
cmake -S "$script_dir" -B "$experiment_build/build" -G Ninja \
    -DYOLO_REPO_ROOT="$repo" -DCMAKE_BUILD_TYPE=Release \
    -DPROFILED_VIDEO_SOURCE="$experiment_build/instrumented/profiled_video.cpp" \
    -DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-linux-gcc15 \
    -DVCPKG_HOST_TRIPLET=x64-linux-gcc15 \
    -DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets \
    -DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++ \
    > "$experiment_build/configure.log" 2>&1
cmake --build "$experiment_build/build" --target high_lk_runner -j 4 \
    > "$experiment_build/build.log" 2>&1
conda run --no-capture-output -n yolo python "$script_dir/run_experiment.py" \
    --repo "$repo" --runner "$experiment_build/build/high_lk_runner" \
    --baseline "$repo/docs/test-results/high_fp32_int8_lk_20261008" \
    --output-dir "$experiment_dir"
conda run -n yolo python "$script_dir/plot_results.py" \
    --summary "$experiment_dir/summary.json" --output-dir "$experiment_dir"
cp -n -- "$experiment_build/configure.log" "$experiment_build/build.log" "$experiment_dir/"
cp -rn -- "$experiment_build/instrumented" "$experiment_dir/"
