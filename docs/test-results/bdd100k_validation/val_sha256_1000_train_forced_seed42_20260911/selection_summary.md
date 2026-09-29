# BDD100K YOLO validation subset

Generated: 2026-09-11T06:33:30.997077+00:00

- Split: val
- Candidate images: 10000
- Selected images: 1000
- Selection method: sha256_rank_with_forced_class_positives
- Seed: 42
- Forced classes: train
- Forced images: 14

| Class | Candidate images | Candidate boxes | Selected images | Selected boxes |
|---|---:|---:|---:|---:|
| person | 3220 | 13262 | 337 | 1354 |
| rider | 515 | 649 | 46 | 52 |
| car | 9879 | 102506 | 988 | 10174 |
| truck | 2689 | 4245 | 263 | 415 |
| bus | 1242 | 1597 | 121 | 168 |
| train | 14 | 15 | 14 | 15 |
| motor | 334 | 452 | 31 | 53 |
| bike | 578 | 1007 | 52 | 95 |
| traffic light | 5653 | 26885 | 554 | 2618 |
| traffic sign | 8221 | 34908 | 814 | 3508 |

The subset is independent from the training split. It is deterministic but not a pure random sample because every positive image for each forced class is retained.
