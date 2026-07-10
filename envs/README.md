# Environment specs

This directory records the intended Conda environments for the current machine.

Use separate environments:

- `yolo.yml`: training, validation, ONNX export, quantization and Python experiment scripts. It uses PyTorch `2.5.1+cu121`, NumPy `2.2.6` and Python OpenCV `4.13.0`.
- `rag-api.yml`: RAG/FastAPI service dependencies. It uses PyTorch `2.6.0+cu124`, NumPy `1.26.4`, `sentence-transformers 3.0.1` and `datasets 2.21.0`.

Do not merge these two environments. Their CUDA wheel lines and NumPy versions are intentionally different.

Create or update environments with:

```bash
conda env create -f envs/yolo.yml
conda env create -f envs/rag-api.yml
```

For the existing environments on this host, validate with:

```bash
conda run -n yolo python -m pip check
conda run -n rag-api python -m pip check
```

When running from the repository root, `rag-api` needs the real Hugging Face `datasets` package installed. Otherwise the local `datasets/` data directory can be imported as a namespace package and break `sentence_transformers`.
