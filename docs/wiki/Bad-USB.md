# Bad USB

> 📦 **From the original MeowKit firmware.**

Turns the device into a **USB HID keyboard** that types a scripted sequence of
keystrokes into a host computer (Rubber-Ducky style), using the ESP32-S3 native
USB. Scripts live on the SD card.

> 🔐 Only plug into computers you own or are authorised to test.

The device uses TinyUSB (`ARDUINO_USB_MODE=0`); when Bad USB runs it enumerates as
a keyboard and replays the selected layout/script.
