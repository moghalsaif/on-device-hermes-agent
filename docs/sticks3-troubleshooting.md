# M5StickS3 troubleshooting

Work from the symptom toward the layer that failed. Avoid reflashing repeatedly before reading diagnostics; Wi-Fi, gateway, speech software, and speaker hardware are separate systems.

## Display, microphone, and speaker report `none`

This normally means the board was built with the wrong profile or hardware initialization failed.

Rebuild explicitly with:

```bash
cd firmware/esp32
pio run -e m5stack-sticks3
```

Then flash that environment. In `diag`, the display should be `st7789`, and microphone and speaker should both be `es8311`.

## I²C reports `bus stuck`

The StickS3 uses GPIO47/48 with external pull-ups. Its M5PM1 expects 100 kHz access and can occasionally miss a transfer.

This port addresses the problem by:

- disabling unnecessary internal pull-ups;
- using 100 kHz for the PMIC;
- retrying M5PM1 register operations;
- waiting for the LCD/audio rail to settle; and
- probing only the known `0x18` and `0x6e` devices in diagnostics.

If a current build still reports a stuck bus, confirm the exact board model and inspect for hardware damage or an attached accessory loading the bus.

## Wi-Fi lost, reason 201

Reason 201 generally means the configured access point was not found. Check:

- The network is broadcasting 2.4 GHz.
- The name is exact, including capitalization and punctuation.
- A phone hotspot remains enabled and discoverable.
- The board is close enough to the access point.
- The network does not require enterprise authentication or a web login page.

Restart phone Wi-Fi setup and enter the network again. Do not publish the password in screenshots or logs.

## Wi-Fi connects but Hermes stays offline

Wi-Fi connectivity does not prove the gateway is reachable.

- Keep `hermes gateway run` active.
- Use the address of the Hermes computer—not the router.
- Connect both devices to the same LAN for a local `ws://` address.
- Allow the Gadget port through the host firewall.
- If the host address changed, update the device's Hermes address.
- Use a stable `wss://` endpoint for operation across unrelated networks.

## The phone says it cannot join the gadget

Start Wi-Fi setup again so the device creates a fresh temporary network and password. Join the network shown on the current screen; old setup credentials expire. Keep the phone connected despite its “no internet” warning, then open `http://192.168.4.1` manually.

## Speech appears as text, but Hermes does not speak

Confirm that a TTS provider is installed and automatic speech replies are enabled. For local speech:

```bash
hermes tools post-setup piper
```

Restart the gateway afterward. If your Hermes version does not have that helper, choose Piper through `hermes tools`.

## Voice input fails to transcribe

Install or configure speech recognition:

```bash
hermes tools post-setup faster_whisper
```

Start with the `base` model. A first-use model download is expected. If the device's local microphone check responds but Hermes receives no useful transcript, inspect the gateway's speech-recognition configuration before changing firmware.

## Speaking animation appears, but the speaker is silent

Run the local speaker check in device settings.

- If the local tone works, the hardware is healthy; investigate TTS and gateway audio delivery.
- If the local tone is silent, check the firmware profile and amplifier control.

The StickS3's AW8737 enable is not a normal ESP32 pin. It is M5PM1 GPIO3. The ES8311 configuration must use PA pin 3 together with the StickS3 PMIC-backed GPIO interface. The speaker task also needs enough stack for stereo staging and the codec write path; this port allocates 8 KB.

## The device resets when speech starts

Older revisions of this port gave the speaker task only 4 KB of stack. The stereo buffer plus the codec library could overflow it as playback began. Current code allocates 8 KB. `diag` should show useful remaining stack for `hg-spk` after a spoken reply.

## The gateway works only while USB is connected

USB is needed for flashing and diagnostics, not normal communication. If unplugging it stops the device, provide another suitable power source. If the device remains powered but goes offline, check its Wi-Fi and the gateway address.

## Safe recovery sequence

1. Reconnect a known data-capable USB cable.
2. Open the 115200-baud console.
3. Run `status` and `diag`.
4. Confirm the board profile and I²C devices.
5. Reconfigure Wi-Fi before erasing anything.
6. Reflash the current `m5stack-sticks3` build only when firmware is actually suspect.
7. Preserve NVS unless you intentionally want to remove pairing and network settings.

