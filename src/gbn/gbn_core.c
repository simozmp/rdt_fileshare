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

//		A buffer for the ack relative to a receive
servicepkt_t *response_ack = NULL;

//		Listening thread id
pthread_t listening_thread_id;

//		GBN core variables for sending
int base = -1;
int next_seqn = -1;
int expectedseqn = -1;

//	A buffer to store all the pkts in snd_buf during retransmission phase
datapkt_t* pkt_win;

//	Timespec variables to estimate the rtt, and update the timer timeout
struct timespec rtt_est;
struct timespec rtt_dev;
struct timespec timer_timeout;


//	Mutex to work in isolation on GBN core variables
pthread_mutex_t core_mutex;
//	mutex owners string, for deadlock detection (printed on sigint occurrence)
char* mutex_owner;

//	Condition relative to a change of the base variable
pthread_cond_t base_change = PTHREAD_COND_INITIALIZER;

//	A string to temporarly store a message for the log file
char* log_msg;




/*						PRIVATE FCTNS PROTOTYPES							*/

int deliver_data(datapkt_t *pkt);
void timeout_handler();
void incoming_pkt_handler();
void listen_routine(void* arg);		//	On separate thread
void launch_listening_thread();

void retransmission();				//	On separate thread

int gbn_core_init();
int gbn_core_fin();
void kill_proc_routine();

ssize_t send_pkt(int socketfd, datapkt_t *pkt);
ssize_t send_ack(int socketfd, servicepkt_t *pkt);

int start_timer(const char* func_name);
int stop_timer(const char* func_name);

void lock_mutex(const char* func_name);
void unlock_mutex(const char* func_name);

int rtt_adj(struct timespec rtt_sample);

