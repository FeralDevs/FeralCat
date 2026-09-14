# Infrared

An IR remote using the board's IR **TX (GPIO7)** and **RX (GPIO5)** at a 38 kHz
carrier. Record a signal from an existing remote and replay it, or use the bundled
universal-remote database.

Backed by the IRremoteESP8266 library. IR files and the universal database live on
the SD card (`/infrared/`).
