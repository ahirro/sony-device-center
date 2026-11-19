# Sony Audio Device Compatibility & Hardware Validation Matrix

This document tracks hardware-level verification and protocol capability support across known Sony headphones and earbuds for the Sony Device Center SDK and desktop applications.

> **Validation Policy**: Devices must never be marked as *Verified* based solely on protocol similarity. Real-world testing with physical hardware and specific firmware versions is required.

## Support Status Legend

- **Verified**: Fully tested and confirmed working with physical hardware on the noted firmware version.
- **Partially Verified**: Core controls confirmed working; specific secondary features pending verification.
- **Expected**: Supported according to reverse-engineered protocol generation and profile registry, but awaiting physical device validation.
- **Unknown**: Protocol compatibility unconfirmed or untested.

---

## Hardware Matrix

| Device | Protocol | Connection | Battery | ANC | Ambient | EQ | DSEE | Firmware | Codec | Speak-to-Chat | Auto Power-Off | Tested Firmware | Tester |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **WH-1000XM3** | V1 | RFCOMM | Verified | Verified | Verified | N/A (V1) | N/A (V1) | Unknown | SBC, AAC, LDAC, aptX | N/A | N/A | 4.5.2 | Community |
| **WH-1000XM4** | V1 | RFCOMM | Verified | Verified | Verified | Verified | Not implemented (V1) | Verified | AAC verified | Verified (on/off; Standard ~30s) | Not implemented (V1) | 3.0.1 | Community (Windows) |
| **WH-1000XM4C** | V2 | RFCOMM | Verified | Partially verified (state change echoed by headset; audible check pending) | Partially verified (as ANC) | Readback verified (10-band, no Clear Bass); writes untested | N/A | Raw query verified (`04 02`); not shown by one-shot `sonyctl info` | AAC (raw query `12 02` = `02`) | Not exposed | Not exposed | 1.1.2 | Maintainer fork (Windows) |
| **WH-1000XM5** | V2 | RFCOMM | Verified | Verified | Verified | Verified | Verified | Verified | SBC, AAC, LDAC | Verified | Verified | 2.3.1 | Core Dev |
| **WH-1000XM6** | V2 | RFCOMM | Intermittent | Readback unresolved | Readback unresolved | Verified (10-band, no Clear Bass) | Expected | Expected | Expected | Expected | Expected | 3.1.5 | Community ([#44](../../pull/44)) |
| **WF-1000XM4** | V2 | RFCOMM | Expected (Dual+Case) | Expected | Expected | Expected | Expected | Expected | SBC, AAC, LDAC | Expected | Expected | — | Awaiting HW |
| **WF-1000XM5** | V2 | RFCOMM | Expected (Dual+Case) | Expected | Expected | Expected | Expected | Expected | SBC, AAC, LDAC | Expected | Expected | — | Awaiting HW |
| **WH-CH720N** | V2 | RFCOMM | Expected | Expected | Expected | Expected | Expected | Expected | SBC, AAC | N/A | Expected | — | Awaiting HW |
| **ULT WEAR** | V2 | RFCOMM | Expected | Expected | Expected | Expected | Expected | Expected | SBC, AAC, LDAC | Expected | Expected | — | Awaiting HW |
| **LinkBuds S** | V2 | RFCOMM | Expected (Dual+Case) | Expected | Expected | Expected | Expected | Expected | SBC, AAC, LDAC | Expected | Expected | — | Awaiting HW |

---

## Protocol Differences Summary

### Protocol V1 (e.g. WH-1000XM3, WH-1000XM4 legacy)
- Fixed command lengths without variable payloads.
- Battery query `0x10 <type>` (returns `0x11`): type `0x00` single, `0x01` dual, `0x02` case.
- Noise control / ambient via `0x66` / `0x67` / `0x68` with inquired type `0x02`: effect byte, then dual/single (`0x02` = NC, `0x00` = ambient at the trailing level).
- 5-band equalizer with Clear Bass via `0x56` / `0x57` / `0x58` with inquired type `0x01` (V2 uses `0x00`).
- Firmware `0x04 0x02` (returns `0x05`), codec `0x18 0x00` (returns `0x19`).
- **Opcode `0x22` is POWER OFF** — must NEVER be transmitted to a V1 device to query battery.
- DSEE (`0xe6 0x02`) and auto power-off (`0xf6 0x04`) do answer on a WH-1000XM4 but are not decoded or exposed yet.
- Speak-to-Chat is Smart Talking Mode: GET `0xf6 0x05` → RET `0xf7 0x05 <kind> <onOff>`, SET enable `0xf8 0x05 0x01 <0|1>`. Enable is **not** inverted. Config SET `0xfc 0x05 0x00 <sensitivity> <focus> <timeout>` is required on enable; without it an XM4 session never times out. `kind 0x02` is an active talking session, not off. Timeout bytes match Headphones Connect: `0x00` ~15s, `0x01` Standard ~30s (what we write today), `0x02` ~1 min, `0x03` do not close. Sensitivity/timeout UI is a nice-to-have.

### WH-1000XM4C (V2, firmware 1.1.2)

The XM4C is **not** a WH-1000XM4: Sound Connect reports "v2 Protocol", and the V1 opcodes get only an ACK. Name matching therefore checks `WH-1000XM4C` before `WH-1000XM4`.

- Noise control uses NCASM inquired type `0x16` (`0x15`, `0x17`, `0x02`, `0x19` and others get an ACK and no reply). GET `66 16` -> RET `67 16 01 <effect> <type> <opaque> <voice> <level>`. NC = effect 1 / type 0; Ambient = effect 1 / type 1; Off = effect 0 / type 0. `<opaque>` read `02` on every capture and is echoed back unchanged on SET (`68 16 01 ...`, read-before-write, same shape as the WF-1000XM4 `0x15` layout). Level is 1-20 (`0x14` = 20).
- Equalizer: both `56 00` and `56 04` return `57 <t> <preset> 0a <10 bands>`, neutral = 6. It is treated as the 10-band layout (inquired `0x04`), no Clear Bass.
- Firmware `04 02` -> `05 02 05 "1.1.2"`; codec `12 02` -> `13 02 02` (AAC). Support-function list `06 00`.
- Debug helper: `sonyctl raw <hex bytes>` sends a payload and prints every reply frame.
- Not yet verified on this device: EQ writes, Focus on Voice, anything outside the list above.

### Protocol V2 (e.g. WH-1000XM5, WF-1000XM4/M5, LinkBuds, ULT WEAR)
- Extended variable-length payload structures.
- **Opcode `0x22` is BATTERY QUERY** (returns `0x23`).
- Subtypes for dual battery channels (left, right) and charging case.
- Dynamic ANC and 20-step Ambient sound level control (`0x66` / `0x67` / `0x68`).
- 5-band graphic equalizer with Clear Bass (`0x56` / `0x57` / `0x58`).
- WH-1000XM6 uses equalizer subtype `0x04`, ten raw band values (0–12), and no Clear Bass. The Qt app and CLI support it; the legacy ImGui client does not.
- DSEE Extreme toggle (`0xe6` / `0xe7` / `0xe8`).
- Speak-to-Chat toggle (`0xf6` / `0xf7` / `0xf8`).
- Auto Power-Off configuration (`0x26` / `0x27` / `0x28`).

---

## Hardware Validation Instructions with `sonyctl`

To validate a newly connected physical device:

1. **Check Bluetooth pairing**:
   ```bash
   bluetoothctl devices Connected
   ```

2. **Run device discovery**:
   ```bash
   sonyctl devices
   ```

3. **Verify profile and capabilities identification**:
   ```bash
   sonyctl info
   ```

4. **Verify battery reporting**:
   ```bash
   sonyctl battery
   ```

5. **Test Noise Control toggles**:
   ```bash
   sonyctl anc on
   sonyctl ambient 10
   sonyctl anc off
   ```

6. **Test Equalizer (V2 devices and WH-1000XM4)**:
   ```bash
   sonyctl eq get
   sonyctl eq preset bass-boost
   sonyctl eq custom 5 1 2 3 4 5
   ```

7. **Test DSEE & Audio features**:
   ```bash
   sonyctl dsee on
   sonyctl dsee off
   ```
