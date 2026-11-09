import csv, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

D = "C:/Users/xavie/OneDrive/Desktop/UCL/year 3/Sensors/Coursework/data/coursework/team7/team7/Team_7_log1_20261105_115057"
LO, HI, T_FROM = 2.0, 10.0, 100.0
st = list(csv.DictReader(open(D + "_student.csv")))
al = list(csv.DictReader(open(D + "_aligned.csv")))
ts = np.array([float(r["timestamp_epoch"]) for r in st]); vs = np.array([float(r["value_raw_mps"]) for r in st])
ta = np.array([float(r["timestamp_epoch"]) for r in al]); g = np.array([float(r["ground_truth_mps"]) for r in al])
t0 = ta[0]; t = ta - t0
graded = (g > LO) & (g <= HI)
win = graded & (t > T_FROM)
mid = graded & (t >= 20) & (t <= 100)

def student_at(times):
    """Student reading nearest in time to each requested time (same rule as the aligned file)."""
    j = np.searchsorted(ts, times).clip(1, len(ts) - 1)
    j -= (times - ts[j - 1]) < (ts[j] - times)
    return vs[j]
def mse(mask, d): return float(np.mean((student_at(ta[mask] - d) - g[mask]) ** 2))

delays = np.round(np.arange(-1.0, 2.0 + 1e-9, 0.005), 3)   # +d = student shown d seconds later
sweep = np.array([mse(win, d) for d in delays])
k = int(sweep.argmin()); best, best_mse = float(delays[k]), float(sweep[k])
base = mse(win, 0.0)
print(f"after 100 s: n={win.sum()} MSE at 0 s {base:.4f}; best delay {best*1000:+.0f} ms -> MSE {best_mse:.4f}")
print(f"aligned-file check at 0 s: {np.mean((np.array([float(r['student_mps']) for r in al])[win]-g[win])**2):.4f}")
for name, m in (("20-100 s graded", mid), ("all graded", graded)):
    sw = np.array([mse(m, d) for d in delays]); kk = int(sw.argmin())
    print(f"{name}: n={m.sum()} MSE 0 s {mse(m,0):.4f}, with {best*1000:+.0f} ms {mse(m,best):.4f}; own best {delays[kk]*1000:+.0f} ms -> {sw[kk]:.4f}")

with open(D + "_delay_sweep.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["delay_s", "mse_after_100s_mps2"])
    w.writerows((f"{d:.3f}", f"{e:.6f}") for d, e in zip(delays, sweep))

SURF, INK, INK2, GRID, BAND = "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df", "#8a8984"
GT, STU = "#2a78d6", "#eb6834"
SQ = chr(0xb2)
plt.rcParams.update({"font.size": 11, "text.color": INK, "axes.labelcolor": INK2, "xtick.color": INK2,
    "ytick.color": INK2, "axes.edgecolor": GRID, "axes.facecolor": SURF, "figure.facecolor": SURF})
TITLE = "Team 7 log 1, graded samples after 100 s"
def tidy(ax):
    ax.grid(True, color=GRID, lw=.8); ax.set_axisbelow(True); ax.spines[["top", "right"]].set_visible(False)

# 1. Speed after 100 s
fig, a = plt.subplots(figsize=(12, 5.5), layout="constrained")
sel = ts - t0 >= T_FROM
tt = ts[sel] - t0
a.axhspan(LO, HI, color=BAND, alpha=.18, lw=0, zorder=0)
a.text(tt[-1] - .2, HI - .15, "Graded area (2" + chr(0x2013) + "10 m/s)", ha="right", va="top", color=INK2, fontweight="bold")
a.plot(t[t >= T_FROM], g[t >= T_FROM], color=GT, lw=1.6, label="Ground truth")
a.plot(tt, vs[sel], color=STU, lw=1, ls="--", alpha=.7, label="Student, as recorded")
a.plot(tt + best, vs[sel], color=STU, lw=1.8, label=f"Student, delayed {best*1000:.0f} ms")
a.set(xlabel="Time since start (s)", ylabel="Wind speed (m/s)", xlim=(T_FROM, tt[-1]), ylim=(0, 11))
a.set_title(f"Team 7 log 1: speed after 100 s, with and without a {best*1000:.0f} ms delay", loc="left", fontweight="bold")
a.legend(frameon=False, loc="upper left", bbox_to_anchor=(0, .9))
tidy(a)
fig.savefig(D + "_delay_speed.png", dpi=200); plt.close(fig)

# 2. Student vs ground truth, graded samples after 100 s
gs = g[win]; sd = student_at(ta[win] - best); s0 = student_at(ta[win])
top = 11
fig, b = plt.subplots(figsize=(7.5, 7.5), layout="constrained")
b.plot([0, top], [0, top], color=INK2, lw=1.2, ls="--", label="Perfect agreement")
b.scatter(gs, s0, s=20, facecolors="none", edgecolors=STU, alpha=.6, linewidths=1, label="As recorded")
b.scatter(gs, sd, s=20, color=STU, alpha=.8, linewidths=0, label=f"Delayed {best*1000:.0f} ms")
b.set(xlabel="Ground truth (m/s)", ylabel="Student (m/s)", xlim=(0, top), ylim=(0, top), aspect="equal")
b.set_title(f"{TITLE}: student vs ground truth", loc="left", fontweight="bold", fontsize=12)
b.legend(frameon=False, loc="upper left")
b.text(.97, .04, f"As recorded: MSE {base:.3f} (m/s){SQ}" + chr(10) + f"Delayed: MSE {best_mse:.3f} (m/s){SQ}" + chr(10) + f"n = {win.sum()}",
       transform=b.transAxes, ha="right", va="bottom", color=INK2)
tidy(b)
fig.savefig(D + "_delay_scatter.png", dpi=200); plt.close(fig)

# 3. MSE across the delay sweep
fig, c = plt.subplots(figsize=(9, 5.5), layout="constrained")
c.plot(delays, sweep, color=STU, lw=2)
c.scatter([best], [best_mse], s=60, color=STU, edgecolors=SURF, linewidths=2, zorder=3)
c.text(.5, .96, f"Best delay {best*1000:+.0f} ms" + chr(10) + f"MSE {best_mse:.3f} (m/s){SQ}" + chr(10) + f"(no delay: {base:.3f} (m/s){SQ})",
       transform=c.transAxes, ha="center", va="top", color=INK2)
c.set(xlabel="Delay added to student (s)", ylabel=f"MSE ((m/s){SQ})", ylim=(0, None), xlim=(delays[0], delays[-1]))
c.set_title(f"{TITLE}: MSE across the delay sweep", loc="left", fontweight="bold", fontsize=12)
tidy(c)
fig.savefig(D + "_delay_mse_sweep.png", dpi=200); plt.close(fig)