//	void integrity_check(const char* caller_fcn);	//	Variables integrity check function (developing purposes)




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
	timetostr(timestring);
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
	timetostr(timestring);
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
	return socket == connsocket ? 0 : -1;
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

			make_datapkt(next_seqn, data, len, &pkt);

			//	Atomic core_mutex variables manipulation start--------------
			lock_mutex(__func__);

			log_write("DEBUG about to push");

			//	Push the packet in the buffer (and wait for base to change while buffer full)
			while(snd_buffer_push(&pkt) < 0)
				pthread_cond_wait(&base_change, &core_mutex);

			log_write("DEBUG done, pkt pushed");

			log_println();
			char* snd_buf_p_str = malloc(sizeof(char)*((WIN*10)+1));
			snd_buf_p(snd_buf_p_str);
			sprintf(log_msg, "gbn_send(): Pkt pushed in the sending buffer: %s", snd_buf_p_str);
			free(snd_buf_p_str);
			log_write(log_msg);

			//	Send the packet
			if((sent = send_pkt(connsocket, &pkt)) >= 0) {

				//	Start timer for first packet of the window
				if(base == next_seqn) {
					log_write(log_msg);
					if(start_timer(__func__) < 0)
						perror("set_timer()");
				}

				//	Update nextseqnum
				next_seqn++;

				return_value = sent;
			} else {
				sprintf(log_msg, "gbn_send(): Failed to send data!");
				log_write(log_msg);

				perror(log_msg);
				errno = EIO;
				return_value = -1;
			}

			unlock_mutex(__func__);
			//	Atomic core_mutex variables manipulation end----------------
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

	double random_norm;

	random_norm = (double)rand() / (double)RAND_MAX;

	//	Random number verification for packet loss simulation
	if(random_norm > LOSS_PROB) {
		sprintf(log_msg, "send_pkt(): Sending pkt n. %d.", pkt->seqn);
		log_write(log_msg);

		if(verify_datapkt(pkt) < 0) {
			log_write("send_pkt(): Corrupted packet, can't send.");
			return_value = -1;
		} else if(snd_buf_mark_snt(pkt->seqn) < 0) {
			sprintf(log_msg, "snd_pkt(): The packet %d is not in the snd_buf, can't send.", pkt->seqn);
			log_write(log_msg);
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
	} else {
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

	pthread_t retransmitter_thread_id;

	if(connsocket != -1)
		pthread_create(&retransmitter_thread_id, NULL, (void*)retransmission, NULL);
}

/*
 *	Handles the timeout event
 *
 */
void retransmission() {

	int buf_len;

	lock_mutex(__func__);

	log_println();
	log_write("timeout_handler(): Timeout reached, trying to re-send the window");

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

	unlock_mutex(__func__);

	//	Start timer again
	if(start_timer(__func__) < 0)
		perror("set_timer()");

	pthread_exit(NULL);
}

/*
 *	Handles incoming packets
 *
 */
void incoming_pkt_handler() {

	// Pointers for packet handling
	datapkt_t *datapkt;
	servicepkt_t *servicepkt;

	struct timespec measured_rtt;

	measured_rtt.tv_nsec = 0;
	measured_rtt.tv_sec = 0;

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
						lock_mutex(__func__);
						expectedseqn++;
						unlock_mutex(__func__);
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

						//	Move window (in isolation)----------------------
						lock_mutex(__func__);

						//	Verifying seqn
						if(servicepkt->seqn < base + WIN &&
							servicepkt->seqn >= base) {

							sprintf(log_msg, "incoming_pkt_handler(): Received a valid ack (packet %d). Moving window.", servicepkt->seqn);
							log_write(log_msg);


							base = servicepkt->seqn + 1;
							if(base == next_seqn) {
								if(stop_timer(__func__) < 0)
									perror("set_timer()");
								sprintf(log_msg, "incoming_pkt_handler(): Timer for packet %d stopped.", servicepkt->seqn);
								log_write(log_msg);
							} else {
								if(start_timer(__func__) < 0)
									perror("set_timer()");
								log_write("incoming_pkt_handler(): Timeout started.");
							}
							
							
							measured_rtt = snd_buf_ack(servicepkt->seqn);
							rtt_adj(measured_rtt);
							sprintf(log_msg, "incoming_pkt_handler(): measured rtt for acked pkt %lds.%ldns", measured_rtt.tv_sec, measured_rtt.tv_nsec);
							log_write(log_msg);
							

							char* snd_buf_p_str = malloc(sizeof(char)*((WIN*10)+1));
							snd_buf_p(snd_buf_p_str);
							sprintf(log_msg, "incoming_pkt_handler(): New base: %d. snd_buf is now %s", base, snd_buf_p_str);
							free(snd_buf_p_str);
							log_write(log_msg);

							//	Signal the base change
							pthread_cond_broadcast(&base_change);

						} else {
							sprintf(log_msg, "incoming_pkt_handler(): Received a duplicate ack (%d).", servicepkt->seqn);
							log_write(log_msg);
						}

						unlock_mutex(__func__);
						//	------------------------------------------------
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

	lock_mutex(__func__);

	//	While there are pkts in window, wait for base to change
	while(base < next_seqn) {
		log_write("wait_delivery(): Waiting for the last acks to arrive.");
		log_println();
		pthread_cond_wait(&base_change, &core_mutex);	// NB: this releases the mutex
	}

	log_write("wait_delivery(): All packets ACKed, transmission ended.");

	unlock_mutex(__func__);

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
		incoming_pkt_handler();
		log_write("listen_routine(): Listening again.");
	}

	sprintf(log_msg, "select(): %s", strerror(errno));
	log_write(log_msg);

	listen_routine(NULL);
}

/*
 *	A function to launch the listening thread
 *
 */
void launch_listening_thread() {

	//	Creating the listening thread
	pthread_create(&listening_thread_id, NULL, (void*)listen_routine, NULL);
}

/*
 *	A function to cancel the listening thread
 *
 */
