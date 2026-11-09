# Drag-force wind speed sensor

An STM32 Nucleo-F411RE reads a load cell through a NAU7802 24-bit ADC, works
out the wind speed from the drag force on the sensor, and prints one speed in
m/s per line over USB serial at 200 Hz.

This README covers the hardware, the firmware, the calibration and the Python
post-processing scripts. For a step-by-step walk through the calibration only,
see [CALIBRATION.txt](CALIBRATION.txt).

---

## 1. Hardware and wiring

| Part | Detail |
|---|---|
| MCU | STM32F411RE (Nucleo-64), 16 MHz HSI, hard-float FPU enabled in `SystemInit()` |
| ADC | NAU7802 at I²C address 0x2A, gain ×128, internal LDO at 3.0 V, 320 SPS |
| Sensor | Load cell on the NAU7802 A+/A- input, measuring drag on the sensor body |
| I²C1 | PB8 = SCL (D15), PB9 = SDA (D14), 100 kHz, open-drain, AF4 |
| I²C2 (optional, off) | PB10 = SCL (D6), PB3 = SDA (D3), enabled with `USE_SECOND_SCALE 1` |
| Serial out | USART2 TX on PA2, 115200 8N1, through the ST-LINK virtual COM port |
| LED | LD2 (green) toggles on every output line; flashes fast if the board halts with an error |

Power the NAU7802 board from 3V3 and GND. With 3.3 V in, the 3.0 V LDO setting
leaves headroom for the LDO's dropout.

---

## 2. Code layout

| File | What it does |
|---|---|
| [Src/main.c](Src/main.c) | Settings, start-up and calibration sequence, force-to-speed conversion, 200 Hz output loop |
| [Src/nau7802.c](Src/nau7802.c), [Inc/nau7802.h](Inc/nau7802.h) | NAU7802 driver: init, gain/rate/LDO, internal offset calibration, raw reads, averaging, tare |
| [Src/hal_lite.c](Src/hal_lite.c) | Minimal bare-metal replacement for the ST HAL: SysTick 1 ms tick, GPIO, blocking UART, polling I²C master |
| [Src/syscall.c](Src/syscall.c), [Src/sysmem.c](Src/sysmem.c) | Newlib stubs so `printf()` goes to the UART |
| [Src/startup_stm32f411xx.S](Src/startup_stm32f411xx.S), [stm32f411xe_flash.ld](stm32f411xe_flash.ld) | Startup code and linker script |
| [main_snippets.c](main_snippets.c) | Older code for pasting into a CubeMX-generated project. It is not part of the build. |
| [data/](data/) | Logged runs, analysis output and the [tools](data/tools/) scripts |

No ST HAL library is used. `hal_lite.c` provides only the few HAL-named
functions this project calls.

### Building and flashing

The project uses CMake + Ninja through the STM32Cube for VS Code extension
(`cube-cmake`) and the `arm-none-eabi-gcc` 14.3.1+st.2 toolchain. Pick the
**Release** or **Debug** preset, build, then flash `build/<preset>/Coursework.elf`
through the ST-LINK.

---

## 3. How the firmware runs

On power-up `main()` does the following:

1. **Init hardware:** GPIO, USART2, I²C1.
2. **Init the NAU7802** (`NAU7802_Init`): reset, power up, set LDO 3.0 V, gain ×128,
   turn off the clock chopper, enable the PGA output capacitor, start conversions,
   wait 300 ms, then run the chip's internal offset calibration.
3. **Set the ADC to 320 SPS** and run the internal offset calibration again at that rate.
4. **Tare:** wait 1 s, then average 200 samples (~0.6 s) with the tunnel off.
   The result is the zero offset.
5. **Wind reference point** (only if `WIND_CAL_AT_BOOT = 1`): count down 10 s
   while you set the tunnel to 10 Pa, then average 250 samples to get `ref_counts`.
6. **Compute the reference speed** v_ref from the known pressure.
7. **Stream speeds** forever (see section 5).

If any step fails (sensor not found, tare failed, too little change at the
reference point), the board prints `ERROR: ...` and flashes the LED fast.

---

## 4. Calibration

### The physics

Drag on the sensor is

    F = Cd · A · q,      q = ½ · ρ · v²

where q is the **dynamic pressure** (Pa), ρ is air density (1.225 kg/m³)
and v is wind speed. Cd and A are not known accurately, so the code
calibrates them out instead of using them.

