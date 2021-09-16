#include "gbn/gbn_core.h"
#include "gbn/gbn_utils.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>




/*									ENUMS									*/
enum statuses {
	//shared statuses
	closed = 0,
	listening,
	established,
	sending,
	receiving,

	closewait,
	lastack,

	finwait1,
	finwait2,
	timewait,

	// server statuses
	synrcvd,

	// client statuses
	synsent
};




//	Global socket, saved on connection
int connsocket = -1;	//	(should be valid when in established status)

//	This indicates whether the system is initialized (1) or not (0)
int gbn_init = 0;

//	Connection status
int status = closed;

//	Connected peer address and length
struct sockaddr remote_addr;
socklen_t remote_addr_len;

//	GBN variables for receiving

//	A buffer for the ack relative to a receive
servicepkt_t *response_ack = NULL;

//	Listening thread id
pthread_t listening_thread_id;

//	GBN core variables for sending
int base = -1;
int nextseqnum = -1;
int expectedseqn = -1;

datapkt_t* pkt_win;


//	Mutex to work in isolation on GBN core variables
pthread_mutex_t core_mutex = PTHREAD_MUTEX_INITIALIZER;

//	Condition relative to a change of the base variable
pthread_cond_t base_change = PTHREAD_COND_INITIALIZER;

char* log_msg;




/*						PRIVATE FCTNS PROTOTYPES							*/

int deliver_data(datapkt_t *pkt);
void timeout_handler();
void incoming_pkt_handler();
void listen_routine(void* arg);
void launch_listening_thread();

int gbn_core_init();
int gbn_core_fin();
void kill_proc_routine();

ssize_t send_pkt(int socketfd, datapkt_t *pkt);
ssize_t send_ack(int socketfd, servicepkt_t *pkt);

int set_timer(int ms);


/*							IMPLEMENTATIONS									*/

/*
 *	This implements the 3-way handshake client side
 *
 */
int gbnc_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen) {

	servicepkt_t buffer_pkt;
	char* servaddr_p = malloc(40*sizeof(char));
	char* timestring = malloc(30*sizeof(char));
	char* log_name = malloc(90*sizeof(char));
	log_msg = malloc(400*sizeof(char));

	//	Opening log file
	timestamp(timestring);
	sprintf(log_name, "%s_gbn_core_log_client", timestring);
	if(log_open(log_name) < 0)
		perror("Error opening the log file!");

	//	Saving the presentation for the server address
	if(inet_ntop(AF_INET, servaddr->sa_data, servaddr_p, 40*sizeof(char)) <= 0)
		perror("ntop\n");

	sprintf(log_msg, "Connection attempt. Trying to reach server at %s", servaddr_p);
	log_write(log_msg);

	// Sending SYN
	make_servicepkt(0, SYN, &buffer_pkt);
	if(sendto(socketfd, (void*) &buffer_pkt, sizeof(buffer_pkt), 0, servaddr,
			addrlen) < 0) {
		perror("sendto in gbn_connect");
	}

	log_write("Response SYN sent, waiting for SYNACK.");

	status = synsent;

	// Waiting for SYNACK
	do {
		if(recvfrom(socketfd, &buffer_pkt, sizeof(buffer_pkt), 0, NULL, 0) < 0) {
			perror("recvfrom in gbn_connect");
		}
	} while(buffer_pkt.type != SYNACK);

	log_write("SYNACK received. ACK-ing it.");

	// Resetting buffer_pkt
	memset(&buffer_pkt, 0, sizeof(buffer_pkt));

	// Sending ACK
	make_servicepkt(0, ACK, &buffer_pkt);
	if(sendto(socketfd, (void*) &buffer_pkt, sizeof(buffer_pkt), 0, servaddr,
			addrlen) < 0) {
		perror("sendto in gbn_connect");
	}

	status = established;
	connsocket = socketfd;

	sprintf(log_msg, "Connection established. Remote address: %s", servaddr_p);
	log_write(log_msg);

	// Connection data init
	remote_addr = servaddr[0];
	remote_addr_len = sizeof(remote_addr);
	gbn_core_init();

	//	Initialize the rcv_buffer
	if(rcv_buffer_init(BUFFERSIZE)<0)
		perror("rcv_buffer_init");

	launch_listening_thread();

	free(timestring);
	free(servaddr_p);
	free(log_name);

	return 1;
}

