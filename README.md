# blyncd
blync light device manager

here's a quick cap for now:

* run daemon as root
* default listen port is tcp 4545
* per-connection command sessions
* use command "." to terminate socket from server side
* list devices with command "d"
* list colors with command "c"
* set colors with command "s"

set command accepts optional HID device path after color. so for example to set only one device to color 2 (magenta), you might use:

    $ echo -n "s2,0001:0013:01" | nc localhost 4545
