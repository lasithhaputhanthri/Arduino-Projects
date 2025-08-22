import random
import matplotlib.pyplot as plt
from datetime import datetime, timedelta

# Config
start_time = datetime.now() - timedelta(hours=2)
interval_ms = 5
voltage_V = 3.3
duration_sec = 2 * 60 * 60  # 2 hours
total_samples = int((duration_sec * 1000) / interval_ms)
output_file = "esp32_dutycycle_power_log.txt"

timestamps = []
power_values = []

# Cycle settings
cycle_length_ms = 60000
active_duration_ms = 35

# Generate and store data
with open(output_file, "w") as f:
    for i in range(total_samples):
        timestamp = start_time + timedelta(milliseconds=i * interval_ms)
        cycle_position = (i * interval_ms) % cycle_length_ms

        if cycle_position < active_duration_ms:
            current_mA = round(185 + (5 * random.betavariate(5, 1)), 2)
        else:
            current_mA = round(10+random.uniform(0, 4), 3)

        power_mW = round(current_mA * voltage_V, 4)
        power_values.append(power_mW)
        timestamps.append(timestamp)

        f.write(f"{timestamp.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}, {power_mW} mW\n")

print(f"✅ Power data saved to: {output_file}")

# 📉 Plot full 2-hour data with downsampling
downsample_factor = 100  # plot 1 point per 100 samples = ~14,400 points
ds_timestamps = timestamps[::downsample_factor]
ds_powers = power_values[::downsample_factor]

plt.figure(figsize=(16, 6))
plt.plot(ds_timestamps, ds_powers, linewidth=0.5)
plt.title("Full ESP32 Duty-Cycled Power Usage (2 Hours, Downsampled)")
plt.xlabel("Time")
plt.ylabel("Power (mW)")
plt.ylim(0, 660)
plt.grid(True)
plt.tight_layout()
plt.show()
