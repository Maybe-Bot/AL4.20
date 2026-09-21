#!/usr/bin/env python3

import csv
import json
from pathlib import Path

LOG = Path("small-events.jsonl")
CSV_OUT = Path("run-summary.csv")

if not LOG.exists():
    raise SystemExit(f"Could not find {LOG}. Run ./scripts/run-small.sh first.")

rows = []

with LOG.open() as f:
    for line in f:
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue

        if event.get("event") == "summary":
            rows.append(event)

if not rows:
    raise SystemExit("No summary records found in small-events.jsonl")

fields = [
    "tick",
    "year",
    "month",
    "day",
    "population",
    "awake",
    "asleep",
    "off",
    "courtships_active",
    "courtships_started",
    "courtships_failed",
    "courtships_completed",
    "births",
    "deaths",
    "state_transitions",
    "awake_to_asleep",
    "asleep_to_awake",
    "asleep_to_off",
]

with CSV_OUT.open("w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    for row in rows:
        writer.writerow({field: row.get(field, "") for field in fields})

print(f"Wrote {CSV_OUT}")

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib is not installed, so only the CSV was created.")
    print("Install it with: sudo apt install python3-matplotlib")
    raise SystemExit(0)

ticks = [r["tick"] for r in rows]

plt.figure(figsize=(12, 6))
plt.plot(ticks, [r.get("awake", 0) for r in rows], label="Awake")
plt.plot(ticks, [r.get("asleep", 0) for r in rows], label="Asleep")
plt.plot(ticks, [r.get("off", 0) for r in rows], label="Off")
plt.xlabel("Tick")
plt.ylabel("Organisms")
plt.title("Lifecycle states")
plt.legend()
plt.tight_layout()
plt.savefig("population-states.png", dpi=150)
plt.close()

plt.figure(figsize=(12, 6))
plt.plot(ticks, [r.get("courtships_active", 0) for r in rows],
         label="Active")
plt.plot(ticks, [r.get("courtships_started", 0) for r in rows],
         label="Started")
plt.plot(ticks, [r.get("courtships_failed", 0) for r in rows],
         label="Failed")
plt.plot(ticks, [r.get("courtships_completed", 0) for r in rows],
         label="Completed")
plt.xlabel("Tick")
plt.ylabel("Courtships")
plt.title("Courtship")
plt.legend()
plt.tight_layout()
plt.savefig("courtship.png", dpi=150)
plt.close()

plt.figure(figsize=(12, 6))
plt.plot(ticks, [r.get("population", 0) for r in rows],
         label="Population")
plt.plot(ticks, [r.get("births", 0) for r in rows],
         label="Cumulative births")
plt.plot(ticks, [r.get("deaths", 0) for r in rows],
         label="Cumulative deaths")
plt.xlabel("Tick")
plt.ylabel("Count")
plt.title("Population and reproduction")
plt.legend()
plt.tight_layout()
plt.savefig("reproduction.png", dpi=150)
plt.close()

print("Wrote population-states.png")
print("Wrote courtship.png")
print("Wrote reproduction.png")