/*
 *	This implements the 3-way handshake server side
 *
 */
int gbnc_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen) {

	servicepkt_t buffer_pkt;
	char* remote_addr_p = malloc(40*sizeof(char));
	char* timestring = malloc(30*sizeof(char));
	char* log_name = malloc(90*sizeof(char));
	log_msg = malloc(400*sizeof(char));

	int return_value = -1;

	errno = 0;		// Zero out errno


	//	Opening log file
	timestamp(timestring);
	sprintf(log_name, "%s_gbn_core_log_server", timestring);
	if(log_open(log_name) < 0)
		perror("Error opening the log file!");

	status = listening;

	log_write("Waiting for incoming connections.");
	log_println();

	// Waiting for a SYN packet (connection request)
	do {
		memset(&buffer_pkt, 0, sizeof(buffer_pkt));

		if(recvfrom(socketfd, (unsigned char*) &buffer_pkt,
				sizeof(servicepkt_t), 0, addr, &addrlen) < 0) {
			perror("recvfrom in gbn_accept");
		}
	} while(buffer_pkt.seqn != 0 && buffer_pkt.type != SYN);

	log_write("Connection incoming, allocating resources.");

	status = synrcvd;

	//	Connection data init
	connsocket = socketfd;
	remote_addr = addr[0];
	remote_addr_len = sizeof(remote_addr);
	connect(connsocket, &remote_addr, remote_addr_len);

	//	Initialize the rcv_buffer
	if(rcv_buffer_init(BUFFERSIZE)<0)
		perror("rcv_buffer_init");

	// Resetting buffer_pkt
	memset(&buffer_pkt, 0, sizeof(buffer_pkt));

	log_write("Accepting.");

	//	Sending SYNACK to accept
	make_servicepkt(0, SYNACK, &buffer_pkt);
	if(write(socketfd, (void*) &buffer_pkt, sizeof(buffer_pkt)) < 0) {   // != sizeof(buffer_pkt)) {
		perror("synack sending in gbn_accept");
		// TODO: Handle error
	}

	//	Resetting buffer_pkt
	memset(&buffer_pkt, 0, sizeof(buffer_pkt));

	log_write("Waiting for ACK to finalize connection..");

	//	Waiting for ACK
	do {
		if(read(socketfd, (unsigned char*) &buffer_pkt, sizeof(servicepkt_t)) < 0)
			perror("recvfrom in gbn_accept");
	} while(buffer_pkt.seqn != 0 || buffer_pkt.type != ACK);

	if(gbn_core_init() < 0)
		perror("gbn_core_init");

	//	Saving the presentation for the server address
	if(inet_ntop(AF_INET, remote_addr.sa_data, remote_addr_p, 40*sizeof(char)) <= 0)
		perror("ntop\n");

	sprintf(log_msg, "Connection established. Remote address: %s", remote_addr_p);
	log_write(log_msg);

	status = established;

	return_value = 1;

	launch_listening_thread();

	free(remote_addr_p);
	free(timestring);
	free(log_name);

	return return_value;
}

int gbn_verify_socket(int socket) {
	return socket == connsocket ? 1 : -1;
}

/*
 *	This function follows from the gbn FSM, it is used to send a single
 *	packet
 *
 */
