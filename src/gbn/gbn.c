#include "gbn.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

//	Loading library modules
#include "gbn_core.h"
#include "packet.h"
#include "rcv_buffer.h"
#include "snd_buf.h"





/*							IMPLEMENTATIONS									*/

/*
 *	This functions returns a gbn compatible socket, or -1 in case of error
 *	(errno will be set appropriately).
 *
 */
int gbn_socket() {
	int return_value = socket(AF_INET, SOCK_DGRAM, 0);

	errno = 0;		//	Zero out errno

	/*
	 *	Before successfully return the socket, the following tries and change
	 *	the receiving buffer size for the UDP socket: this will prevent packets
	 *	discards on receiver side (and induced retransmissions)
	 *
	 */
	int receiver_buffer_size = sizeof(datapkt_t)*WIN;

	if(setsockopt(return_value, SOL_SOCKET, SO_RCVBUF,
			&receiver_buffer_size, sizeof(receiver_buffer_size)) < 0)
		return_value = -1;

	return return_value;
}

/*
 *	connect() implementation for gbn socket
 *
 */
int gbn_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen) {
	return gbnc_connect(socketfd, servaddr, addrlen);
}

/*
 *	accept() implementation for gbn socket
 *
 */
int gbn_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen) {

	return gbnc_accept(socketfd, addr, addrlen);
}


/*
 *	shutdown() implementation for gbn socket
 *
 */
int gbn_shutdown(int socketfd) {

	return gbnc_shutdown(socketfd);
}

/*
 *	close() implementation for gbn socket
 *
 */
int gbn_close(int socketfd) {

	return gbnc_close(socketfd);
}

/*
 *	This function splits the message into pieces, and then sends them one by
 *	one by calling the lower level core function gbnc_send()
 *
 *	The function returns the actual number of bytes that has been sent, or -1
 *	in case of error. (errno will be set appropriately)
 *
 */
ssize_t gbn_write(int socket, void *buf, size_t len) {

	int pieces = 1;
	ssize_t data_sent = -1;
	size_t data_left;
	size_t data_len;
	int now_sent = 0;

	errno = 0;		// Zero out errno

	if(gbn_verify_socket(socket) < 0) {		// Verifying the socket
		errno = ENOTSOCK;	//	"Socket operation on non-socket" (non-gbn sock)
	} else {

		data_sent = 0;

		// Computing number of pieces (for multiple pkts data)
		if(len > PCKDATASIZE)
			pieces += (int) len / PCKDATASIZE;

		for(int i=0; i<pieces; i++) {
			data_left = len - i*PCKDATASIZE;

			// Computing data field lenght for the current packet
			data_len = PCKDATASIZE < data_left ? PCKDATASIZE : data_left;

			if((now_sent = gbnc_send(buf + (i*PCKDATASIZE), data_len)) == -1) {
				perror("gbn_send");
			}

			data_sent += data_len;
		}
	}

	//	Waiting for all pkts to be acked
	wait_delivery();

	return data_sent;
}

/*
 *	This functions tries to read up to count bytes from the output buffer,
 *	into the buffer starting at buf
 *
 */
ssize_t gbn_read(int socket, void *buf, size_t count) {

	ssize_t data_read = -1;			// For return value

	//	Error handling
	if(buf == NULL)
		errno = EFAULT;			//	"Bad address"

	else if(gbn_verify_socket(socket) < 0)
		errno = ENOTSOCK;		//	"Socket operation on non-socket"

	else
		//	Fetching count bytes from receiver buffer
		if((data_read = rcv_buffer_fetch(buf, count)) < 0) {
			perror("gbn_read");
		}

	return data_read;
}
