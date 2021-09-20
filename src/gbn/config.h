#ifndef CONFIG_H
#define CONFIG_H

// Data field size for a packet
#define PCKDATASIZE 65400 //65506
// Window size
#define WIN 5
// Timeout (seconds and nanoseconds)
#define TIMEOUT_NS 50000000
#define TIMEOUT_S 0
// Incoming data buffer size
#define BUFFERSIZE PCKDATASIZE*WIN*2
// Incoming data buffer size
#define LOSS_PROB 0.2

#define ALPHA 0.125
#define BETA 0.25

#endif
