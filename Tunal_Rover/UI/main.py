import serial
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

# Set up the serial connection
try:
    ser = serial.Serial('COM23', 115200)
    sleep(2)  # Give time for connection to establish
except serial.SerialException:
    st.error("Could not open COM23. Ensure the device is connected.")
    ser = None

# Streamlit interface setup
st.set_page_config(page_title="ESP32 Environmental Sensor Dashboard", page_icon="🌍", layout="wide")

# Header Bar
st.markdown("""
    <style>
        .header {
            background-color: #060606;
            color: white;
            padding: 10px;
            text-align: center;
            font-size: 24px;
            font-weight: bold;
        }
        .footer {
            background-color: #090909;
            color: white;
            padding: 10px;
            text-align: center;
            font-size: 21px;
        }
        .healthy {
            color: green;
            font-weight: bold;
        }
        .warning {
            color: red;
            font-weight: bold;
        }
        .neutral {
            font-weight: normal;
        }
        .section {
            margin-top: 20px;
        }
        .separator {
            border-top: 1px solid #ccc;
            margin-top: 20px;
            margin-bottom: 20px;
        }
        .location {
            font-size: 32px;
            font-weight: bold;
            color: #eeeeee;
        }
    </style>
""", unsafe_allow_html=True)

# Title
st.markdown('<div class="header">ESP32 Environmental Sensor Dashboard</div>', unsafe_allow_html=True)

# Placeholders for each section
location_placeholder = st.empty()
temp_placeholder = st.empty()
humidity_placeholder = st.empty()
gas_placeholder = st.empty()
status_placeholder = st.empty()
history_placeholder = st.empty()

# Create deque objects to store sensor data for plotting
history_length = 20  # How many previous data points to store
temp_history = deque(maxlen=history_length)
humidity_history = deque(maxlen=history_length)
gas_history = deque(maxlen=history_length)

# Store previous latitude and longitude to prevent redundant map rendering
previous_lat = None
previous_lon = None

# Continuous read and display
while True:
    if ser and ser.in_waiting > 0:
        # Read serial data
        serial_data = ser.readline().decode('utf-8').strip()
        
        # Parse latitude, longitude, temperature, and humidity from the serial data
        try:
            parts = serial_data.split('|')
            lat = float(parts[1].split(':')[1].strip())
            lon = float(parts[2].split(':')[1].strip())
            temp = float(parts[3].split(':')[1].strip().replace("°C", ""))
            humidity = float(parts[4].split(':')[1].strip().replace("%", ""))
            
            # Generate dummy gas sensor data
            gas_level = random.uniform(0, 100)

            # Only update the map if the location has changed
            if lat != previous_lat or lon != previous_lon:
                # Update location with numeric display
                location_placeholder.subheader("Location Data")
                location_placeholder.text(f"Latitude: {lat:.6f}, Longitude: {lon:.6f}")
                location_placeholder.markdown(f"<p class='location'>{lat:.6f}, {lon:.6f}</p>", unsafe_allow_html=True)
                
                # Display the map with the location
                st.map(data={"lat": [lat], "lon": [lon]})
                
                # Update previous lat and lon for future comparisons
                previous_lat = lat
                previous_lon = lon

            # Update environmental data
            temp_placeholder.metric("Temperature (°C)", f"{temp:.2f}")
            humidity_placeholder.metric("Humidity (%)", f"{humidity:.2f}")
            gas_placeholder.metric("Gas Sensor Level", f"{gas_level:.2f}")
            
            # Health status checks
            temp_status = "Healthy" if is_healthy(temp, HEALTHY_RANGES["temperature"]) else "Warning"
            humidity_status = "Healthy" if is_healthy(humidity, HEALTHY_RANGES["humidity"]) else "Warning"
            gas_status = "Healthy" if is_healthy(gas_level, HEALTHY_RANGES["gas_level"]) else "Warning"
            
            # Health status formatting
            temp_class = "healthy" if temp_status == "Healthy" else "warning"
            humidity_class = "healthy" if humidity_status == "Healthy" else "warning"
            gas_class = "healthy" if gas_status == "Healthy" else "warning"
            
            # Display health status with color coding
            status_placeholder.subheader("Health Status")
            status_placeholder.write(f"Temperature: <span class='{temp_class}'>{temp_status}</span>", unsafe_allow_html=True)
            status_placeholder.write(f"Humidity: <span class='{humidity_class}'>{humidity_status}</span>", unsafe_allow_html=True)
            status_placeholder.write(f"Gas Sensor: <span class='{gas_class}'>{gas_status}</span>", unsafe_allow_html=True)
            
            # Update histories for plotting
            temp_history.append(temp)
            humidity_history.append(humidity)
            gas_history.append(gas_level)
            
            # Plot history of sensor data
            fig, ax = plt.subplots(3, 1, figsize=(10, 6))
            ax[0].plot(temp_history, marker='o', label='Temperature (°C)', color='tab:blue')
            ax[1].plot(humidity_history, marker='o', label='Humidity (%)', color='tab:orange')
            ax[2].plot(gas_history, marker='o', label='Gas Level', color='tab:green')

            # Add labels and titles
            for i, sensor in enumerate(["Temperature", "Humidity", "Gas Level"]):
                ax[i].set_title(f'{sensor} History')
                ax[i].set_xlabel('Time')
                ax[i].set_ylabel(sensor)
                ax[i].legend()

            # Display plots
            history_placeholder.pyplot(fig)
            
        except (ValueError, IndexError):
            st.error("Failed to parse data. Please check the data format.")
            
    sleep(1)  # Delay to avoid overwhelming the interface

    # Horizontal separator
    st.markdown('<div class="separator"></div>', unsafe_allow_html=True)

# Footer Bar
st.markdown('<div class="footer">© 2024 ESP32 Environmental Dashboard</div>', unsafe_allow_html=True)
