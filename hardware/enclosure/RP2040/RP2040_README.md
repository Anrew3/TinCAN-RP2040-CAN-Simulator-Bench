# RP2040 README

### Why RP2040? Over ESP32??
  This board & hardware was chosen because its readily accesible from adafruit, mouser etc...

### Why ESP32? Over RP2040?
  Because of the complexity of soldering, stuffing wires in there, and lack of bluetooth & wifi...

## How do I build this?
  - Gather the specified hardware, 3d print items.3D PRINT...(Shell, Carry Tray, Backplate)
  - Use the Firmare Flashing Tool found below to flash the rp2040 firmware.

## Hardware List (Required)
  - Adafruit RP2040 Canbus Board [Adafruit_RP2040CAN](https://www.adafruit.com/product/5724) , [Mouser_RP2040CAN](https://mou.sr/3xuF0uN)
  - Adafruit MPM3610 21V -> 5V [Adafruit_MPM3610](https://www.adafruit.com/product/4739) , [Mouser_MPM3610](https://mou.sr/3Vck6YR)
  - Molex 6P Connector [Mouser_Molex6P](https://mou.sr/3JtCprp)
  - Barrel Jack [Amazon_BarrelJack](https://www.amazon.com)
  - CASE... 3D Print or CNC Aluminum [3D Print](https://github.com/Anrew3/TinCAN-RP2040-CAN-Simulator-Bench/hardware/enclosure/Carry_Tray_RP2040.3mf) , [CNC Aluminum](https://www.jlcpcb.com)
  - M2 & M4 Screws (4x M4x.7mmx15mm, 5x M2x.7mmx5mm)

## Hardware List (Optional)
  - JST Headers [Amazon_JSTHeaders](https://www.amazon.com)
  - ESP32 WIFI Addon [Adafruit_ESP32](https://www.adafruit.com/product/5395)

## Internal Wiring (TINCAN)
![Screenshot](docs/images/TinCAN_Wiring.jpg)

## Flashing Firmware

Use this site to flash the firmware via USB once the device is built. If you have issues you can use the Arduino IDE as well to build & upload manually.

<div align="center">
  <a href="https://anrew3.github.io/TinCAN-RP2040-CAN-Simulator/tincan-flasher.html"
     target="_blank"
     rel="noopener noreferrer">
    <img src="https://img.shields.io/badge/🔧%20Flash%20Firmware-Open%20Flasher-blue?style=for-the-badge&logo=browser&logoColor=white"
         alt="Open TinCAN Flasher">
  </a>
</div>
