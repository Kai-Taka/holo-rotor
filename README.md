# holo-rotor

Firmware that spins the holoedu cylinder: an nRF52840 Pro Micro (nice!nano v2 or a clone, the same
controller as the Lily58) drives a NEMA 17 (17HS4401) through a TMC2209 in STEP/DIR mode.

It is a plain [Zephyr](https://zephyrproject.org) app (Zephyr is what ZMK runs on) built for
Zephyr's upstream `promicro_nrf52840/nrf52840/uf2` board. The output is a `.uf2` you drag onto the
board, exactly like a ZMK build.

* Step pulses come from the nRF52840 **hardware PWM**, so the step rate is crystal-accurate and
  jitter-free (the TMC2209 MicroPlyer interpolator needs that for smooth motion).
* A small thread ramps speed linearly so the phone and cylinder are never jerked.
* You control it from a serial shell over the USB cable.

## Build on GitHub

Every push runs `.github/workflows/build.yml`. Open the run under **Actions** and download the
`holo-rotor-firmware` artifact (`holo-rotor.uf2`).

## Build locally (WSL / Ubuntu)

```bash
sudo apt install --no-install-recommends git cmake ninja-build python3-venv xz-utils wget
python3 -m venv ~/zephyr-venv && source ~/zephyr-venv/bin/activate
pip install west

mkdir ~/holo-ws && cd ~/holo-ws
git clone <this repo> holo-rotor
west init -l holo-rotor && west update -o=--depth=1 -n
pip install -r zephyr/scripts/requirements-base.txt
west sdk install -t arm-zephyr-eabi      # downloads the Zephyr SDK ARM toolchain once

cd holo-rotor
west build -p -b promicro_nrf52840/nrf52840/uf2 . -d ../build
# -> ../build/zephyr/zephyr.uf2
```

## Flash

1. Double-tap RST (short RST to GND twice). A USB drive called `NICENANO` (or similar) appears.
2. Copy `holo-rotor.uf2` onto it. The board reboots into the firmware.

From WSL, if the drive is `E:` in Windows:

```bash
sudo mkdir -p /mnt/e && sudo mount -t drvfs E: /mnt/e
cp ../build/zephyr/zephyr.uf2 /mnt/e/
```

The bootloader is untouched, so you can always go back to ZMK the same way.

## Wiring

| TMC2209 pin | Connect to | Notes |
|---|---|---|
| STEP | P0.17 | hardware PWM |
| DIR | P0.20 | |
| EN | P0.22 | active low; the firmware keeps the driver off at boot |
| VIO (VDD) | VCC (3.3 V) | the firmware turns on the switched VCC pin (P0.13) at boot |
| GND (logic) | GND | |
| MS1 / MS2 | GND / VCC | sets microsteps, see below |
| VM / GND | 12–24 V supply | put a 100 µF+ electrolytic across VM–GND right at the driver |
| A1 A2 B1 B2 | motor coils | |

P0.06 / P0.08 are kept free for a later TMC2209 UART link.

Never plug or unplug the motor while VM is powered. Set the current with the Vref
potentiometer before the first spin; check your module's Rsense to work out Vref (the module
sheet in this project lists 0.11 Ω).

### Microsteps (MS1/MS2)

| MS2 | MS1 | Microsteps |
|---|---|---|
| GND | GND | 8 (firmware default) |
| GND | VCC | 32 |
| VCC | GND | 64 |
| VCC | VCC | 16 |

If you change the jumpers, tell the firmware with `rotor ustep <n>` so rpm stays correct.
At 8 microsteps, 1000 rpm is a 26.7 kHz step rate; at 16 it is 53.3 kHz. Both are fine for the PWM.

## Shell

Connect to the board's USB serial port at any baud rate (PuTTY or Windows Terminal on
`COMx`; in WSL use `usbipd` to attach it, then `screen /dev/ttyACM0`).

```
rotor rpm 300        spin at 300 rpm (negative reverses), ramped
rotor accel 50       ramp rate in rpm/s (default 100)
rotor status         target / current rpm, step rate, settings
rotor stop           ramp down to 0, then release the coils
rotor estop          cut the driver immediately (the cylinder coasts)
rotor ustep 16       match the MS1/MS2 wiring
```

## Notes for high speed

Without UART the TMC2209 runs in StealthChop, which loses torque at high speed. If the motor
stalls (it buzzes and stops turning) before your target rpm, try a lower `accel`, a higher supply
voltage (24 V), and more current. The planned fix is the UART link, which lets the firmware switch
to SpreadCycle above a speed threshold.
