# blyncd
blync light device manager

here's a quick cap for now:

* run daemon as root
* default listen port is tcp 4545
* per-connection command sessions
* use command "." to terminate socket from server side
* list devices and their aliases with command "d"
* list colors with command "c"
* set colors with command "s"
* set a device alias with "a"

to get started quickly, connect your devices and ensure they're powered (check `dmesg`), label them A-Z

run `make` and then `sudo ./a.out --debug` and blyncd will list their device IDs in the console

use the set command to turn them all on (white) to make sure they're working

    $ echo "s1" | nc localhost 4545

turn them off again with `"s0"` and then turn them on one at a time using a device ID

    $ echo "s1,0001:0013:01" | nc localhost 4545

identify the label on the lit device and then assign it and turn them off

    $ echo "ax,0001:0013:01s0" | nc localhost 4545

now repeat this procedure until all the labels are matched with device IDs. when you're done you can
verify your work with the device list, as it shows the current alias, if any, for each device

    $ echo "d" | nc localhost 4545
    0001:0013:01 x
    0001:0014:01 y
    0001:0015:01 z

now you're ready to sequence! write a program to open a socket and begin streaming setcolor commands
to blyncd with whatever timing is appropriate to your application!

    "s1,xs0s2,ys0s3,z" ...

note that if you use a line-oriented protocol, the whitespace will cause delay, probably on the scale
of micro- or milliseconds, as it is shunted into the lookahead buffer (one character deep) and comes
"back around" to the top level of the stream parser before being discarded.
