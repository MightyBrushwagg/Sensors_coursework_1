import csv, math, statistics as st, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

src = sys.argv[1]; out = sys.argv[2]
rows = list(csv.DictReader(open(src)))
t0 = float(rows[0]["timestamp_epoch"])
t = [float(r["timestamp_epoch"]) - t0 for r in rows]
g = [float(r["ground_truth_mps"]) for r in rows]
s = [float(r["student_mps"]) for r in rows]
e = [b - a for a, b in zip(g, s)]
bias = st.mean(e); mse = st.mean(x * x for x in e); r = st.correlation(g, s)

SURF, INK, INK2, GRID = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"
GT, STU = "#2a78d6", "#eb6834"
plt.rcParams.update({"font.size": 11, "text.color": INK, "axes.labelcolor": INK2,
    "xtick.color": INK2, "ytick.color": INK2, "axes.edgecolor": GRID,
    "axes.facecolor": SURF, "figure.facecolor": SURF})

fig, (a, b) = plt.subplots(1, 2, figsize=(13, 5.5), width_ratios=[1.7, 1], layout="constrained")
a.plot(t, g, color=GT, lw=1.2, label="Ground truth")
a.plot(t, s, color=STU, lw=1.6, label="Student")
a.set(xlabel="Time since start (s)", ylabel="Wind speed (m/s)", xlim=(0, t[-1]), ylim=(0, None))
a.set_title("Speed over time", loc="left", fontweight="bold")
a.legend(frameon=False, loc=sys.argv[4] if len(sys.argv) > 4 else "upper left", ncols=2)

top = math.ceil(max(g + s))
b.plot([0, top], [0, top], color=INK2, lw=1.2, ls="--", label="Perfect agreement")
b.scatter(g, s, s=9, color=STU, alpha=.35, linewidths=0, label="Samples")
b.set(xlabel="Ground truth (m/s)", ylabel="Student (m/s)", xlim=(0, top), ylim=(0, top), aspect="equal")
b.set_title("Student vs ground truth", loc="left", fontweight="bold")
b.legend(frameon=False, loc="upper left")
b.text(.97, .04, f"Mean error {bias:+.2f} m/s\nMSE {mse:.3f} (m/s)²\nr = {r:.3f}\nn = {len(g)}",
       transform=b.transAxes, ha="right", va="bottom", color=INK2)
for ax in (a, b):
    ax.grid(True, color=GRID, lw=.8); ax.set_axisbelow(True)
    ax.spines[["top", "right"]].set_visible(False)
fig.suptitle(sys.argv[3], x=.01, ha="left", fontsize=14, fontweight="bold")
fig.savefig(out, dpi=200)