void cancel_listening_thread() {

	//	Cancelling the listening thread
	pthread_cancel(listening_thread_id);
}

int gbn_core_init() {
	int return_value;

	struct sigaction on_sigint, on_sigalrm, old_action;

	on_sigint.sa_handler = kill_proc_routine;
	sigemptyset(&on_sigint.sa_mask);
	on_sigint.sa_flags = 0;

	on_sigalrm.sa_handler = timeout_handler;
	sigemptyset(&on_sigalrm.sa_mask);
	on_sigalrm.sa_flags = 0;


	if(gbn_init != 1) {

		mutex_owner = malloc(50);
		sprintf(mutex_owner, "-----");

		//	Binding signal handlers
		sigaction(SIGALRM, &on_sigalrm, &old_action);
		sigaction(SIGINT, &on_sigint, &old_action);

		//	Allocating memory for the incoming pkts ack
		response_ack = malloc(sizeof(servicepkt_t));

		//	Allocate the send buffer for unacked pkts (of size WIN)
		snd_buffer_init(WIN);

		lock_mutex(__func__);

		pkt_win = malloc(WIN*sizeof(datapkt_t));

		rtt_est.tv_nsec = 800000;
		rtt_est.tv_sec = 0;

		rtt_dev.tv_nsec = 80000;
		rtt_dev.tv_sec = 0;

		timer_timeout.tv_nsec = TIMEOUT_NS;
		timer_timeout.tv_sec = TIMEOUT_S;

		base = 1;
		next_seqn = 1;
		expectedseqn = 1;

		return_value = 0;
		gbn_init = 1;

		unlock_mutex(__func__);
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

		lock_mutex(__func__);

		base = -1;
		next_seqn = -1;
		expectedseqn = -1;

		gbn_init = 0;
		return_value = 0;

		unlock_mutex(__func__);

		cancel_listening_thread();

		free(pkt_win);
		free(log_msg);

		log_write("Closing log..");
		log_dump();

	} else
		return_value = -1;

	return return_value;
}

void kill_proc_routine() {

	printf("\nTermination occurred.\n");
	printf("\ntrylock(core_mutex): %s. owner: %s\n", strerror(pthread_mutex_trylock(&core_mutex)), mutex_owner);
	gbn_core_fin();
	printf("Killing procedure terminated.\n");
	exit(1);
}

int start_timer(const char* func_name) {
	struct itimerval timer_val;
	struct itimerval timer_val_old;

	timer_val.it_interval.tv_usec = timer_val.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once

	timer_val.it_value.tv_sec = timer_timeout.tv_sec;
	timer_val.it_value.tv_usec = timer_timeout.tv_nsec/1000;

	sprintf(log_msg, "start_timer(): timer started by %s() with a value of %lds%ldns", func_name, timer_val.it_value.tv_sec, timer_val.it_value.tv_usec*1000);
	log_write(log_msg);

	return setitimer(ITIMER_REAL, &timer_val, &timer_val_old);
}

int stop_timer(const char* func_name) {
	struct itimerval timer_val;
	struct itimerval timer_val_old;

	timer_val.it_interval.tv_usec = timer_val.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once

	timer_val.it_value.tv_sec = 0;
	timer_val.it_value.tv_usec = 0;

	sprintf(log_msg, "stop_timer(): timer stopped by %s().", func_name);
	log_write(log_msg);

	return setitimer(ITIMER_REAL, &timer_val, &timer_val_old);
}

void lock_mutex(const char* func_name) {

	char* prev_owner = malloc(50);

	sprintf(prev_owner, "%s-", mutex_owner);

	pthread_mutex_lock(&core_mutex);
	//sprintf(log_msg, "%s: -------------------------------\tMUTEX LOCKED", func_name);
	//log_write(log_msg);

	sprintf(mutex_owner, "%s%s", prev_owner, func_name);
}

