# otter Users' Guide

Note: otter is presently in Beta.  New features may get integrated more frequently than this guide is updated.

otter is a terminal shell that operates on a POSIX command line, between a TTY client/host and a binary MPipe target/server.  It has three main jobs.

1. Send and receive input over MPipe TTY
2. Parse human input into MPipe binary packets to send
3. Format received MPipe binary packets into readable output to the shell window.

Usage of otter could be compared to usage of the standard, command line version of FTP (ftp).

## Launching

### Help
Assuming you have an otter binary on your system, you can get some standard help documentation:

```
$ ./otter --help
Usage: otter [-pvd] <ttyfile> [<baudrate>] [-e <e.g. 8N1>] [-i <mpipe|modbus>] [-x <filepath>] [-C <file.json>] [--help] [--version]
  <ttyfile>                 Path to tty file (e.g. /dev/tty.usbmodem)
  <baudrate>                Baudrate, default is 115200
  -p, --pipe                Use pipe I/O instead of terminal console
  -e, --encoding=<e.g. 8N1> Manual-entry for TTY encoding (default mpipe:8N1, modbus:8N2)
  -i, --intf=<mpipe|modbus> Select "mpipe" or "modbus" interface (default=mpipe)
  -x, --xpath=<filepath>    Path to directory of external data processor programs
  -C, --config=<file.json>  JSON based configuration file.
  -v, --verbose             Use verbose mode
  -d, --debug               Set debug mode on: requires compiling for debug
  --help                    Print this help and exit
  --version                 Print version information and exit
```

### Opening an Interactive Shell

Opening `otter` as an interactive shell is similar to usage of the `screen` app.  An example is below.

```
$ otter /dev/tty.usbmodem2131 115200
```

* 1st argument is always the ttyfile.
* 2nd argument is always the baudrate.  
* The `-v, --verbose` argument can be added if you want more english-language descriptions about data going in and out of otter.
* Other arguments are optional and are for advanced usage.

### Opening otter as a Subprocess

It is possible to open otter as a subprocess, from within another program or script.  Often this is done using Python (see examples from the otter repository), but any language that supports a popen() API will work.  Open otter with the `-p, --pipe` option from within your app. Have your app monitor otter's stdin & stderr, and output to otter's stdout.

## Minimalist Usage: Reading Input

Truly minimalist usage of otter is merely to start it up and watch data get printed as the connected target device sends data to otter.  In many cases, this is sufficient.  The data that gets printed to otter is completely dependent on the app that's running on the target, although it is customary on OpenTag devices to print out a welcome message on reset (see below):

```
[v][001] SYS_ON
System on and Mpipe active
```

### Interpreting Codes Received on Incoming Packets

In the above example, which is printed by most OpenTag devices when they reset (as long as they have an MPipe peripheral), there are two codes.

1. `[v]` : it means the packet is valid.  CRC passed.  If CRC didn't pass, you would see `[x]` and a hex dump of the packet.
2. `[001]` : Each MPipe packet contains a counter that starts at 1 and is incremented each time a packet is sent.  The purpose is merely to assist software in matching requests with responses.

If you use `-v, --verbose` mode, these codes will be written as english language descriptions.


## Send a Command: Hint, Press Escape

Press escape to open an otter prompt.

```
otter~ 
```

Type a command (`cmdls`) that prints a list of commands your build of otter supports.  Press Enter to run the command.

```
otter~ cmdls
```

Different builds of otter support different commands.  More information about all the known commands, command packages, and how to integrate custom commands will be described in a future session.

### Arrow Keys

At present, otter doesn't have great support for arrow keys.  This is planned for the future.

### Command History

One exception to usage of arrow keys is command history, which can be invoked by using the up and down arrows within an otter command prompt.

