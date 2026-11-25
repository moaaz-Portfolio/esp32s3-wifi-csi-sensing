# Firmware Guide (ESP32-S3 CSI Receiver)

This document explains the firmware-side role in the RF sensing pipeline.

## Firmware Role

The firmware running under `firmware/csi_receiver/` is responsible for:

- configuring ESP32-S3 WiFi CSI reception
- capturing CSI packets from the RF channel
- formatting CSI packets for UART serial streaming
- providing input to host-side logging and ML inference

## Build and Flash

```bash
cd firmware/csi_receiver
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected Behavior

After successful flash, host-side Python scripts should read lines beginning with `CSI,` over serial.

## Integration Notes

- Host scripts in `python/` and `ml/` assume UART baud `115200`
- Keep USB data cable stable during long recordings
- Use consistent AP and room setup for repeatable datasets

## Verification

Use the serial reader:

```bash
source .venv/bin/activate
cd python
python serial_reader.py --port /dev/ttyACM0 --baud 115200 --duration 30 --verbose
```

## First-Time Wi-Fi Setup (ESP32-S3)

The firmware remembers the last Wi-Fi network that connected successfully. It does not require Wi-Fi credentials to be edited in source code.

1. Power on the ESP32-S3. It first tries the saved network for about 15 seconds.
2. If no working network is saved, it creates a temporary WPA2 network named `ESP32-CSI-SETUP`.
3. Open the USB serial monitor at **115200 baud** and copy the line `Temporary setup password: ...`. This key is unique to the device.
4. Connect your computer or phone to `ESP32-CSI-SETUP` using that temporary key. Keep the device connected even if the OS reports that the network has no internet.
5. Open `http://192.168.4.1`, press **بحث** to scan nearby networks, select a **2.4 GHz** network, enter its password, and press **حفظ واتصال**.
6. After the ESP32 receives an IP address, it saves the network credentials in its local NVS storage and closes the temporary setup AP.

The home/office Wi-Fi password is never printed to the serial log or committed to GitHub. The temporary setup AP key is printed only over the USB serial connection. If the saved network is unavailable on a later boot, the setup AP opens again.