ssize_t gbn_send(void *data, size_t len) {
	ssize_t return_value = 0;
	ssize_t sent = 0;
	datapkt_t pkt;

	errno = 0;		// Zero out errno

	if(connsocket != -1) {

		if(len <= PCKDATASIZE) {	// If data fits a packet, send

			make_datapkt(nextseqnum, data, len, &pkt);

			//	Atomic core_mutex variables manipulation start--------------
			pthread_mutex_lock(&core_mutex);

			//	While the window is full, wait for base to change
			while(nextseqnum >= base + WIN) {
				log_write("gbn_send(): Waiting for window to move.");
				log_println();
				pthread_cond_wait(&base_change, &core_mutex);	// NB: this releases the mutex
			}

			//	Push the packet in the buffer
			if(snd_buffer_push(&pkt) == 0) {

				log_println();
				char* snd_buf_p_str = malloc(sizeof(char)*((WIN*10)+1));
				snd_buf_p(snd_buf_p_str);
				sprintf(log_msg, "gbn_send(): Pkt pushed in the sending buffer: %s", snd_buf_p_str);
				free(snd_buf_p_str);
				log_write(log_msg);

				//	Send the packet
				if((sent = send_pkt(connsocket, &pkt)) >= 0) {

					//	Start timer for first packet of the window
					if(base == nextseqnum) {

						sprintf(log_msg, "gbn_send(): Timeout started for packet n. %d.", base);
						log_write(log_msg);
						if(set_timer(TIMEOUT))
							perror("set_timer()");
					}
					
					//	Update nextseqnum
					nextseqnum++;

					return_value = sent;
				} else {
					sprintf(log_msg, "gbn_send(): Failed to send data!");
					log_write(log_msg);

					perror(log_msg);
					errno = EIO;
					return_value = -1;
				}


				pthread_mutex_unlock(&core_mutex);
				//	Atomic core_mutex variables manipulation end----------------

			} else {	//	This else branch should never occur as it comes after a base_change
						//	condition broadcast, that comes after an ack that frees some space in the buffer
				sprintf(log_msg, "gbn_send(): Error pushing pkt to snd_buffer. This is an anomaly!");
				log_write(log_msg);

				perror(log_msg);
				return_value = -1;
			}

		} else {	//	This as well souldn't occur, as this function (gbn_send()) shall be only
					//	invoked by gbn_write() in gbn.c, which checks the packet's size
			log_write("gbn_send(): Message too long to fit a packet.");
			return_value = -1;
			errno = EMSGSIZE;
		}

	} else {
		log_write("gbn_send(): Socket not connected.");
		return_value = -1;
		errno = ENOTCONN;
	}

	return return_value;
}

ssize_t send_pkt(int socketfd, datapkt_t *pkt) {

	int return_value;

	double random_norm = (double)rand() / (double)RAND_MAX;

	if(random_norm > LOSS_PROB) {
		sprintf(log_msg, "send_pkt(): Sending pkt n. %d.", pkt->seqn);
		log_write(log_msg);

		if(verify_datapkt(pkt) < 0) {
			log_write("send_pkt(): Corrupted packet, can't send.");
			return_value = -1;
		} else
			return_value = sendto(socketfd, pkt, sizeof(datapkt_t), 0, &remote_addr,
				remote_addr_len);
	} else {
		sprintf(log_msg, "send_pkt(): Sending pkt n. %d. Loss simulation ON.", pkt->seqn);
		log_write(log_msg);

		return_value = 1;
	}

	return return_value;
}

ssize_t send_ack(int socketfd, servicepkt_t *pkt) {

	sprintf(log_msg, "send_ack(): Acking pkt n. %d.", pkt->seqn);
	log_write(log_msg);


	return sendto(socketfd, pkt, sizeof(servicepkt_t), 0, &remote_addr,
		sizeof(struct sockaddr));
}

int deliver_data(datapkt_t *pkt) {
	ssize_t return_value = 0;

	errno = 0;		// Zero out errno

	if((return_value = rcv_buffer_write(&pkt->payload, pkt->len)) < 0) {
		sprintf(log_msg, "deliver_data(): Error delivering packet n.%d.", pkt->seqn);
		log_write(log_msg);
		perror("deliver_data");
	}
	else {
		sprintf(log_msg, "deliver_data(): Delivering packet n.%d.", pkt->seqn);
		log_write(log_msg);
	}

	return return_value;
}

/*
 *	Handles the timeout event
 *
 */
void timeout_handler() {

	int buf_len;

	errno = 0;	// Zero out errno

	if(connsocket != -1) {

		log_println();
		log_write("timeout_handler(): Timeout reached, trying to re-send the window");
		//	Start timer again
		if(set_timer(TIMEOUT) < 0)
			perror("set_timer()");

		//	Backup window pkts (not doing so will cause some pkts retransmission as valid acks arrives)
		for(buf_len=0; buf_len<=WIN; buf_len++)
			if(snd_buf_get(pkt_win+buf_len, buf_len))
				break;

		// Send again all packets in the window (buf_len is useful for sending only valid pkts)
		for(int i=0; i<buf_len; i++) {
			sprintf(log_msg, "timeout_handler(): About to send pkt n.%d.", pkt_win[i].seqn);
			log_write(log_msg);
			send_pkt(connsocket, pkt_win+i);
		}
	} else {
		errno = ENOTCONN;
	}
}

