import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# === Function to load and clean log data ===
def load_power_log(file_path):
    df = pd.read_csv(file_path, header=None, names=['Timestamp', 'Power'], sep=',', engine='python')
    df['Timestamp'] = pd.to_datetime(df['Timestamp'])
    df['Power'] = df['Power'].astype(str).str.replace(' mW', '').astype(float)
    return df

# === Function to plot with fixed Y-axis ===
def plot_power_data(df, title):
    plt.figure(figsize=(14, 5))
    plt.plot(df['Timestamp'], df['Power'], linewidth=0.7)
    plt.title(title)
    plt.xlabel("Time")
    plt.ylabel("Power (mW)")
    plt.ylim(0, 700)  # Fixed Y-axis range
    plt.grid(True)
    plt.gca().xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
    plt.tight_layout()
    plt.show()

# === Load and plot both datasets ===
dutycycle_df = load_power_log('esp32_dutycycle_power_log.txt')
onlypower_df = load_power_log('esp32_power_log_only_power.txt')

plot_power_data(dutycycle_df, "ESP32 Duty-Cycled Power Usage (Full Data)")
plot_power_data(onlypower_df, "ESP32 Continuous Power Usage (Full Data)")
