# Install Hermes on the M5Stack M5StickS3

This guide takes a factory or recoverable M5StickS3 K150 from source code to a paired Hermes voice device.

## 1. Confirm the hardware

Use the model label and product documentation—not appearance alone. This profile targets the M5Stack **M5StickS3 K150**, which contains an ESP32-S3-PICO-1, 8 MB flash, 8 MB octal PSRAM, a 135×240 ST7789 display, ES8311 audio codec, AW8737 amplifier, and M5PM1 power controller.

StickC, StickC Plus, and StickC Plus2 require different firmware.

## 2. Prepare the gateway computer

Install and log in to Hermes Agent. Then install this repository's Gadget plugin:

```bash
hermes plugins install https://github.com/moghalsaif/on-device-hermes-agent/tree/main/plugin --enable
hermes gateway setup
hermes gadget info
```

Select Hermes Gadget during setup. Record the complete device URL printed by `hermes gadget info`; on a local network it normally resembles `ws://192.168.x.x:8765/gadget`.

The address must identify the computer running Hermes, not the router and not the StickS3. The gateway computer's firewall must allow inbound TCP port 8765 unless you configured another port.

## 3. Prepare the build tools

Install Python 3.10 or newer, Git, and PlatformIO Core. Then:

```bash
git clone https://github.com/moghalsaif/on-device-hermes-agent.git
cd on-device-hermes-agent
python3 -m pip install -e ".[serial]"
```

Check that PlatformIO is available:

```bash
pio --version
```

## 4. Connect and identify the serial port

Use a USB-C data cable. Charge-only cables power the display but cannot flash firmware.

Typical ports:

```text
macOS:   /dev/cu.usbmodem101
Linux:   /dev/ttyACM0
Windows: COM5
```

If the port does not appear, reconnect the cable. If necessary, hold the device reset control long enough to enter its download/recovery mode and try again.

## 5. Build and flash

From the repository root:

```bash
cd firmware/esp32
pio run -e m5stack-sticks3
pio run -e m5stack-sticks3 -t upload --upload-port YOUR_PORT
```

The build should identify an ESP32-S3, 8 MB flash, and 8 MB PSRAM. Do not disconnect power during flashing.

To watch the USB console afterward:

```bash
pio device monitor --port YOUR_PORT --baud 115200
```

## 6. Configure Wi-Fi from a phone

With no saved network, the display opens a temporary setup network automatically.

1. Join the `Hermes-XXXX` network shown on the display.
2. Enter the temporary password shown on the device.
3. Keep the network selected if the phone reports “no internet.”
4. Open `http://192.168.4.1`.
5. Enter a 2.4 GHz Wi-Fi network and its password.
6. Enter the full Hermes Gadget URL from `hermes gadget info`.
7. Choose **Check connection and save**.

The board saves new credentials only after obtaining an IP address. A failed attempt restores the prior configuration.

For a phone hotspot, connect the Hermes gateway computer and StickS3 to the same hotspot. Some guest and managed networks block communication between clients; use another network if they do.

## 7. Pair the device

When the StickS3 displays a pairing code:

```bash
hermes gadget pair
```

Verify the device name and code before approving. Successful pairing is stored across restarts.

## 8. Configure microphone recognition and speech output

Run:

```bash
hermes tools
hermes setup
```

Choose local faster-whisper for speech recognition and Piper for text-to-speech. The `base` recognition model is a sensible balance for an initial setup. Enable automatic TTS for Gadget replies.

If your Hermes release provides direct post-setup commands:

```bash
hermes tools post-setup faster_whisper
hermes tools post-setup piper
```

Restart the gateway. The first recognition request may download a model and take longer.

## 9. Verify the complete path

Hold the front TALK button, speak, and release it. A successful turn has all of these stages:

1. The display shows listening activity.
2. Hermes displays the recognized text.
3. Hermes produces an answer.
4. The display animates while speaking.
5. The onboard speaker plays the complete answer.

Open device settings to run local microphone, speaker, display, and input checks. A local speaker tone bypasses Wi-Fi, Hermes, and TTS, making it useful for separating hardware problems from gateway problems.

## 10. Expected diagnostics

At the USB console, run:

```text
status
diag
```

The important fields should show:

```text
board:       m5stack-sticks3
phase:       online
network:     true
display:     st7789
microphone:  es8311
speaker:     es8311
i2c:         0x18, 0x6e
```

Do not paste unreviewed logs publicly. Remove network names, addresses that identify private infrastructure, tokens, device keys, and conversation content.

## 11. Change networks later

Open device settings and select **Wi-Fi setup**. Join the new temporary setup network and repeat the phone flow. Pairing identity is preserved.

The current firmware stores one active Wi-Fi network. Automatic roaming among several saved networks is planned but is not yet implemented.

## 12. Disconnect USB

After flashing and configuration, USB is not required for normal operation. The device may run from a suitable USB power supply or its supported battery arrangement. Hermes must still be reachable over Wi-Fi.

For operation without the original laptop, continue with [Run Hermes independently](standalone-hermes.md).