/*
 *	Handles incoming packets
 *
 */
void incoming_pkt_handler() {

	// Pointers for packet handling
	datapkt_t *datapkt;
	servicepkt_t *servicepkt;

	// Pointer to a buffer
	unsigned char *buffer_pkt;

	//	Bytes read
	int n;

	// Allocate space for the biggest packet, datapkt_t
	buffer_pkt = malloc(sizeof(datapkt_t));

	memset(buffer_pkt, 0, sizeof(datapkt_t));	//	Zero out buffer_pkt

	log_println();

	// Read packet
	if((n = read(connsocket, buffer_pkt, sizeof(datapkt_t))) >= 0) {

		// Check packet category
		switch(n) {

			// Data packet handling
			case sizeof(datapkt_t):

				datapkt = (datapkt_t*) buffer_pkt;

				// Verify _checksum for datapkt
				if(verify_datapkt(datapkt) == 1) {

					//	Handling packet
					if(datapkt->seqn == expectedseqn) {

						sprintf(log_msg, "incoming_pkt_handler(): datapkt incoming. seqn:%d. OK. Delivering.",
								datapkt->seqn);
						log_write(log_msg);

						// Deliver data to upper layer
						if(deliver_data(datapkt) < 0)
							log_write("incoming_pkt_handler(): Error delivering data to receiving buffer");

						// Update response_ack
						make_servicepkt(datapkt->seqn, ACK, response_ack);

						// Update expectedseqn (in isolation)
						pthread_mutex_lock(&core_mutex);
						expectedseqn++;
						pthread_mutex_unlock(&core_mutex);
					} else {
						sprintf(log_msg, "incoming_pkt_handler(): datapkt incoming. seqn:%d. Not delivering as it is not the packet I expect (%d).",
								datapkt->seqn, expectedseqn);
						log_write(log_msg);
					}

					// Send response_ack
					if(send_ack(connsocket, response_ack) != sizeof(servicepkt_t))
						log_write("incoming_pkt_handler(): Error sending ACK.");
				} else {
					log_write("incoming_pkt_handler(): Received a corrupted datapkt. Ignoring.");
				}
				break;

			// Service packet handling
			case sizeof(servicepkt_t):

				servicepkt = (servicepkt_t*) buffer_pkt;

				// Verifying checksum for servicepkt
				if(verify_servicepkt(servicepkt) == 1) {

					//	Verifying pkt type
					if(servicepkt->type == ACK) {

						//	Verifying seqn
						if(servicepkt->seqn < base + WIN &&
							servicepkt->seqn >= base) {

							//	Move window (in isolation)----------------------
							pthread_mutex_lock(&core_mutex);


							sprintf(log_msg, "incoming_pkt_handler(): Received a valid ack (packet %d). Moving window.", servicepkt->seqn);
							log_write(log_msg);

							base = servicepkt->seqn + 1;
							if(base == nextseqnum) {
								if(set_timer(0) < 0)
									perror("set_timer()");
								sprintf(log_msg, "incoming_pkt_handler(): Timer for packet %d stopped.", servicepkt->seqn);
								log_write(log_msg);
							} else {
								if(set_timer(TIMEOUT) < 0)
									perror("set_timer()");
								log_write("incoming_pkt_handler(): Timeout started.");
							}

							snd_buf_ack(servicepkt->seqn);


							//	Signal the base change
							pthread_cond_broadcast(&base_change);



							char* snd_buf_p_str = malloc(sizeof(char)*((WIN*10)+1));
							snd_buf_p(snd_buf_p_str);
							sprintf(log_msg, "incoming_pkt_handler(): New base: %d. snd_buf is now %s", base, snd_buf_p_str);
							free(snd_buf_p_str);
							log_write(log_msg);


							pthread_mutex_unlock(&core_mutex);
							//	------------------------------------------------
						} else {
							sprintf(log_msg, "incoming_pkt_handler(): Received a duplicate ack (%d).", servicepkt->seqn);
							log_write(log_msg);
						}

					} else {
						log_write("incoming_pkt_handler(): Not an ack. ignoring.");
					}

				break;

			// Not recognized packet, discarded (?)
			default:
				log_write("incoming_pkt_handler(): Unrecognized pkt received. (?)");
				break;
			}
		}
	} else {
		log_write("incoming_pkt_handler(): Read failed.");
	}

	free(buffer_pkt);
}

