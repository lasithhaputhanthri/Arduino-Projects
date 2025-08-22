import serial
import time
from datetime import datetime

PORT = 'COM17'  # Change this
BAUD_RATE = 115200
OUTPUT_FILE = "power_log.txt"
DURATION = 7200  # 10 seconds for quick test

ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
start_time = time.time()

with open(OUTPUT_FILE, 'w') as file:
    while (time.time() - start_time) < DURATION:
        line = ser.readline().decode().strip()
        if line:
            print(f"Raw line: {line}")
            try:
                power_mW = float(line)
                timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                log_line = f"{timestamp}, {power_mW:.1f} mW"
                print(log_line)
                file.write(log_line + "\n")
                file.flush()
            except ValueError:
                continue

print("Done.")
ser.close()
