from ultralytics import YOLO
from pathlib import Path
import shutil
import json

ROOT = Path(__file__).resolve().parent
IMAGE_WIDTH = 640
IMAGE_HEIGHT = 256
IMG_SIZE = (IMAGE_HEIGHT, IMAGE_WIDTH)

PT_PATH = "/home/ubuntu/YOLO/model/runs/segment/severstal_yolo11n_seg_pretrained/weights/best.pt"
DEPLOY_DIR = ROOT.parent / "yolo_onnx_cpp" / "deploy"
DEPLOY_DIR.mkdir(parents=True, exist_ok=True)

model = YOLO(PT_PATH)

onnx_path = model.export(
    format="onnx",
    imgsz=IMG_SIZE,
    opset=12,
    simplify=True,
    dynamic=False,
    half=False,
    nms=False,
    batch=1,
    device="cpu",
)

onnx_path = Path(onnx_path)

target_onnx = DEPLOY_DIR / "best.onnx"
shutil.copy2(onnx_path, target_onnx)

# 保存类别名，C++ 后处理需要用
with open(DEPLOY_DIR / "classes.json", "w", encoding="utf-8") as f:
    json.dump(model.names, f, ensure_ascii=False, indent=2)

print(f"ONNX exported to: {target_onnx}")
print(f"Classes saved to: {DEPLOY_DIR / 'classes.json'}")