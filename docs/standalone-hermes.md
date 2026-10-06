# Run Hermes independently from the laptop

The M5StickS3 can operate without its USB cable and without the computer that flashed it. It cannot operate without a reachable Hermes backend: the ESP32 does not have enough memory or compute for Hermes, a modern language model, faster-whisper, and Piper.

## Choose the level of independence

### Same home network

Run Hermes on an always-on mini PC, home server, Raspberry Pi, or reused computer. Give that host a reserved LAN address and configure the StickS3 with:

```text
ws://LAN_ADDRESS:8765/gadget
```

This is the simplest design, but the StickS3 works only on networks that can reach that private address.

### Portable across different Wi-Fi networks

Run Hermes on an always-on host and expose the Gadget gateway through a stable TLS-protected endpoint:

```text
wss://hermes.example.com/gadget
```

The firmware supports `wss://` and validates public certificate authorities. Do not expose an unencrypted `ws://` gateway to the public internet.

The upstream SDK documents [Tailscale Funnel](tailscale-funnel.md) as one way to provide a secure endpoint. A reverse proxy or managed tunnel can also work when it preserves WebSocket connections.

## Recommended host

An Intel N100-class mini PC with 16 GB RAM provides a comfortable always-on host for Hermes, local Piper TTS, and local faster-whisper. A Raspberry Pi 5 can work, but local recognition may be slower. Cloud speech providers reduce local compute requirements but introduce provider cost and send audio to that provider.

## Migration checklist

1. Install Hermes on the new host.
2. Authenticate the model provider on that host.
3. Install and enable the Gadget plugin.
4. Configure faster-whisper and Piper, or your chosen speech providers.
5. Start the gateway and verify `hermes gadget info`.
6. Configure firewall, TLS, and a stable hostname.
7. Change the StickS3's Hermes address through phone setup.
8. Approve the device on the new Hermes installation.
9. Test a complete voice turn.
10. Turn off the original laptop and repeat the test.

## Wi-Fi roaming limitation

The current firmware stores one active network. To move elsewhere, start phone setup and replace it. A future roaming implementation should store several credentials, scan for known 2.4 GHz networks, choose the strongest eligible network, and fall back to the temporary setup access point when none are available.

Do not hard-code Wi-Fi credentials in firmware or publish them in a repository.

