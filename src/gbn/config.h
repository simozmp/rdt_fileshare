#ifndef CONFIG_H
#define CONFIG_H

//	Window size
#define WIN				10

//	Default timeout (seconds and nanoseconds)
#define TIMEOUT_S		0
#define TIMEOUT_NS		50000000

//	Incoming data buffer size
#define LOSS_PROB		0.4

//	Rtt estimation function flag (active: 1, not active: 0)
#define RTT_EST			1


/*
 *	The following macros should not be modified
 *
 */

//	Data field size for a packet
#define PCKDATASIZE		65400 	//	65535 is UDP pkt size

//	Incoming data buffer size for UDP socket
#define BUFFERSIZE		PCKDATASIZE*WIN

//	Alpha and Beta parameters for rtt estimation
#define ALPHA			0.125
#define BETA			0.25

#endif
