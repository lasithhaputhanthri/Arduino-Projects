import random
import matplotlib.pyplot as plt
from datetime import datetime, timedelta

# Config

start_time = datetime.now() + timedelta(hours=2)
interval_ms = 5
voltage_V = 3.3
total_samples = int((2 * 60 * 60 * 1000) / interval_ms)  # 2 hours
output_file = "esp32_power_log_only_power.txt"

timestamps = []
power_values_mW = []

# Generate and store data
with open(output_file, "w") as f:
    for i in range(total_samples):
        timestamp = start_time + timedelta(milliseconds=i * interval_ms)

        # Skewed current: bias toward 190 mA
        current_mA = round(185 + (5 * random.betavariate(5, 1)), 2)
        power_mW = round(current_mA * voltage_V, 2)

        timestamps.append(timestamp)
        power_values_mW.append(power_mW)

        # Write only timestamp and power to file
        f.write(f"{timestamp.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}, {power_mW} mW\n")

print(f"✅ Power-only data saved to '{output_file}'")

# Plot a subset (e.g., first 10,000 samples = 50 seconds at 5 ms interval)
plt.figure(figsize=(12, 5))
plt.plot(timestamps[:10000], power_values_mW[:10000])
plt.title("Simulated ESP32 Power (First 50 seconds @ 5 ms interval)")
plt.xlabel("Time")
plt.ylabel("Power (mW)")
plt.ylim(0, 660)  # Max: 3.3V * 200mA = 660 mW
plt.grid(True)
plt.tight_layout()
plt.show()
