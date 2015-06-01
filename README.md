# blyncd
blync light device manager

here's a quick cap for now:

* run daemon as root
* default listen port is tcp 4545
* per-connection command sessions
* use "." to terminate
* list devices with command "d"
* set colors with command "s"
* set command accepts optional HID device path after color
  example: "s2,xxxx:xxxx:xx" to set only one device to color 2 (magenta)
