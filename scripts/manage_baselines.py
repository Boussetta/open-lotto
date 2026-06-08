# SPDX-FileCopyrightText: 2025 Wissem Boussetta
# SPDX-License-Identifier: MIT

#!/usr/bin/env python3
"""
Historical baseline manager for performance tracking.

Manages historical performance baselines, detects trends, and generates reports.
"""

import json
import sys
import os
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import statistics


class BaselineHistory:
    """Manage historical performance baselines."""

    def __init__(self, history_dir: str = ".github/benchmarks"):
        """Initialize baseline history manager.
        
        Args:
            history_dir: Directory to store historical baselines
        """
        self.history_dir = Path(history_dir)
        self.history_dir.mkdir(parents=True, exist_ok=True)
        self.history_file = self.history_dir / "history.json"

    def load_history(self) -> Dict:
        """Load historical baselines from disk.
        
        Returns:
            Dictionary mapping timestamps to baseline records
        """
        if not self.history_file.exists():
            return {}
        
        with open(self.history_file, 'r') as f:
            return json.load(f)

    def save_history(self, history: Dict) -> None:
        """Save historical baselines to disk.
        
        Args:
            history: Dictionary of historical baselines
        """
        with open(self.history_file, 'w') as f:
            json.dump(history, f, indent=2)

    def add_baseline(self, benchmark_file: str, commit_hash: Optional[str] = None,
                     metadata: Optional[Dict] = None) -> str:
        """Add a new baseline to history.
        
        Args:
            benchmark_file: Path to benchmark results JSON file
            commit_hash: Git commit hash (auto-detected if not provided)
            metadata: Optional metadata (architecture, compiler, etc.)
            
        Returns:
            Timestamp of the added baseline
        """
        # Load benchmark results
        with open(benchmark_file, 'r') as f:
            results = json.load(f)
        
        # Use existing timestamp or current time
        timestamp = results.get('timestamp', datetime.utcnow().isoformat() + 'Z')
        
        # Auto-detect commit hash if not provided
        if commit_hash is None:
            try:
                import subprocess
                commit_hash = subprocess.check_output(
                    ['git', 'rev-parse', 'HEAD'],
                    text=True
                ).strip()
            except Exception:
                commit_hash = "unknown"
        
        # Load history
        history = self.load_history()
        
        # Store baseline with metadata
        baseline_entry = {
            'timestamp': timestamp,
            'commit': commit_hash,
            'benchmarks': results.get('benchmarks', {}),
            'metadata': metadata or {}
        }
        
        history[timestamp] = baseline_entry
        self.save_history(history)
        
        return timestamp

    def get_trend(self, metric: str, limit: int = 10) -> List[Tuple[str, float]]:
        """Get performance trend for a specific metric.
        
        Args:
            metric: Metric name (e.g., 'lotto', 'eurojackpot', 'rng')
            limit: Maximum number of recent baselines to return
            
        Returns:
            List of (timestamp, value) tuples, sorted chronologically
        """
        history = self.load_history()
        
        trend = []
        for timestamp in sorted(history.keys())[-limit:]:
            entry = history[timestamp]
            if metric in entry.get('benchmarks', {}):
                value = entry['benchmarks'][metric].get('value', None)
                if value is not None:
                    trend.append((timestamp, float(value)))
        
        return trend

    def detect_degradation(self, metric: str, threshold_percent: float = 5.0) -> Optional[Dict]:
        """Detect gradual performance degradation.
        
        Args:
            metric: Metric name to analyze
            threshold_percent: Degradation threshold percentage
            
        Returns:
            Dictionary with degradation info or None if no degradation
        """
        trend = self.get_trend(metric, limit=20)
        
        if len(trend) < 3:
            return None
        
        # Split into two halves: older and recent
        mid = len(trend) // 2
        older = [v for _, v in trend[:mid]]
        recent = [v for _, v in trend[mid:]]
        
        if not older or not recent:
            return None
        
        older_avg = statistics.mean(older)
        recent_avg = statistics.mean(recent)
        
        degradation_pct = ((older_avg - recent_avg) / older_avg) * 100
        
        if degradation_pct > threshold_percent:
            return {
                'metric': metric,
                'degradation_percent': degradation_pct,
                'older_avg': older_avg,
                'recent_avg': recent_avg,
                'older_count': len(older),
                'recent_count': len(recent),
                'trend': trend[-5:]  # Last 5 for context
            }
        
        return None

    def generate_report(self, metrics: Optional[List[str]] = None) -> str:
        """Generate a performance report.
        
        Args:
            metrics: Specific metrics to include (all if None)
            
        Returns:
            Formatted report string
        """
        history = self.load_history()
        
        if not history:
            return "No baseline history available.\n"
        
        if metrics is None:
            # Auto-detect metrics from first entry
            first_entry = next(iter(history.values()))
            metrics = list(first_entry.get('benchmarks', {}).keys())
        
        report = "# Performance Baseline History Report\n\n"
        report += f"**Generated:** {datetime.utcnow().isoformat()}Z\n"
        report += f"**Total Baselines:** {len(history)}\n\n"
        
        for metric in metrics:
            trend = self.get_trend(metric)
            if not trend:
                continue
            
            report += f"## {metric.upper()}\n\n"
            report += f"**Current:** {trend[-1][1]:.1f}\n"
            report += f"**Oldest:** {trend[0][1]:.1f}\n"
            
            if len(trend) > 1:
                change_pct = ((trend[-1][1] - trend[0][1]) / trend[0][1]) * 100
                trend_direction = "📈" if change_pct > 0 else "📉"
                report += f"**Trend:** {trend_direction} {change_pct:+.1f}%\n"
            
            degradation = self.detect_degradation(metric)
            if degradation:
                report += f"\n⚠️  **Performance Degradation Detected**\n"
                report += f"- Degradation: {degradation['degradation_percent']:.1f}%\n"
                report += f"- Historical Avg: {degradation['older_avg']:.1f}\n"
                report += f"- Recent Avg: {degradation['recent_avg']:.1f}\n"
            
            report += "\n"
        
        return report

    def get_stats(self, metric: str) -> Optional[Dict]:
        """Get statistical summary for a metric.
        
        Args:
            metric: Metric name
            
        Returns:
            Dictionary with min, max, mean, stddev
        """
        trend = self.get_trend(metric, limit=100)
        
        if len(trend) < 2:
            return None
        
        values = [v for _, v in trend]
        
        return {
            'metric': metric,
            'count': len(values),
            'min': min(values),
            'max': max(values),
            'mean': statistics.mean(values),
            'median': statistics.median(values),
            'stdev': statistics.stdev(values) if len(values) > 1 else 0.0
        }


