#!/usr/bin/env python3
"""
Train a multi-label linear score model for gatewayd offline AI.

PC usage:
    py -3 -m pip install scikit-learn
    py -3 offline_ai_train.py ai_dataset/training.csv --out offline_ai_model.json

The exported JSON is consumed directly by gatewayd C++ code; scikit-learn is
not required on the board.
"""

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Dict, List, Tuple

from offline_ai_dataset import FEATURE_COLUMNS, RISK_TYPES

DEVICE_TYPES = {
    "meter_voltage_risk": ["single_phase_meter"],
    "meter_current_risk": ["single_phase_meter"],
    "meter_energy_risk": ["single_phase_meter"],
    "env_risk": ["env_sensor"],
    "dtu_stability_risk": ["dtu_node"],
    "relay_state_risk": ["relay_device"],
}


def device_type_for_id(device_id: str) -> str:
    if device_id.startswith("METER_"):
        return "single_phase_meter"
    if device_id.startswith("ENV_"):
        return "env_sensor"
    if device_id.startswith("RELAY_"):
        return "relay_device"
    if device_id.startswith("DTU_"):
        return "dtu_node"
    return "gateway"


def load_training(path: Path) -> Tuple[List[Dict[str, str]], List[List[float]]]:
    rows: List[Dict[str, str]] = []
    features: List[List[float]] = []
    with path.open("r", newline="", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            rows.append(row)
            features.append([float(row.get(name, "0") or 0.0) for name in FEATURE_COLUMNS])
    return rows, features


def compute_scaler(features: List[List[float]]) -> Tuple[List[float], List[float]]:
    count = max(1, len(features))
    means = [sum(row[idx] for row in features) / count for idx in range(len(FEATURE_COLUMNS))]
    scales: List[float] = []
    for idx, mean in enumerate(means):
        variance = sum((row[idx] - mean) ** 2 for row in features) / count
        scale = math.sqrt(variance)
        scales.append(scale if scale > 1e-9 else 1.0)
    return means, scales


def normalize_features(features: List[List[float]], means: List[float], scales: List[float]) -> List[List[float]]:
    return [
        [(value - means[idx]) / scales[idx] for idx, value in enumerate(row)]
        for row in features
    ]


def train_builtin_head(x_rows: List[List[float]], y: List[int],
                       max_iter: int) -> Tuple[List[float], float]:
    weights = [0.0 for _ in FEATURE_COLUMNS]
    bias = 0.0
    positives = max(1, sum(y))
    negatives = max(1, len(y) - sum(y))
    pos_weight = len(y) / (2.0 * positives)
    neg_weight = len(y) / (2.0 * negatives)
    lr = 0.08

    for _ in range(max_iter):
        grad_w = [0.0 for _ in FEATURE_COLUMNS]
        grad_b = 0.0
        for row, target in zip(x_rows, y):
            z = bias + sum(weight * value for weight, value in zip(weights, row))
            pred = 1.0 / (1.0 + math.exp(-max(-35.0, min(35.0, z))))
            sample_weight = pos_weight if target else neg_weight
            err = (pred - target) * sample_weight
            for idx, value in enumerate(row):
                grad_w[idx] += err * value
            grad_b += err
        denom = max(1.0, float(len(y)))
        for idx in range(len(weights)):
            weights[idx] -= lr * grad_w[idx] / denom
        bias -= lr * grad_b / denom

    return weights, bias


def train_model_builtin(args: argparse.Namespace) -> Dict[str, object]:
    rows, features = load_training(Path(args.training_csv))
    if not rows:
        raise RuntimeError("training CSV is empty")

    means, scales = compute_scaler(features)
    x_scaled = normalize_features(features, means, scales)
    model: Dict[str, object] = {
        "version": args.version,
        "mode": "linear_score",
        "feature_names": FEATURE_COLUMNS,
        "feature_mean": {name: float(value) for name, value in zip(FEATURE_COLUMNS, means)},
        "feature_scale": {name: float(value) for name, value in zip(FEATURE_COLUMNS, scales)},
        "heads": {},
        "trainer": "builtin_logistic",
    }

    for risk in RISK_TYPES:
        allowed_types = set(DEVICE_TYPES[risk])
        indices = [
            idx for idx, row in enumerate(rows)
            if device_type_for_id(row["device_id"]) in allowed_types
        ]
        if not indices:
            continue
        y = [int(float(rows[idx].get(f"label_{risk}", "0") or 0)) for idx in indices]
        positives = sum(y)
        if positives == 0 or positives == len(y):
            bias = 8.0 if positives else -8.0
            weights = [0.0 for _ in FEATURE_COLUMNS]
        else:
            weights, bias = train_builtin_head([x_scaled[idx] for idx in indices], y, args.max_iter)

        model["heads"][risk] = {
            "device_types": DEVICE_TYPES[risk],
            "weights": {name: float(value) for name, value in zip(FEATURE_COLUMNS, weights)},
            "bias": float(bias),
            "positive_samples": positives,
            "total_samples": len(y),
        }

    return model


def train_model(args: argparse.Namespace) -> Dict[str, object]:
    try:
        from sklearn.linear_model import LogisticRegression
        from sklearn.preprocessing import StandardScaler
    except ImportError as exc:
        if args.require_sklearn:
            raise RuntimeError("scikit-learn is required on PC: py -3 -m pip install scikit-learn") from exc
        print("scikit-learn not found, using builtin logistic fallback")
        return train_model_builtin(args)

    rows, features = load_training(Path(args.training_csv))
    if not rows:
        raise RuntimeError("training CSV is empty")

    scaler = StandardScaler()
    x_scaled = scaler.fit_transform(features)
    model: Dict[str, object] = {
        "version": args.version,
        "mode": "linear_score",
        "feature_names": FEATURE_COLUMNS,
        "feature_mean": {name: float(value) for name, value in zip(FEATURE_COLUMNS, scaler.mean_)},
        "feature_scale": {name: float(value) if abs(float(value)) > 1e-9 else 1.0
                          for name, value in zip(FEATURE_COLUMNS, scaler.scale_)},
        "heads": {},
        "trainer": "sklearn_logistic_regression",
    }

    for risk in RISK_TYPES:
        allowed_types = set(DEVICE_TYPES[risk])
        indices = [
            idx for idx, row in enumerate(rows)
            if device_type_for_id(row["device_id"]) in allowed_types
        ]
        if not indices:
            continue

        y = [int(float(rows[idx].get(f"label_{risk}", "0") or 0)) for idx in indices]
        positives = sum(y)
        if positives == 0 or positives == len(y):
            # Keep a deterministic head even when a tiny test dataset lacks a class.
            bias = 8.0 if positives else -8.0
            weights = {name: 0.0 for name in FEATURE_COLUMNS}
        else:
            clf = LogisticRegression(
                class_weight="balanced",
                max_iter=args.max_iter,
                solver="liblinear",
                random_state=7,
            )
            clf.fit(x_scaled[indices], y)
            weights = {
                name: float(value)
                for name, value in zip(FEATURE_COLUMNS, clf.coef_[0])
            }
            bias = float(clf.intercept_[0])

        model["heads"][risk] = {
            "device_types": DEVICE_TYPES[risk],
            "weights": weights,
            "bias": bias,
            "positive_samples": positives,
            "total_samples": len(y),
        }

    return model


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train Gateway offline AI linear score model.")
    parser.add_argument("training_csv", help="CSV generated by offline_ai_dataset.py")
    parser.add_argument("--out", default="offline_ai_model.json", help="Output model JSON")
    parser.add_argument("--version", default="offline_ai_linear_score_v1")
    parser.add_argument("--max-iter", type=int, default=500)
    parser.add_argument("--require-sklearn", action="store_true",
                        help="Fail instead of using the built-in fallback trainer")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    model = train_model(args)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8") as fp:
        json.dump(model, fp, ensure_ascii=True, indent=2)
        fp.write("\n")
    print(json.dumps({
        "model_path": str(out),
        "heads": sorted(model["heads"].keys()),
        "feature_count": len(FEATURE_COLUMNS),
    }, ensure_ascii=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
