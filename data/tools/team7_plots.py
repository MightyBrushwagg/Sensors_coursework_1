import csv, math, statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

D = "C:/Users/xavie/OneDrive/Desktop/UCL/year 3/Sensors/Coursework/data/coursework/team7/team7/"
B = D + "Team_7_log1_20261105_115057"
LO, HI = 2.0, 10.0
rows = list(csv.DictReader(open(B + "_aligned.csv")))
t0 = float(rows[0]["timestamp_epoch"])
t = [float(r["timestamp_epoch"]) - t0 for r in rows]
g = [float(r["ground_truth_mps"]) for r in rows]
s = [float(r["student_mps"]) for r in rows]
e = [b - a for a, b in zip(g, s)]
graded = [LO < a <= HI for a in g]

def mse(errs): return st.mean(x * x for x in errs)
mse_all = mse(e); mse_gr = mse([x for x, k in zip(e, graded) if k])
n_gr = sum(graded)

SURF, INK, INK2, GRID, BAND = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#8a8984"
GT, STU, AQUA = "#2a78d6", "#eb6834", "#1baf7a"
plt.rcParams.update({"font.size": 11, "text.color": INK, "axes.labelcolor": INK2, "xtick.color": INK2,
    "ytick.color": INK2, "axes.edgecolor": GRID, "axes.facecolor": SURF, "figure.facecolor": SURF})
TITLE = "Team 7 log 1, 5 Nov 2026 11:50:57"

def tidy(ax):
    ax.grid(True, color=GRID, lw=.8); ax.set_axisbelow(True)
    ax.spines[["top", "right"]].set_visible(False)

# 1. Speed over time with the graded band
fig, ax = plt.subplots(figsize=(12, 5.5), layout="constrained")
ax.axhspan(LO, HI, color=BAND, alpha=.18, lw=0, zorder=0)
ax.text(t[-1] * .99, HI - .15, "Graded area (2–10 m/s)", ha="right", va="top", color=INK2, fontweight="bold")
ax.plot(t, g, color=GT, lw=1.4, label="Ground truth")
ax.plot(t, s, color=STU, lw=1.6, label="Student")
ax.set(xlabel="Time since start (s)", ylabel="Wind speed (m/s)", xlim=(0, t[-1]), ylim=(0, 11))
ax.legend(frameon=False, loc="lower center", ncols=2)
ax.set_title(f"{TITLE}: speed over time", loc="left", fontweight="bold")
tidy(ax)
fig.savefig(B + "_graded_speed.png", dpi=200); plt.close(fig)

# 2. Error per time step and running MSE, in separate panels because the units differ
cum_gr, sq_g, n_g = [], 0.0, 0
for x, k in zip(e, graded):
    if k: sq_g += x * x; n_g += 1
    cum_gr.append(sq_g / n_g if n_g else float("nan"))
fig, (ax, ax2) = plt.subplots(2, 1, figsize=(12, 7.5), sharex=True, height_ratios=[1.4, 1], layout="constrained")
ax.axhline(0, color=INK2, lw=1)
e_gr = [x if k else float("nan") for x, k in zip(e, graded)]   # gaps outside the graded area
ax.plot(t, e_gr, color=STU, lw=1, alpha=.8, label="Error at each graded step (student − ground truth)")
ax.set(ylabel="Error (m/s)", xlim=(0, t[-1]))
lim = math.ceil(max(abs(x) for x, k in zip(e, graded) if k) * 2) / 2
ax.set_ylim(-lim, lim)
ax.legend(frameon=False, loc="lower left")
ax2.plot(t, cum_gr, color=GT, lw=2, label=f"Running MSE (final {mse_gr:.3f} (m/s)², n = {n_gr})")
ax2.set(xlabel="Time since start (s)", ylabel="Running MSE ((m/s)²)", ylim=(0, None))
ax2.legend(frameon=False, loc="upper right")
tidy(ax2)
ax.set_title(f"{TITLE}: graded area (2–10 m/s) error and running MSE", loc="left", fontweight="bold")
tidy(ax)
fig.savefig(B + "_error_running_mse.png", dpi=200); plt.close(fig)

# 3. Student vs ground truth with the graded band
top = math.ceil(max(g + s))
fig, ax = plt.subplots(figsize=(7.5, 7.5), layout="constrained")
ax.axvspan(LO, HI, color=BAND, alpha=.18, lw=0, zorder=0)
ax.text((LO + HI) / 2, top - .2, "Graded area (2–10 m/s)", ha="center", va="top", color=INK2, fontweight="bold")
ax.plot([0, top], [0, top], color=INK2, lw=1.2, ls="--", label="Perfect agreement")
ax.scatter(g, s, s=12, color=STU, alpha=.45, linewidths=0, label="Samples")
ax.set(xlabel="Ground truth (m/s)", ylabel="Student (m/s)", xlim=(0, top), ylim=(0, top), aspect="equal")
ax.legend(frameon=False, loc="upper left", bbox_to_anchor=(0, .93))
ax.text(.97, .04, f"All: MSE {mse_all:.3f} (m/s)², n = {len(g)}\nGraded: MSE {mse_gr:.3f} (m/s)², n = {n_gr}",
        transform=ax.transAxes, ha="right", va="bottom", color=INK2)
ax.set_title(f"{TITLE}: student vs ground truth", loc="left", fontweight="bold", fontsize=12)
tidy(ax)
fig.savefig(B + "_graded_scatter.png", dpi=200); plt.close(fig)
print(f"MSE all {mse_all:.4f}  graded {mse_gr:.4f} n={n_gr}  max|err| {max(map(abs,e)):.2f}")