def main():
    """CLI interface for baseline management."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Manage historical performance baselines')
    parser.add_argument('--history-dir', default='.github/benchmarks',
                        help='Directory for baseline history')
    
    subparsers = parser.add_subparsers(dest='command', help='Command to run')
    
    # Add baseline command
    add_parser = subparsers.add_parser('add', help='Add new baseline')
    add_parser.add_argument('benchmark_file', help='Path to benchmark results JSON')
    add_parser.add_argument('--commit', help='Git commit hash')
    add_parser.add_argument('--arch', help='Architecture')
    
    # Report command
    report_parser = subparsers.add_parser('report', help='Generate performance report')
    report_parser.add_argument('--metrics', nargs='+', help='Specific metrics to include')
    
    # Trend command
    trend_parser = subparsers.add_parser('trend', help='Show metric trend')
    trend_parser.add_argument('metric', help='Metric name')
    trend_parser.add_argument('--limit', type=int, default=10, help='Number of entries')
    
    # Degradation command
    degrade_parser = subparsers.add_parser('degrade', help='Check for degradation')
    degrade_parser.add_argument('metric', help='Metric name')
    degrade_parser.add_argument('--threshold', type=float, default=5.0,
                                help='Degradation threshold %')
    
    # Stats command
    stats_parser = subparsers.add_parser('stats', help='Show metric statistics')
    stats_parser.add_argument('metric', help='Metric name')
    
    args = parser.parse_args()
    
    manager = BaselineHistory(args.history_dir)
    
    if args.command == 'add':
        metadata = {'architecture': args.arch} if args.arch else None
        timestamp = manager.add_baseline(args.benchmark_file, args.commit, metadata)
        print(f"✓ Baseline added: {timestamp}")
    
    elif args.command == 'report':
        report = manager.generate_report(args.metrics)
        print(report)
    
    elif args.command == 'trend':
        trend = manager.get_trend(args.metric, args.limit)
        if not trend:
            print(f"No data for metric: {args.metric}")
        else:
            print(f"Trend for {args.metric} ({len(trend)} entries):")
            for ts, value in trend:
                print(f"  {ts}: {value:.1f}")
    
    elif args.command == 'degrade':
        degradation = manager.detect_degradation(args.metric, args.threshold)
        if degradation:
            print(f"⚠️  Performance degradation detected: {degradation['degradation_percent']:.1f}%")
            print(f"  Historical average: {degradation['older_avg']:.1f}")
            print(f"  Recent average: {degradation['recent_avg']:.1f}")
        else:
            print(f"No significant degradation detected for {args.metric}")
    
    elif args.command == 'stats':
        stats = manager.get_stats(args.metric)
        if not stats:
            print(f"No data for metric: {args.metric}")
        else:
            print(f"Statistics for {args.metric}:")
            print(f"  Count: {stats['count']}")
            print(f"  Min: {stats['min']:.1f}")
            print(f"  Max: {stats['max']:.1f}")
            print(f"  Mean: {stats['mean']:.1f}")
            print(f"  Median: {stats['median']:.1f}")
            print(f"  StdDev: {stats['stdev']:.1f}")
    
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
