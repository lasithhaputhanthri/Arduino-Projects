import random
import streamlit as st
import matplotlib.pyplot as plt
from time import sleep
from collections import deque

# Define healthy ranges for each variable
HEALTHY_RANGES = {
    "temperature": (18.0, 30.0),  # Adjust as needed
    "humidity": (30.0, 60.0),     # Adjust as needed
    "gas_level": (0.0, 100.0)     # Dummy range for gas sensor data
}

# Function to check if a value is within the healthy range
def is_healthy(value, range_tuple):
    return range_tuple[0] <= value <= range_tuple[1]

# Streamlit interface setup
st.set_page_config(page_title="ESP32 Environmental Sensor Dashboard", page_icon="🌍", layout="wide")

# Header
st.title("ESP32 Environmental Sensor Dashboard")

# Create placeholders
temp_placeholder = st.empty()
humidity_placeholder = st.empty()
gas_placeholder = st.empty()
status_placeholder = st.empty()
history_placeholder = st.empty()

# Create deques to store sensor data
history_length = 20  # Adjustable history length
temp_history = deque(maxlen=history_length)
humidity_history = deque(maxlen=history_length)
gas_history = deque(maxlen=history_length)

# Continuous simulation loop
while True:
    # Generate dummy data
    temp = random.uniform(15, 35)
    humidity = random.uniform(20, 70)
    gas_level = random.uniform(0, 100)

    # Update metrics
    temp_placeholder.metric("Temperature (°C)", f"{temp:.2f}")
    humidity_placeholder.metric("Humidity (%)", f"{humidity:.2f}")
    gas_placeholder.metric("Gas Level", f"{gas_level:.2f}")

    # Health status checks
    temp_status = "Healthy" if is_healthy(temp, HEALTHY_RANGES["temperature"]) else "Warning"
    humidity_status = "Healthy" if is_healthy(humidity, HEALTHY_RANGES["humidity"]) else "Warning"
    gas_status = "Healthy" if is_healthy(gas_level, HEALTHY_RANGES["gas_level"]) else "Warning"

    # Display health status
    status_placeholder.write(f"""
    - Temperature: **{temp_status}**
    - Humidity: **{humidity_status}**
    - Gas Level: **{gas_status}**
    """)

    # Update history
    temp_history.append(temp)
    humidity_history.append(humidity)
    gas_history.append(gas_level)

    # Plot sensor data
    fig, axes = plt.subplots(3, 1, figsize=(10, 8))
    for ax, history, label in zip(axes, [temp_history, humidity_history, gas_history], ["Temperature", "Humidity", "Gas Level"]):
        ax.plot(history, label=label)
        ax.set_title(f"{label} History")
        ax.set_xlabel("Time")
        ax.set_ylabel(label)
        ax.legend()
    history_placeholder.pyplot(fig)

    sleep(1)  # Simulate real-time updates
