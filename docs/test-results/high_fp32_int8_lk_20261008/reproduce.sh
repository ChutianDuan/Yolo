#!/usr/bin/env bash
# Run with: bash reproduce.sh /absolute/path/to/a/new/experiment-directory
set -euo pipefail
if [ "$#" -ne 1 ]; then
    echo "Usage: bash reproduce.sh NEW_OUTPUT_DIRECTORY" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(git -C "$script_dir" rev-parse --show-toplevel)
experiment_dir=$1
if [ -e "$experiment_dir" ]; then
    echo "Refusing to overwrite an existing experiment directory" >&2
    exit 2
fi
mkdir -p -- "$experiment_dir"
experiment_dir=$(cd -- "$experiment_dir" && pwd)
experiment_build=$(mktemp -d /tmp/high-fp32-int8-lk.XXXXXX)
export CUDA_VISIBLE_DEVICES=4,5
export LD_LIBRARY_PATH="/root/vcpkg/.toolchains/gcc15/lib:${LD_LIBRARY_PATH:-}"
cd -- "$repo/yolo_onnx_cpp"
cmake -S "$script_dir" -B "$experiment_build" -G Ninja \
    -DYOLO_REPO_ROOT="$repo" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/root/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_TARGET_TRIPLET=x64-linux-gcc15 \
    -DVCPKG_HOST_TRIPLET=x64-linux-gcc15 \
    -DVCPKG_OVERLAY_TRIPLETS=/root/vcpkg/custom-triplets \
    -DCMAKE_C_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=/root/vcpkg/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++ \
    > "$experiment_dir/configure.log" 2>&1
cmake --build "$experiment_build" --target high_lk_runner -j 4 \
    > "$experiment_dir/build.log" 2>&1
conda run --no-capture-output -n yolo python "$script_dir/prepare_int8.py" \
    --repo "$repo" --source "$repo/yolo_onnx_cpp/deploy/best.onnx" \
    --data "$repo/model/data/bdd100k_yolo_det/data.yaml" \
    --output-dir "$experiment_dir/models_u8s8" \
    > "$experiment_dir/quantization.log" 2>&1
conda run --no-capture-output -n yolo python "$script_dir/run_experiment.py" \
    --repo "$repo" --runner "$experiment_build/high_lk_runner" \
    --models "$experiment_dir/models_u8s8/quantization.json" \
    --manifest "$repo/docs/test-results/bdd100k_long_tail/gt_3videos_stride6_20260911/manifest.json" \
    --classes "$repo/yolo_onnx_cpp/deploy/classes.json" \
    --output-dir "$experiment_dir/measured" \
    > "$experiment_dir/experiment.log" 2>&1
conda run -n yolo python "$script_dir/write_report.py" \
    --summary "$experiment_dir/measured/summary.json" \
    --output "$experiment_dir/report.md"
echo "Report: $experiment_dir/report.md"
