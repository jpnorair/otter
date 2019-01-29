# Otter Features TODO

## 24 Jan 2019

### ~~mknode -p (or similar)~~ Resolved

~~mknode -p should modify the node if it already exists, or create it if it does not.~~

### ~~xnode command~~ Resolved

~~allows changing user/address and running an inline command in a single batched command.~~

~~e.g. xnode user [DEADBEEF] "file r 0"~~

### Support multiple tty interfaces

* All devices will be addressed as if they are on a single channel/tty
* There will be a single rx and a single tx pktlist
* One RX thread for __each__ interface
* One TX thread for __all__ interfaces
* Packets inbound on interface X are responded-to on interface X
* Device Table should have interface handle for each device

