"""Fine-tune local YOLO weights with fake quantization and export QAT INT8 QDQ."""
from __future__ import annotations

import argparse
import copy
import os
import sys
from functools import partial
from pathlib import Path

os.environ.setdefault("CUDA_VISIBLE_DEVICES", "4,5")
SCRIPT_DIR = Path(__file__).resolve().parent
sys.path = [str(SCRIPT_DIR.parent)] + [p for p in sys.path if not p or Path(p).resolve() != SCRIPT_DIR]

import torch
from ultralytics import YOLO
from ultralytics.models.yolo.detect import DetectionTrainer

from model.onnx import export_fp32_onnx, save_classes
from model.quantization import (
    add_common_arguments, calibrate_qat, inspect_int8, load_training_data,
    pack_qdq_weights, prepare_inputs, prepare_qat_model, set_observers,
    validate_classes, write_manifest,
)


class QATDetectionTrainer(DetectionTrainer):
    def __init__(self, *args, calibration_images, export_imgsz, freeze_observer_epoch, **kwargs):
        self.calibration_images = calibration_images
        self.export_imgsz = export_imgsz
        self.freeze_observer_epoch = freeze_observer_epoch
        super().__init__(*args, **kwargs)
        self.add_callback("on_pretrain_routine_end", self._use_current_model)
        self.add_callback("on_train_epoch_start", self._update_observers)

    def get_model(self, cfg=None, weights=None, verbose=True):
        model = super().get_model(cfg, weights, verbose).to(self.device)
        prepare_qat_model(model)
        calibrate_qat(model, self.calibration_images, self.export_imgsz)
        return model

    def get_dataset(self) -> dict:
        return load_training_data(Path(self.args.data).resolve())

    def _use_current_model(self, trainer) -> None:
        # Avoid EMA-averaging scales/minima/maxima independently of the weights.
        self.ema.enabled = False
        self.ema.ema = self.model

    def _update_observers(self, trainer) -> None:
        set_observers(self.model, self.epoch < self.freeze_observer_epoch)

    def save_model(self) -> bool:
        # Standard Ultralytics save_model casts observers and qparams to FP16.
        model = copy.deepcopy(self.model).cpu().float().eval()
        set_observers(model, False)
        checkpoint = {"model": model, "ema": None, "epoch": self.epoch,
                      "best_fitness": self.best_fitness, "train_args": vars(self.args),
                      "train_metrics": self.metrics}
        self.wdir.mkdir(parents=True, exist_ok=True)
        torch.save(checkpoint, self.last)
        if self.best_fitness == self.fitness:
            torch.save(checkpoint, self.best)
        return True

    def final_eval(self) -> None:
        # Epoch validation already selects best.pt; do not strip it to FP16.
        return


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    add_common_arguments(parser)
    parser.add_argument("--epochs", type=int, default=10)
    parser.add_argument("--freeze-observer-epoch", type=int, default=3,
                        help="0-based epoch at which observer updates stop; fake quant stays on")
    parser.add_argument("--batch", type=int, default=8)
    parser.add_argument("--lr", type=float, default=0.0001)
    parser.add_argument("--device", default="auto", help="auto, cpu or one visible GPU index; no DDP")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()
    if args.epochs <= 0 or args.batch <= 0 or args.lr <= 0 or args.workers < 0:
        parser.error("epochs, batch and lr must be positive; workers must be nonnegative")
    if not 0 <= args.freeze_observer_epoch < args.epochs:
        parser.error("freeze-observer-epoch must be in [0, epochs)")
    if args.device not in {"auto", "cpu"} and not args.device.isdecimal():
        parser.error("--device accepts auto, cpu or one visible GPU index")
    pt, data, images = prepare_inputs(args)
    load_training_data(data)
    model = YOLO(str(pt))
    validate_classes(model, data)
    out = Path(args.output_dir).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=False)
    imgsz = tuple(args.imgsz)
    # The source FP32 export is identical to the PTQ baseline, before QAT updates.
    names = export_fp32_onnx(pt, out / "fp32.onnx", imgsz, 13, False, "cpu")
    save_classes(names, out / "classes.json")
    device = ("0" if torch.cuda.is_available() else "cpu") if args.device == "auto" else args.device
    trainer = partial(QATDetectionTrainer, calibration_images=images,
                      export_imgsz=imgsz, freeze_observer_epoch=args.freeze_observer_epoch)
    model.train(
        trainer=trainer, data=str(data), project=str(out), name="train", exist_ok=False,
        epochs=args.epochs, batch=args.batch, imgsz=max(imgsz), device=device,
        workers=args.workers, seed=args.seed, optimizer="AdamW", lr0=args.lr,
        amp=False, half=False, rect=True, mosaic=0.0, mixup=0.0, close_mosaic=0,
        cache=False, plots=False, save=True, val=True, patience=0, warmup_epochs=0.0,
    )
    best = model.trainer.best
    if not best.is_file():
        raise RuntimeError("QAT training did not produce a validated best.pt checkpoint")
    export_fp32_onnx(best, out / "qat_int8.onnx", imgsz, 13, False, "cpu")
    pack_qdq_weights(out / "qat_int8.onnx")
    details = inspect_int8(out / "qat_int8.onnx", imgsz)
    details["qat_checkpoint"] = str(best)
    details["checkpoint_contains_fake_quant"] = True
    write_manifest(out, "qat", pt, data, images, args, details)
    print(f"QAT complete: {out / 'qat_int8.onnx'}")


if __name__ == "__main__":
    main()
