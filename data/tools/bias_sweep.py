import csv, math, statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = "C:/Users/xavie/OneDrive/Desktop/UCL/year 3/Sensors/Coursework/data/"
GOOD = ROOT + "good_tunnel/run_20261104_155953"
BAD = ROOT + "bad_tunnel/run_20261104_152716_giga_aligned.csv"

def load(p):
    r = list(csv.DictReader(open(p)))
    return r, [float(x["ground_truth_mps"]) for x in r], [float(x["student_mps"]) for x in r]
def mse(g, s, b=0.0, k=1.0):
    return rmse(g, s, b, k) ** 2
def rmse(g, s, b=0.0, k=1.0):
    return math.sqrt(sum((k * c + b - a) ** 2 for a, c in zip(g, s)) / len(g))

rows, g, s = load(GOOD + "_aligned.csv")
biases = [i / 1000 for i in range(-2000, 2001)]   # -2.000 to +2.000 m/s in 1 mm/s steps
sweep = [(b, mse(g, s, b)) for b in biases]
best, best_mse = min(sweep, key=lambda x: x[1])
best_rmse = math.sqrt(best_mse)
print(f"tried {len(biases)}; best bias {best:+.3f}; rmse {rmse(g,s):.4f} -> {best_rmse:.4f}; mean error check {st.mean(a-c for a,c in zip(g,s)):+.4f}")

c = [x + best for x in s]
with open(GOOD + "_bias_corrected.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["timestamp_iso", "timestamp_epoch", "ground_truth_mps", "student_mps", "student_corrected_mps"])
    for r, v in zip(rows, c):
        w.writerow([r["timestamp_iso"], r["timestamp_epoch"], r["ground_truth_mps"], r["student_mps"], f"{v:.6f}"])
with open(GOOD + "_bias_sweep.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["bias_mps", "rmse_mps", "mse_mps2"])
    w.writerows((f"{b:.3f}", f"{math.sqrt(e):.6f}", f"{e:.6f}") for b, e in sweep)

for lo, hi in [(3,5),(5,7),(7,9),(9,12)]:
    p = [(a, v) for a, v in zip(g, c) if lo <= a < hi]
    print(f"bin {lo}-{hi}: n={len(p)} residual {st.mean(v-a for a,v in p):+.3f}")
fit = st.linear_regression(s, g)
print(f"scale+offset fit: k={fit.slope:.4f} b={fit.intercept:+.4f} rmse={rmse(g,s,fit.intercept,fit.slope):.4f}")

_, bg, bs = load(BAD)
print(f"BAD all: rmse raw {rmse(bg,bs):.4f}, with bias {rmse(bg,bs,best):.4f}, scale+offset {rmse(bg,bs,fit.intercept,fit.slope):.4f}")
hi = [(a, v) for a, v in zip(bg, bs) if a >= 3]; lo = [(a, v) for a, v in zip(bg, bs) if a < 1]
for n, p in (("gt>=3", hi), ("gt<1", lo)):
    A = [x[0] for x in p]; B = [x[1] for x in p]
    print(f"BAD {n}: n={len(p)} rmse raw {rmse(A,B):.4f}, with bias {rmse(A,B,best):.4f}")

t0 = float(rows[0]["timestamp_epoch"]); t = [float(r["timestamp_epoch"]) - t0 for r in rows]
SURF, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
GT, STU = "#2a78d6", "#eb6834"
plt.rcParams.update({"font.size": 11, "text.color": INK, "axes.labelcolor": INK2, "xtick.color": INK2,
    "ytick.color": INK2, "axes.edgecolor": GRID, "axes.facecolor": SURF, "figure.facecolor": SURF})
fig, (a, b, d) = plt.subplots(1, 3, figsize=(17, 5.5), width_ratios=[1.6, 1, 1], layout="constrained")
a.plot(t, g, color=GT, lw=1.4, label="Ground truth")
a.plot(t, c, color=STU, lw=1.6, label="Student + bias")
a.set(xlabel="Time since start (s)", ylabel="Wind speed (m/s)", xlim=(0, t[-1]), ylim=(0, None))
a.set_title("Corrected speed over time", loc="left", fontweight="bold")
a.legend(frameon=False, loc="upper left", ncols=2)
top = math.ceil(max(g + c))
b.plot([0, top], [0, top], color=INK2, lw=1.2, ls="--", label="Perfect agreement")
b.scatter(g, c, s=12, color=STU, alpha=.45, linewidths=0, label="Samples")
b.set(xlabel="Ground truth (m/s)", ylabel="Student + bias (m/s)", xlim=(0, top), ylim=(0, top), aspect="equal")
b.set_title("Corrected vs ground truth", loc="left", fontweight="bold")
b.legend(frameon=False, loc="upper left")
d.plot(biases, [e for _, e in sweep], color=STU, lw=2)
d.scatter([best], [best_mse], s=60, color=STU, edgecolors=SURF, linewidths=2, zorder=3)
d.text(.97, .96, f"Best bias {best:+.3f} m/s\nMSE {best_mse:.3f} (m/s)²", transform=d.transAxes, ha="right", va="top", color=INK2)
d.set(xlabel="Bias added to student (m/s)", ylabel="MSE ((m/s)²)", ylim=(0, None))
d.set_title("MSE across the sweep", loc="left", fontweight="bold")
for ax in (a, b, d):
    ax.grid(True, color=GRID, lw=.8); ax.set_axisbelow(True); ax.spines[["top", "right"]].set_visible(False)
fig.suptitle(f"Good tunnel run with bias correction: MSE {mse(g,s):.3f} to {best_mse:.3f} (m/s)²", x=.01, ha="left", fontsize=14, fontweight="bold")
fig.savefig(GOOD + "_bias_corrected.png", dpi=200)
