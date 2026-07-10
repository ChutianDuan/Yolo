# YOLO High/Low Optical Flow Comparison

- Video: `/home/ubuntu/YOLO/Readme/dynamic_onnx_flow_detections.mp4`
- Output directory: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953`
- Label source: `full_yolo` full-frame high-resolution YOLO.
- `high_low_yolo_flow` is a test-side fusion, not an integrated C++ tracker mode.

## Run Summary

| run | model | detect fps | async | frames | processed | detected | elapsed sec | display fps | avg tracks |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| full_yolo | high | 0.0 | False | 1204 | 1204 | 1204 | 376.012 | 3.202 | 7.382 |
| high_yolo_flow | high | 4.0 | False | 1204 | 176 | 176 | 65.916 | 18.266 | 7.067 |
| high_low_yolo_flow | high+low | 4.0 | False | 1204 | 176 | 176 | 65.916 | 18.266 | 7.532 |
| low_yolo_flow | low | 4.0 | False | 1204 | 186 | 186 | 28.284 | 42.569 | 5.664 |

## Quality vs Full YOLO Labels

### high_yolo_flow

| IoU | precision | recall | F1 | matches | labels | predictions | mean IoU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.3 | 0.883065 | 0.845410 | 0.863827 | 7514 | 8888 | 8509 | 0.890087 |
| 0.5 | 0.875426 | 0.838096 | 0.856355 | 7449 | 8888 | 8509 | 0.894226 |
| 0.7 | 0.808438 | 0.773965 | 0.790826 | 6879 | 8888 | 8509 | 0.917049 |

### high_low_yolo_flow

| IoU | precision | recall | F1 | matches | labels | predictions | mean IoU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.3 | 0.823335 | 0.840009 | 0.831588 | 7466 | 8888 | 9068 | 0.887163 |
| 0.5 | 0.814733 | 0.831233 | 0.822900 | 7388 | 8888 | 9068 | 0.892114 |
| 0.7 | 0.749007 | 0.764176 | 0.756516 | 6792 | 8888 | 9068 | 0.916267 |

### low_yolo_flow_internal

| IoU | precision | recall | F1 | matches | labels | predictions | mean IoU |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.3 | 0.728152 | 0.558731 | 0.632289 | 4966 | 8888 | 6820 | 0.803983 |
| 0.5 | 0.713050 | 0.547142 | 0.619175 | 4863 | 8888 | 6820 | 0.812034 |
| 0.7 | 0.602199 | 0.462084 | 0.522918 | 4107 | 8888 | 6820 | 0.846112 |

## Frame Sources

```json
{
  "full_yolo": {
    "detected": 1193,
    "interpolated": 11
  },
  "high_yolo_flow": {
    "detected": 176,
    "weak_tracked": 1028
  },
  "high_low_yolo_flow": {
    "high_flow_primary": 936,
    "low_flow_fill": 268
  },
  "low_yolo_flow": {
    "detected": 184,
    "weak_tracked": 1004,
    "interpolated": 16
  }
}
```

## Artifacts

- `full_yolo_response`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/full_yolo_infer_video_response.json`
- `high_yolo_flow_response`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/high_yolo_flow_infer_video_response.json`
- `low_yolo_flow_response`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/low_yolo_flow_infer_video_response.json`
- `high_low_yolo_flow_response`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/high_low_yolo_flow_infer_video_response.json`
- `full_yolo_pseudo_labels`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/full_yolo_pseudo_labels.jsonl`
- `full_yolo_video`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/full_yolo_detections.mp4`
- `high_yolo_flow_video`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/high_yolo_flow_detections.mp4`
- `high_low_yolo_flow_video`: `/home/ubuntu/YOLO/yolo_onnx_cpp/test_outputs/video_compare/dynamic_onnx_flow_detections_yolo_high_low_flow_20260630_003953/high_low_yolo_flow_detections.mp4`