The load cell reading after the tare (`counts`) is proportional to F, and so
to q. With one reading `ref_counts` at a known pressure q_ref:

    q / q_ref = counts / ref_counts
    v         = v_ref · √(counts / ref_counts)
    v_ref     = √(2 · q_ref / ρ)

Cd, A and the load cell's counts-per-newton all cancel out. The calibration
needs only the zero (tare) and one known pressure.

### The constants

| Constant | Value | Meaning |
|---|---|---|
| `KNOWN_PSI` | 0.00145038 psi = **10.0 Pa** | Tunnel pressure at the reference point |
| v_ref | √(2 × 10 / 1.225) = **4.04 m/s** | Speed at the reference point |
| `REF_COUNTS` | **22401** | Counts above zero at 10 Pa. Used only when `WIND_CAL_AT_BOOT = 0` |
| `SPEED_BIAS_MPS` | **+0.641 m/s** | Empirical offset added to every output (see below) |
| `AIR_DENSITY` | 1.225 kg/m³ | Standard sea-level air |

So the firmware outputs

    v_out = 4.04 · √(counts / ref_counts) + 0.641      (m/s)

### Doing the calibration

1. Tunnel **off**. Power up or reset the board and wait for the tare.
2. When it prints `Set the tunnel to 0.0015 psi`, bring the tunnel to **10 Pa**
   within 10 s. The prompt rounds to 4 decimals; the value used is 0.00145038 psi.
   The countdown shows the live change in counts.
3. Wait for `Wind calibration done`. The board prints a `#define REF_COUNTS` line.
4. To skip step 2 on later boots, paste that value into `REF_COUNTS` and set
   `WIND_CAL_AT_BOOT 0`. The tare still runs at every boot.

### The bias correction

With no bias, the 4 Nov good-tunnel run read **0.641 m/s low on average**.
[bias_sweep.py](data/tools/bias_sweep.py) tried every constant offset from
−2.000 to +2.000 m/s in 0.001 m/s steps and kept the one with the lowest
error. That offset is `SPEED_BIAS_MPS`.

Limitations:

- It is empirical. There is no physical explanation for it yet.
- It was fitted on 4.5–9.7 m/s only.
- Because it is added after the square root, the output never goes below
  0.641 m/s, even with the fan off.
- It only holds for the same mounting and calibration as that run. Re-fit it
  after remounting or recalibrating.

### Weight calibration (not used for speed)

`RUN_CALIBRATION 1` runs an older guided calibration with a known mass
(`KNOWN_MASS_GRAMS`, 50 g). It gives `ZERO_OFFSET` (−154503) and `CAL_FACTOR`
(1026.06 counts/g). It is useful for checking that the load cell works, but
the speed calculation does not use it.

---

## 5. Output

| Item | Detail |
|---|---|
| Rate | 200 Hz. SysTick gives a 1 ms tick; the main loop prints when `HAL_GetTick()` reaches the next 5 ms deadline. No hardware timer is used. The Team 7 log measured 199.4 Hz. |
| ADC reads | Every new sample (320 SPS) is read as soon as it is ready. Data-ready is found by polling the CR bit of the NAU7802 PU_CTRL register over I²C; the DRDY pin is not used. |
| Filter | First-order low-pass on the tared counts: `y += 0.3 · (x − y)`. Time constant ≈ 2.8 samples ≈ 9 ms. It starts at 0, so the first ~30 ms ramp up. |
| Clamping | If counts ≤ 0 (noise or a backwards push), the speed is 0 before the bias, so the output is 0.641. No upper limit. |
| Format | ASCII text, 2 decimals, CRLF: e.g. `4.62\r\n`. Printed with fixed-point integer maths, not `printf` floats. |
| UART | Blocking (polls TXE/TC). |
| Counter/timestamp | None. Each line is only the speed. |

At boot the board also prints status lines (`Wind speed sensor`, `Init Scale 1... OK`,
the countdown, `Reference speed = 4.04 m/s. Streaming at 200 Hz`) before the
speed stream starts.

---

## 6. Logged data

Runs are logged by the coursework logging tool, which records our board
(the "student" device) and the tunnel's reference instrument (VDAS) at the
same time. Each run produces three CSVs:

