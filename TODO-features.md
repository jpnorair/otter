# Otter Features TODO

## 29 Jan 2019

### Test Multi-TTY interfaces, xnode, and mknode

* Extra Todos

## ~~24 Jan 2019~~

### ~~mknode -p (or similar)~~ Resolved

~~mknode -p should modify the node if it already exists, or create it if it does not.~~

### ~~xnode command~~ Resolved

~~allows changing user/address and running an inline command in a single batched command.~~

~~e.g. xnode user [DEADBEEF] "file r 0"~~

### ~~Support multiple tty interfaces~~

* ~~All devices will be addressed as if they are on a single channel/tty~~
* ~~There will be a single rx and a single tx pktlist~~
* ~~One RX thread for __all__ interfaces (use poll)~~
* ~~One TX thread for __all__ interfaces~~
* ~~Packets inbound on interface X are responded-to on interface X, although otter/smut tends to be an outbound provider of REST messages, in which case this is irrelevant.~~
* ~~Device Table should have interface handle for each device~~