int wait_delivery() {
	pthread_mutex_lock(&core_mutex);

	//	While the window is full, wait for base to change
	while(base < nextseqnum) {
		log_write("wait_delivery(): Waiting for the last acks to arrive.");
		log_println();
		pthread_cond_wait(&base_change, &core_mutex);	// NB: this releases the mutex
	}

	log_write("wait_delivery(): All packets ACKed, transmission ended.");

	pthread_mutex_unlock(&core_mutex);

	return 0;
}

/*
 *	This is the function that will be run on a separate thread,
 *	to catch incoming data via the select() syscall
 *
 */
void listen_routine(void* arg) {
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(connsocket, &readset);

	log_write("listen_routine(): Listening for incoming packets.");

	while(select(connsocket + 1, &readset, NULL, NULL, NULL) == 1) {
		raise(SIGUSR1);
	}

	log_write("listen_routine(): select() error. listen_routine blocked.");
}

/*
 *	A function to launch the listening thread
 *
 */
void launch_listening_thread() {

	struct sigaction sa;

	// Bind incoming_pkt_handler function to SIGUSR1
	sa.sa_handler = incoming_pkt_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

	if(sigaction(SIGUSR1, &sa, NULL) == -1)
        perror("sigaction");

	pthread_create(&listening_thread_id, NULL, (void*)listen_routine, NULL);
}

/*
 *	A function to cancel the listening thread
 *
 */
void kill_listening_thread() {

	//	Killing the listening thread
	pthread_cancel(listening_thread_id);

	//	Unregister handler from SIGUSR1
	if(sigaction(SIGUSR1, NULL, NULL) == -1)
        perror("sigaction");
}

int gbn_core_init() {
	int return_value;

	struct sigaction on_sigint, old_action;

	on_sigint.sa_handler = kill_proc_routine;
	sigemptyset(&on_sigint.sa_mask);
	on_sigint.sa_flags = 0;


	if(gbn_init != 1) {

		//	Binding timeout handler
		signal(SIGALRM, timeout_handler);
		sigaction(SIGINT, &on_sigint, &old_action);
		
		//	Allocating memory for the incoming pkts ack
		response_ack = malloc(sizeof(servicepkt_t));

		//	Allocate the send buffer for unacked pkts (of size WIN)
		snd_buffer_init(WIN);

		pthread_mutex_lock(&core_mutex);

		pkt_win = malloc(WIN*sizeof(datapkt_t));

		base = 1;
		nextseqnum = 1;
		expectedseqn = 1;

		return_value = 0;
		gbn_init = 1;

		pthread_mutex_unlock(&core_mutex);
	} else
		return_value = -1;

	return return_value;
}

int gbn_core_fin() {	//	TODO: add a connection end protocol

	int return_value;

	if(gbn_init == 1) {

		log_println();
		log_write("Finalizing connection.");

		//	Dellocating memory for the incoming pkts ack
		free(response_ack);

		//	Destroying snd_buf
		snd_buffer_destroy(WIN);

		pthread_mutex_lock(&core_mutex);

		base = -1;
		nextseqnum = -1;
		expectedseqn = -1;

		gbn_init = 0;
		return_value = 0;

		free(pkt_win);
		free(log_msg);

		pthread_mutex_unlock(&core_mutex);

		log_write("Closing log..");
		log_dump();

	} else
		return_value = -1;

	return return_value;
}

void kill_proc_routine() {
	printf("\nTermination occurred.\n");
	gbn_core_fin();
	printf("Killing procedure terminated.\n");
	exit(1);
}

int set_timer(int ms) {
	struct itimerval timer_val;
	struct itimerval timer_val_old;

	timer_val.it_interval.tv_usec = timer_val.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once

	timer_val.it_value.tv_sec = 0;
	timer_val.it_value.tv_usec = ms*1000;

	return setitimer(ITIMER_REAL, &timer_val, &timer_val_old);
}
