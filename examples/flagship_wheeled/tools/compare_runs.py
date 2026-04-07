#!/usr/bin/env python3

"""Compare normalised flagship runs across backends."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List, Tuple


SCHEMA_VERSION = "flagship.normalised_run.v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare normalised flagship run outputs across backends."
    )
    parser.add_argument("runs", nargs="+", help="Normalised run JSON files.")
    parser.add_argument(
        "--max-ticks",
        type=int,
        default=None,
        help="Optional debug-only shared prefix window for aligned comparison details.",
    )
    parser.add_argument("--json-out", help="Optional machine-readable comparison output path.")
    return parser.parse_args()


def load_normalised_run(path: Path) -> Dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: expected JSON object")
    if payload.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"{path}: unsupported schema_version: {payload.get('schema_version')}")
    if not isinstance(payload.get("ticks"), list):
        raise ValueError(f"{path}: missing ticks list")
    return payload


def safe_float(value: Any, *, default: float = 0.0) -> float:
    try:
        out = float(value)
    except (TypeError, ValueError):
        return default
    if not math.isfinite(out):
        return default
    return out


def safe_bool(value: Any) -> bool:
    return bool(value)


def aligned_pairs(
    lhs: Iterable[Dict[str, Any]],
    rhs: Iterable[Dict[str, Any]],
    *,
    max_ticks: int | None = None,
) -> List[Tuple[Dict[str, Any], Dict[str, Any]]]:
    lhs_list = list(lhs)
    rhs_list = list(rhs)
    count = min(len(lhs_list), len(rhs_list))
    if max_ticks is not None:
        count = min(count, max(0, int(max_ticks)))
    return list(zip(lhs_list[:count], rhs_list[:count]))


def branch_sequence(ticks: Iterable[Dict[str, Any]]) -> List[str]:
    out: List[str] = []
    last: str | None = None
    for tick in ticks:
        branch = str(tick.get("branch", "unknown"))
        if branch != last:
            out.append(branch)
            last = branch
    return out


def branch_set(ticks: Iterable[Dict[str, Any]]) -> set[str]:
    return {str(tick.get("branch", "unknown")) for tick in ticks}


def branch_histogram(ticks: Iterable[Dict[str, Any]]) -> Dict[str, int]:
    hist: Dict[str, int] = {}
    for tick in ticks:
        branch = str(tick.get("branch", "unknown"))
        hist[branch] = hist.get(branch, 0) + 1
    return dict(sorted(hist.items()))


def progress_summary(run: Dict[str, Any]) -> Dict[str, Any]:
    summary = run.get("summary", {})
    initial_goal_dist = safe_float(summary.get("initial_goal_dist"), default=math.inf)
    final_goal_dist = safe_float(summary.get("final_goal_dist"), default=math.inf)
    reduction_abs = initial_goal_dist - final_goal_dist
    reduction_ratio = 0.0
    if math.isfinite(initial_goal_dist) and initial_goal_dist > 1.0e-9:
        reduction_ratio = reduction_abs / initial_goal_dist
    return {
        "initial_goal_dist": initial_goal_dist,
        "final_goal_dist": final_goal_dist,
        "reduction_abs": reduction_abs,
        "reduction_ratio": reduction_ratio,
    }


def jaccard_similarity(lhs: set[str], rhs: set[str]) -> float:
    union = lhs.union(rhs)
    if not union:
        return 1.0
    return len(lhs.intersection(rhs)) / len(union)


def branch_agreement(pairs: List[Tuple[Dict[str, Any], Dict[str, Any]]]) -> Dict[str, Any]:
    if not pairs:
        return {"aligned_ticks": 0, "matches": 0, "agreement_ratio": 0.0}
    matches = sum(1 for lhs, rhs in pairs if lhs.get("branch") == rhs.get("branch"))
    return {
        "aligned_ticks": len(pairs),
        "matches": matches,
        "agreement_ratio": matches / len(pairs),
    }


def mean_abs_difference(
    pairs: List[Tuple[Dict[str, Any], Dict[str, Any]]],
    lhs_key: str,
    rhs_key: str,
) -> float:
    if not pairs:
        return 0.0
    total = 0.0
    for lhs, rhs in pairs:
        total += abs(safe_float(lhs.get(lhs_key)) - safe_float(rhs.get(rhs_key)))
    return total / len(pairs)


def action_mae(pairs: List[Tuple[Dict[str, Any], Dict[str, Any]]]) -> Dict[str, Any]:
    compared = 0
    linear_sum = 0.0
    angular_sum = 0.0
    for lhs, rhs in pairs:
        lhs_action = lhs.get("shared_action")
        rhs_action = rhs.get("shared_action")
        if not isinstance(lhs_action, dict) or not isinstance(rhs_action, dict):
            continue
        linear_sum += abs(safe_float(lhs_action.get("linear_x")) - safe_float(rhs_action.get("linear_x")))
        angular_sum += abs(safe_float(lhs_action.get("angular_z")) - safe_float(rhs_action.get("angular_z")))
        compared += 1
    if compared == 0:
        return {"compared_ticks": 0, "linear_x_mae": None, "angular_z_mae": None}
    return {
        "compared_ticks": compared,
        "linear_x_mae": linear_sum / compared,
        "angular_z_mae": angular_sum / compared,
    }


def compare_pair(baseline: Dict[str, Any], candidate: Dict[str, Any], *, max_ticks: int | None = None) -> Dict[str, Any]:
    lhs_ticks = baseline["ticks"]
    rhs_ticks = candidate["ticks"]
    lhs_final = baseline.get("final", {})
    rhs_final = candidate.get("final", {})
    lhs_progress = progress_summary(baseline)
    rhs_progress = progress_summary(candidate)
    lhs_branch_seq = branch_sequence(lhs_ticks)
    rhs_branch_seq = branch_sequence(rhs_ticks)
    lhs_branch_set = branch_set(lhs_ticks)
    rhs_branch_set = branch_set(rhs_ticks)
    result = {
        "baseline_backend": baseline.get("backend"),
        "candidate_backend": candidate.get("backend"),
        "baseline_run_id": baseline.get("run_id"),
        "candidate_run_id": candidate.get("run_id"),
        "same_goal_reached": bool(lhs_final.get("goal_reached")) == bool(rhs_final.get("goal_reached")),
        "same_final_status": lhs_final.get("status") == rhs_final.get("status"),
        "progress": {
            "baseline": lhs_progress,
            "candidate": rhs_progress,
            "reduction_ratio_delta": abs(lhs_progress["reduction_ratio"] - rhs_progress["reduction_ratio"]),
        },
        "branches": {
            "baseline_sequence": lhs_branch_seq,
            "candidate_sequence": rhs_branch_seq,
            "same_sequence": lhs_branch_seq == rhs_branch_seq,
            "baseline_histogram": branch_histogram(lhs_ticks),
            "candidate_histogram": branch_histogram(rhs_ticks),
            "family_overlap": jaccard_similarity(lhs_branch_set, rhs_branch_set),
            "terminal_branch_match": lhs_final.get("branch") == rhs_final.get("branch"),
        },
        "baseline_final": lhs_final,
        "candidate_final": rhs_final,
        "baseline_tick_count": len(lhs_ticks),
        "candidate_tick_count": len(rhs_ticks),
    }
    if max_ticks is not None:
        pairs = aligned_pairs(lhs_ticks, rhs_ticks, max_ticks=max_ticks)
        result["aligned_debug"] = {
            "aligned_tick_count": len(pairs),
            "branch_trace": branch_agreement(pairs),
            "goal_dist_mae": mean_abs_difference(pairs, "goal_dist", "goal_dist"),
            "shared_action": action_mae(pairs),
        }
    return result


def print_report(baseline: Dict[str, Any], comparisons: List[Dict[str, Any]]) -> None:
    baseline_progress = progress_summary(baseline)
    print(
        f"baseline: {baseline.get('backend')} ({baseline.get('run_id')}) "
        f"ticks={baseline.get('summary', {}).get('tick_count')} "
        f"goal_reached={safe_bool(baseline.get('final', {}).get('goal_reached'))} "
        f"progress_ratio={baseline_progress['reduction_ratio']:.3f}"
    )
    for result in comparisons:
        progress = result["progress"]
        branches = result["branches"]
        print(
            f"compare: {result['candidate_backend']} ({result['candidate_run_id']}) "
            f"same_goal_reached={result['same_goal_reached']} "
            f"same_final_status={result['same_final_status']}"
        )
        print(
            f"  progress_ratio.baseline={progress['baseline']['reduction_ratio']:.3f} "
            f"candidate={progress['candidate']['reduction_ratio']:.3f} "
            f"delta={progress['reduction_ratio_delta']:.3f}"
        )
        print(
            f"  branch_family_overlap={branches['family_overlap']:.3f} "
            f"same_sequence={branches['same_sequence']} "
            f"terminal_branch_match={branches['terminal_branch_match']}"
        )
        print(f"  branch_sequence.baseline={branches['baseline_sequence']}")
        print(f"  branch_sequence.candidate={branches['candidate_sequence']}")
        print(
            f"  final.baseline={baseline.get('final', {}).get('status')} "
            f"candidate={result['candidate_final'].get('status')}"
        )
        aligned_debug = result.get("aligned_debug")
        if isinstance(aligned_debug, dict):
            shared_action = aligned_debug["shared_action"]
            linear_text = (
                "n/a"
                if shared_action["linear_x_mae"] is None
                else f"{shared_action['linear_x_mae']:.4f}"
            )
            angular_text = (
                "n/a"
                if shared_action["angular_z_mae"] is None
                else f"{shared_action['angular_z_mae']:.4f}"
            )
            print(
                f"  debug.aligned_branch_agreement={aligned_debug['branch_trace']['agreement_ratio']:.3f} "
                f"({aligned_debug['branch_trace']['matches']}/{aligned_debug['branch_trace']['aligned_ticks']})"
            )
            print(f"  debug.goal_dist_mae={aligned_debug['goal_dist_mae']:.4f}")
            print(f"  debug.shared_action_mae.linear_x={linear_text}")
            print(f"  debug.shared_action_mae.angular_z={angular_text}")


def main() -> int:
    args = parse_args()
    runs = [load_normalised_run(Path(path)) for path in args.runs]
    baseline = runs[0]
    comparisons = [compare_pair(baseline, run, max_ticks=args.max_ticks) for run in runs[1:]]

    report = {
        "schema_version": "flagship.comparison.v1",
        "max_ticks": args.max_ticks,
        "baseline": {
            "backend": baseline.get("backend"),
            "run_id": baseline.get("run_id"),
            "summary": baseline.get("summary"),
            "final": baseline.get("final"),
        },
        "comparisons": comparisons,
    }

    print_report(baseline, comparisons)
    if args.json_out:
        output_path = Path(args.json_out)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(report, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
