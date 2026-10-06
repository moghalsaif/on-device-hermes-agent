![Hermes connected to an M5StickS3](assets/readme/hermes-sticks3-banner.png)

# On-device Hermes Agent for M5StickS3 and ESP32

A working, source-buildable Hermes Gadget firmware port for the **M5Stack M5StickS3 (K150)**, plus a practical installation and troubleshooting guide.

This repository is based on [Adolanium/hermes-gadget-sdk](https://github.com/Adolanium/hermes-gadget-sdk). It adds the StickS3 board profile and the hardware fixes required for its display, M5PM1 power controller, ES8311 microphone and speaker codec, AW8737 amplifier, buttons, Wi-Fi, and spoken Hermes replies.

> **Status:** Experimental but physically exercised on one M5StickS3 K150. Display, buttons, Wi-Fi, microphone transcription, text responses, Piper speech synthesis, and onboard speaker playback have worked together. A full long-duration hardware verification report is still pending.

## What works

| Capability | Status |
|---|---|
| ST7789 135×240 display | Working |
| Front TALK button | Working |
| Side AUX/CANCEL button | Working |
| Wi-Fi and phone setup | Working |
| Hermes pairing and reconnect | Working |
| ES8311 onboard microphone | Working |
| ES8311 + AW8737 onboard speaker | Working |
| Local Piper text-to-speech | Working |
| Local faster-whisper speech recognition | Working |
| Battery readings and software power-off | Not implemented |

## Architecture

The StickS3 is the voice terminal, not the AI computer:

```text
M5StickS3 microphone
        ↓
Wi-Fi → Hermes Gadget gateway → speech recognition → Hermes model
        ↑                                      ↓
M5StickS3 speaker ← streamed PCM audio ← text-to-speech
```

Hermes currently runs on a Mac, Linux computer, home server, or another always-on host. For a portable device that works away from your laptop, see [Run Hermes independently](docs/standalone-hermes.md).

## Quick start

You need:

- An exact M5Stack M5StickS3 K150—not a StickC or StickC Plus model.
- A data-capable USB-C cable.
- Python 3.10 or newer and [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).
- Hermes Agent installed and logged in on the gateway computer.
- A 2.4 GHz Wi-Fi network. The StickS3 does not join 5 GHz-only or enterprise networks.

Clone and install the local tools:

```bash
git clone https://github.com/moghalsaif/on-device-hermes-agent.git
cd on-device-hermes-agent
python3 -m pip install -e ".[serial]"
```

Build and flash:

```bash
cd firmware/esp32
pio run -e m5stack-sticks3
pio run -e m5stack-sticks3 -t upload --upload-port /dev/cu.usbmodem101
```

Use the serial port shown on your computer. Common names are `/dev/cu.usbmodem…` on macOS, `/dev/ttyACM…` on Linux, and `COM…` on Windows.

Install the matching Hermes plugin on the gateway computer:

```bash
hermes plugins install https://github.com/moghalsaif/on-device-hermes-agent/tree/main/plugin --enable
hermes gateway setup
hermes gadget info
```

Keep the gateway running. After the first flash, the StickS3 displays a temporary `Hermes-XXXX` Wi-Fi network and password. Join it from a phone, keep the network selected when the phone warns that it has no internet, and open:

```text
http://192.168.4.1
```

Enter the 2.4 GHz network, its password, and the complete gateway address printed by `hermes gadget info`. Pair the device when its code appears:

```bash
hermes gadget pair
```

For the complete walkthrough, expected diagnostics, and recovery steps, read [Install Hermes on the StickS3](docs/m5sticks3-install-guide.md).

## Enable speech

In Hermes, configure:

- Speech-to-text: local faster-whisper, with the `base` model as a practical starting point.
- Text-to-speech: Piper.
- Automatic TTS for Gadget replies.

The interactive route is:

```bash
hermes tools
hermes setup
```

On Hermes versions that expose the post-setup helpers, these commands install the local engines:

```bash
hermes tools post-setup faster_whisper
hermes tools post-setup piper
```

Restart the Hermes gateway after changing speech providers. The first transcription downloads the selected recognition model and can take longer than later requests.

## Hardware details

| Part | StickS3 connection |
|---|---|
| Display | ST7789 over SPI; MOSI 39, SCLK 40, CS 41, DC 45, RESET 21, backlight 38 |
| I²C | SDA 47, SCL 48, 100 kHz, external pull-ups |
| ES8311 | Address `0x18`; MCLK 18, BCLK 17, WS 15, DOUT 14, DIN 16 |
| M5PM1 | Address `0x6e`; GPIO2 supplies LCD/audio and GPIO3 enables the AW8737 amplifier |
| Buttons | Front TALK 11; side AUX/CANCEL 12 |

The complete implementation is in:

- [`firmware/esp32/main/port_sticks3.cpp`](firmware/esp32/main/port_sticks3.cpp)
- [`firmware/esp32/main/port_codec.cpp`](firmware/esp32/main/port_codec.cpp)
- [`firmware/esp32/main/board.cpp`](firmware/esp32/main/board.cpp)
- [`firmware/esp32/boards/m5stack-sticks3/`](firmware/esp32/boards/m5stack-sticks3/)

## Problems already solved by this port

- M5PM1 I²C timeouts and “bus stuck” diagnostics.
- Powering the LCD and audio rail in the correct order.
- Avoiding a full I²C scan that can upset the M5PM1.
- Converting the ES8311 duplex stereo stream to Hermes mono audio.
- Enabling the external AW8737 amplifier through M5PM1 GPIO3.
- Preventing the speaker playback task from overflowing its stack.
- Recovering from Wi-Fi reason 201 and changing networks through phone setup.
- Installing missing Piper and faster-whisper runtime components.

See [StickS3 troubleshooting](docs/sticks3-troubleshooting.md) for symptoms, causes, and fixes.

## Other ESP32 boards

The underlying Hermes Gadget SDK supports several ESP32-S3 devices and custom boards. Start with [supported hardware](docs/supported-hardware.md), then use [the porting guide](docs/porting.md) for a new display, codec, microphone, speaker, or button layout.

Do not flash the StickS3 profile onto a different M5Stack model merely because its enclosure looks similar. Pins, flash layout, power controllers, and displays differ.

## Security

- Never commit Wi-Fi passwords, device keys, tokens, pairing codes, or private transcripts.
- Use `wss://` for a gateway reachable outside a trusted local network.
- Verify the pairing code on the physical display before approving a device.
- Sanitize diagnostics before publishing them.

## License

This project preserves the upstream history and licensing of [Hermes Gadget SDK](https://github.com/Adolanium/hermes-gadget-sdk). M5StickS3 hardware facts follow [M5Stack’s StickS3 documentation](https://docs.m5stack.com/en/core/StickS3) and its K150 schematic. See [LICENSE](LICENSE), [NOTICE](NOTICE), and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
