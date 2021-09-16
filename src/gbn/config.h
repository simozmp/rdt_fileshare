#ifndef CONFIG_H
#define CONFIG_H

// Data field size for a packet
#define PCKDATASIZE 65400 //65506
// Window size
#define WIN 5
// Timeout
#define TIMEOUT 50
// Incoming data buffer size
#define BUFFERSIZE PCKDATASIZE*WIN*2
// Incoming data buffer size
#define LOSS_PROB 0.4

#endif