| File | Contents |
|---|---|
| `*_student.csv` | Every line our board sent (~200 Hz): timestamp, COM port, `value_raw_mps`, `value_clipped_mps` |
| `*_vdas.csv` | The tunnel's reference speed (ground truth), at a much lower rate |
| `*_aligned.csv` | One row per ground-truth sample with the nearest student reading: `timestamp_iso, timestamp_epoch, ground_truth_mps, student_mps` |

All analysis uses the aligned file.

| Folder | Run | Notes |
|---|---|---|
| [data/](data/) (top level) | `run_20261104_144058_simulated_*` | Simulated test run |
| [data/bad_tunnel/](data/bad_tunnel/) | `run_20261104_152716_giga_*` | 4 Nov, run in the "bad" tunnel |
| [data/good_tunnel/](data/good_tunnel/) | `run_20261104_155953_*` | 4 Nov, 4.5–9.7 m/s, used to fit the bias |
| [data/coursework/team7/team7/](data/coursework/team7/team7/) | `Team_7_log1_20261105_115057_*` | 5 Nov **graded run**, 0–10.3 m/s, 123 s, includes a fan-off section |

There is also a top-level [good_tunnel/](good_tunnel/) folder holding a copy
of the 4 Nov good-tunnel CSVs and graph.

---

## 7. Post-processing scripts

All scripts are in [data/tools/](data/tools/). They need Python 3.10+ with
`matplotlib` (and `numpy` for `delay_sweep.py`). Paths to the data are written
into each script as full paths to this folder, so they break if the Coursework
folder is moved or renamed. Outputs are saved next to the input CSVs.

| Script | Input | What it does | Outputs |
|---|---|---|---|
| [plot_run.py](data/tools/plot_run.py) | any `*_aligned.csv` | Speed over time (ground truth vs student) and a student-vs-truth scatter, with mean error, MSE and correlation | `*_ground_truth_vs_student.png` |
| [bias_sweep.py](data/tools/bias_sweep.py) | good-tunnel run (+ bad-tunnel run for a check) | Sweeps a constant bias from −2 to +2 m/s, picks the lowest MSE, also prints per-speed residuals and a scale + offset fit for comparison | `_bias_sweep.csv`, `_bias_corrected.csv`, `_bias_corrected.png` |
| [team7_plots.py](data/tools/team7_plots.py) | Team 7 graded run | Plots with the graded band (2–10 m/s) marked: speed over time, error per step with running MSE, scatter | `_graded_speed.png`, `_error_running_mse.png`, `_graded_scatter.png` |
| [delay_sweep.py](data/tools/delay_sweep.py) | Team 7 student + aligned CSVs | Shifts the student signal in time from −1 to +2 s in 5 ms steps and finds the delay that gives the lowest MSE after 100 s, to check for time lag | `_delay_sweep.csv`, `_delay_speed.png`, `_delay_scatter.png`, `_delay_mse_sweep.png` |

Usage:

```
python data/tools/plot_run.py <aligned.csv> <out.png> "<title>" [legend position]
python data/tools/bias_sweep.py
python data/tools/team7_plots.py
python data/tools/delay_sweep.py
```

The outputs of `delay_sweep.py` are not currently in the data folder.

---

## 8. Results

| Run | n | Speed range | RMSE (m/s) | MSE ((m/s)²) | R² |
|---|---|---|---|---|---|
| Good tunnel, 4 Nov, no bias | 254 | 4.46–9.73 | 0.669 | 0.447 | 0.865 |
| Good tunnel, 4 Nov, +0.641 bias | 254 | 4.46–9.73 | 0.190 | 0.036 | 0.989 |
| Team 7 graded run, 5 Nov | 492 | 0–10.32 | 0.505 | 0.255 | 0.976 |

The good-tunnel run with bias is the same data the bias was fitted to, so it
is a best case. The Team 7 run is independent data. In its 65 fan-off rows
the sensor reads 0.61 m/s on average, which is mostly the bias.

---

## 9. Known issues

- **Bias floor:** with the fan off, the output sits at about 0.64 m/s instead of 0.
- **Single calibration point:** only the zero and 10 Pa are measured. The √ law
  and the bias are assumed to hold across the rest of the range.
- **Air density is fixed** at 1.225 kg/m³. It is not corrected for temperature or air pressure.
- **Graded-run build:** the committed code (`7fa63b6`) has `WIND_CAL_AT_BOOT 0`,
  so the graded run probably used the stored `REF_COUNTS = 22401`. The working
  copy has it set to 1.
