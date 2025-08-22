def calculate_average_power(filename):
    power_sum = 0
    count = 0
    with open(filename, "r") as f:
        for line in f:
            if "mW" in line:
                try:
                    power_str = line.strip().split(",")[1].replace("mW", "").strip()
                    power_mW = float(power_str)
                    power_sum += power_mW
                    count += 1
                except (IndexError, ValueError):
                    continue  # skip malformed lines
    average = power_sum / count if count else 0
    return round(average, 4)

# Set your filenames
file1 = "esp32_power_log_only_power.txt"         # e.g. always on
file2 = "esp32_dutycycle_power_log.txt"          # e.g. duty-cycled

avg1 = calculate_average_power(file1)
avg2 = calculate_average_power(file2)

print(f"🔋 Average Power (Scenario 1): {avg1} mW")
print(f"🌙 Average Power (Scenario 2 - Duty Cycled): {avg2} mW")
