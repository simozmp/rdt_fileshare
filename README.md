# rdt_fileshare: RDT over UDP client/server CLI app for filesharing

This is a simple client/server application written in C, which implements Go Back N protocol in its own `read()`, `write()` functions to reliably send data with a UDP socket.