void unlock_mutex(const char* func_name) {

	//integrity_check(__func__);
	mutex_owner[0] = '\0';

	pthread_mutex_unlock(&core_mutex);
	//sprintf(log_msg, "%s: -------------------------------\tMUTEX UNLOCKED", func_name);
	//log_write(log_msg);
}

/*
 *	I'd like to imagine the role of this function as a "sentinel", which verifies consistency
 *	in the GBN core variables.
 *
 *	When one of the conditions are violated, the function closes the application, and prints
 *	informations about system status.
 *
 *	NB Developing purpose function
 *
 *
void integrity_check(const char* caller_fcn) {

	int violations = 0;

	if(next_seqn < base) {
		sprintf(log_msg, "\n\nViolation occurred. next_seqn=%d < base=%d. Killing processes.", next_seqn, base);
		log_write(log_msg);
		fprintf(stderr, "%s", log_msg);
		violations++;
	} else if(next_seqn > base+WIN) {
		sprintf(log_msg, "\n\nViolation occurred. next_seqn=%d > base=%d. Killing processes.", next_seqn, base);
		log_write(log_msg);
		fprintf(stderr, "%s", log_msg);
		fprintf(stderr, "%s", log_msg);
		violations++;
	} else if(ts_max(rtt_est, timer_timeout) == 1) {
		sprintf(log_msg, "\n\nViolation occurred. rtt_est > timer_timeout. Killing processes.");
		log_write(log_msg);
		fprintf(stderr, "%s", log_msg);
		fprintf(stderr, "%s", log_msg);
		violations++;
	}

	if(violations < 0) {

		sprintf(log_msg, "\nintegrity_check() called by:%s()\n", caller_fcn);
		log_write(log_msg);

		fprintf(stderr, "%s", log_msg);

		fprintf(stderr, "\ncore_variables status:\n");
		fprintf(stderr, "%20s\t%d\n\n", "base", base);
		fprintf(stderr, "%20s\t%d\n\n", "base + WIN", base+WIN);
		fprintf(stderr, "%20s\t%d\n\n", "next_seqn", next_seqn);

		fprintf(stderr, "\ntimer status:\n");
		fprintf(stderr, "%20s\t%ld\".%ld\n\n", "timer_timeout", timer_timeout.tv_sec, timer_timeout.tv_nsec);
		fprintf(stderr, "%20s\t%ld\".%ld\n\n", "rtt_est + WIN", rtt_est.tv_sec, rtt_est.tv_nsec);
		fprintf(stderr, "%20s\t%ld\".%ld\n\n", "rtt_dev", rtt_dev.tv_sec, rtt_dev.tv_nsec);

		fprintf(stderr, "\nsnd_buffer status:\n");
		fprintf(stderr, "%20s\t%s\n\n", "snd_buf", snd_buf_p());

		log_dump();

		fprintf(stderr, "\nLog dumped, killing.\n");

		raise(SIGKILL);
	}
}
 *
 *
 */
int rtt_adj(struct timespec rtt_sample) {

	rtt_est = ts_sum(ts_times(rtt_est, (1-ALPHA)), ts_times(rtt_sample, ALPHA));

	rtt_dev = ts_sum(ts_times(rtt_dev, (1-BETA)), ts_times(ts_abs_diff(rtt_sample, rtt_est), BETA));

	timer_timeout = ts_sum(rtt_est, ts_times(rtt_dev, 4));


	sprintf(log_msg, "rtt_sample: %lds%ldns -----> new_timeout: %lds%ldns", rtt_sample.tv_sec, rtt_sample.tv_nsec,
			timer_timeout.tv_sec, timer_timeout.tv_nsec);
	log_write(log_msg);

	sprintf(log_msg, "[rtt_est: %lds%ldns - rtt_dev: %lds%ldns]", rtt_est.tv_sec, rtt_dev.tv_nsec,
			rtt_dev.tv_sec, rtt_dev.tv_nsec);

	return 0;
}
