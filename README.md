# Revenge tool


This is a small hid tool that receives the text to insert from BLE NUS.

Every thing you send to the RX char will be output as hid through USB.
The idea is to plug it to the pc without it being visible which fits stationary PCs

# sending..

Use nrf connect on phone, connect to the revenge device and send whatever to the rx char

# special commands

- "\s" - sleep for one second before continue
- "\c" - toggle caps lock
- "\t" - opens terminal (linux only) - sends ctrl+alt+t
- \"r" - opens a terminal, sleep, open rick roll url
- "\n" - send enter
- "\m" - rotates the mouse for 10 seconds
- "\u[URL]" opens terminal then writes `xdg open [URL]` and sends enter 
- "\x[URL]" like the previous but takes over the mouse and rotates it for 60 seconds

