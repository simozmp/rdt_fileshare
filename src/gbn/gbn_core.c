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
	//	Shared statuses
	closed = 0,
	listening,
	established,
	sending,		//	Assumed during gbn_write()

		//	Passive 4-way-handshake
	closewait,
	lastack,
		//	Active 4-way handshake
	finwait1,
	finwait2,
	timedwait,

	// Server statuses
	synrcvd,

	// Client statuses
	synsent
};




/*							GLOBAL VARIABLES								*/

//	Global socket, saved on connection
int connsocket = -1;	//	(should be valid when status != closed)

//	This indicates whether the system is initialized (1) or not (0)
int gbn_init = 0;

//	Connection status
int status = closed;

//	Connected peer address and length
struct sockaddr remote_addr;
socklen_t remote_addr_len;

//	Buffered service pkts for retransmissions
servicepkt_t valid_ack;			//	Last valid ack (for out of order pkts)
servicepkt_t valid_fin;			//	fin/finack
servicepkt_t valid_syn;			//	syn/synack

//	Listening thread id
pthread_t listening_thread_id = 0;

//	GBN core variables for sending
int base = -1;
int next_seqn = -1;
int expected_seqn = -1;

//	A buffer to store a copy all unacked pkts during retransmission phase
datapkt_t* pkt_win;

//	Time variables
struct timespec rtt_est;				//	Estimated round-trip time
struct timespec rtt_dev;				//	Round-trip time std deviance
struct timespec timer_timeout;			//	Timeout for retransmission timer
struct timespec timed_wait_timeout;		//	Timeout for timed wait timer
struct itimerval null_timer;			//	A (0s0ns) timeout to stop the timer

//	pthread_mutex to work in isolation on GBN core variables and pthread_conds
pthread_mutex_t core_mutex;

//	pthread_cond to signal condition change
pthread_cond_t conn_status_chng = PTHREAD_COND_INITIALIZER;	//	cond: (status changed)	(used during connection phase)
pthread_cond_t base_chng = PTHREAD_COND_INITIALIZER;		//	cond: (base changed)
pthread_cond_t conn_closed = PTHREAD_COND_INITIALIZER;		//	cond: (status == closed)

//	A string to temporarily store a message to write into the log file
char* log_msg;




/*						PRIVATE FCTNS PROTOTYPES							*/

int gbn_core_init();
int gbn_core_fin();
void incoming_pkt_handler();
int estimate_rtt(struct timespec rtt_sample);
int start_timer(const char* func_name);
int stop_timer(const char* func_name);
ssize_t send_pkt(int socketfd, datapkt_t *pkt);
ssize_t send_ack(int socketfd, servicepkt_t *pkt);
void launch_listening_thread();
void lock_mutex(const char* func_name);
void unlock_mutex(const char* func_name);
void timeout_handler();
int deliver_data(datapkt_t *pkt);			//	only called by the listening thread
int passive_close();						//	only called by the listening thread
void listen_routine(void* arg);				//	only called by the listening thread
void retransmission();						//	only called by the listening thread




/*							IMPLEMENTATIONS									*/

/*
 *	GBN implementation of the connect() function, connects socketfd socket
 *	to a server at servaddr (sockaddr of lenght addrlen)
 *
 */
int gbnc_connect(int socketfd, const struct sockaddr *servaddr,
		socklen_t addrlen) {

	char* servaddr_p = malloc(40*sizeof(char));
	char* timestring = malloc(30*sizeof(char));
	char* log_name = malloc(90*sizeof(char));
	log_msg = malloc(400*sizeof(char));

	//	Opening log file
	current_time_str(timestring);
	sprintf(log_name, "%s_gbn_core_log_client", timestring);
	if(log_open(log_name) < 0)
		perror("Error opening the log file!");

	// Connection data init
	connsocket = socketfd;
	remote_addr = servaddr[0];
	remote_addr_len = sizeof(remote_addr);

	gbn_core_init();

	//	Initialize the rcv_buffer
	if(rcv_buffer_init(BUFFERSIZE)<0)
		perror("rcv_buffer_init");

	launch_listening_thread();

	//	Saving the presentation for the server address
	if(inet_ntop(AF_INET, servaddr->sa_data, servaddr_p, 40*sizeof(char)) <= 0)
		perror("inet_ntop");

	sprintf(log_msg, "Connection attempt. Trying to reach server at %s",
			servaddr_p);
	log_write(log_msg);

	//	Making SYN pkt
	if(make_servicepkt(0, SYN, &valid_syn) < 0) {
		printf("gbnc_connect(): make_servicepkt error.");
	}

	//	Sending SYN pkt
	if(send_ack(socketfd, &valid_syn) < 0) {
		perror("send_ack()");
		return -1;
	} else
		start_timer(__func__);

	log_write("Response SYN sent, waiting for SYNACK.");	//	TODO: Implement a timeout/exponential backoff

	status = synsent;

	//	Waiting for synack
	lock_mutex(__func__);
	while(status == synsent)
		pthread_cond_wait(&conn_status_chng, &core_mutex);
	unlock_mutex(__func__);

	sprintf(log_msg, "Connection established. Remote address: %s", servaddr_p);
	log_write(log_msg);

	status = established;

	free(timestring);
	free(servaddr_p);
	free(log_name);

	return 0;
}

/*
 *	This implements the 3-way handshake server side
 *
 */
int gbnc_accept(int socketfd, struct sockaddr *addr, socklen_t addrlen) {

	int return_value = -1;

	errno = 0;

	if(status == closed) {
		char* remote_addr_p = malloc(40*sizeof(char));
		char* timestring = malloc(30*sizeof(char));
		char* log_name = malloc(90*sizeof(char));
		log_msg = malloc(400*sizeof(char));

		//	Opening log file
		current_time_str(timestring);
		sprintf(log_name, "%s_gbn_core_log_server", timestring);
		if(log_open(log_name) < 0)
			perror("Error opening the log file!");

		status = listening;

		log_write("Waiting for incoming connections.");
		log_println();

		// Waiting for a SYN packet (connection request)
		do {
			memset(&valid_syn, 0, sizeof(valid_syn));

			if(recvfrom(socketfd, (unsigned char*) &valid_syn,
					sizeof(servicepkt_t), 0, addr, &addrlen) < 0) {
				perror("recvfrom in gbn_accept");
			}
		} while(valid_syn.seqn != 0 && valid_syn.type != SYN);

		log_write("Connection incoming, allocating resources.");

		if(gbn_core_init() < 0)
			perror("gbn_core_init");

		launch_listening_thread();

		status = synrcvd;

		//	Connection data init
		connsocket = socketfd;
		remote_addr = addr[0];
		remote_addr_len = sizeof(remote_addr);
		connect(connsocket, &remote_addr, remote_addr_len);

		//	Initialize the rcv_buffer
		if(rcv_buffer_init(BUFFERSIZE)<0)
			perror("rcv_buffer_init");

		// Resetting syn buffer
		memset(&valid_syn, 0, sizeof(valid_syn));

		log_write("Accepting.");

		//	Sending SYNACK to accept
		if(make_servicepkt(0, SYNACK, &valid_syn) < 0) {
			printf("gbnc_connect(): make_servicepkt error.");
		}
		if(send_ack(connsocket, &valid_syn) < 0)
			perror("send_ack() in gbn_accept");
		else
			start_timer(__func__);

		//	Waiting for ACK
		lock_mutex(__func__);
		while(status != established) {
			log_write("Waiting for ACK to finalize connection..");
			pthread_cond_wait(&conn_status_chng, &core_mutex);
		}
		unlock_mutex(__func__);

		//	Saving the presentation for the server address
		if(inet_ntop(AF_INET, remote_addr.sa_data, remote_addr_p, 40*sizeof(char)) <= 0)
			perror("inet_ntop");

		sprintf(log_msg, "Connection established. Remote address: %s", remote_addr_p);
		log_write(log_msg);

		return_value = 1;

		free(remote_addr_p);
		free(timestring);
		free(log_name);
	} else {
		errno = EISCONN;}

	return return_value;
}

/*
 *	This functions checks if socket is the gbn connection socket
 *	Returns 0 if check passes, -1 otherwise
 *
 */
int gbn_verify_socket(int socket) {
	return socket == connsocket ? 0 : -1;
}

/*
 *	This function follows from the gbn FSM, it is used to send a new single
 *	data packet, transporting the content of the buffer pointed by data (len
 *	specifies its size). Before sending the packet, the latter is pushed into
 *	snd_buf.
 *
 *  Return value is the number of bytes pushed by the socket, or -1 in case
 *	of error.
 *
 */
ssize_t gbnc_send(void *data, size_t len) {
	ssize_t return_value = 0;
	ssize_t sent = 0;
	datapkt_t pkt;
	char* snd_buf_presentation;

	errno = 0;		// Zero out errno

	if(status != closed) {

		if(len <= PCKDATASIZE) {	// If data fits a packet, send

			//	Setting status to sending, when allowed
			if(status != closewait && status != lastack) {
				log_println();
				log_write("status: sending");
				status = sending;
			}

			//	Creating a new datapkt
			if(make_datapkt(next_seqn, data, len, &pkt) < 0) {
				log_write("gbn_send(): make_datapkt error.");
				return -1;
			}

			lock_mutex(__func__);

			//	Trying to push the packet in the buffer
			while(snd_buf_push(&pkt) < 0) {
				log_write("gbn_send(): Waiting for buffer to have some space.");

				//	Waiting for base to change
				if(pthread_cond_wait(&base_chng, &core_mutex) < 0) {
					sprintf(log_msg, "pthread_cond_wait() failed. (%s)", strerror(errno));
					log_write(log_msg);
				}
			}

			//	Printing snd_buf into log
			snd_buf_presentation = malloc(sizeof(char)*((WIN*10)+1));
			snd_buf_p(snd_buf_presentation);
			sprintf(log_msg, "gbn_send(): Pkt pushed in the sending buffer: %s", snd_buf_presentation);
			free(snd_buf_presentation);
			log_write(log_msg);

			//	Sending the packet (follows from gbn smd)
			if((sent = send_pkt(connsocket, &pkt)) >= 0) {

				//	Start timer for first packet of the window
				if(base == next_seqn)
					if(start_timer(__func__) < 0) {
						sprintf(log_msg, "start_timer() failed. (%s)", strerror(errno));
						log_write(log_msg);
					}

				//	Update nextseqnum
				next_seqn++;

				return_value = sent;
			} else {
				sprintf(log_msg, "gbn_send(): send_pkt failed!");
				log_write(log_msg);

				perror(log_msg);
				errno = EIO;
				return_value = -1;
			}

			unlock_mutex(__func__);

		} else {	//	This souldn't occur, as this function (gbn_send()) shall be only
					//	invoked by gbn_write() in gbn.c, which checks the packet's size
			log_write("gbn_send(): Message too long to fit a packet.");
			return_value = -1;
			errno = EMSGSIZE;
		}

	} else {
		log_write("gbn_send(): Connection closed.");
		return_value = -1;
		errno = ENOTCONN;
	}

	return return_value;
}

/*
 *	This functions sends a single datapkt, and transparently discards it
 *	with probability defined in the LOSS_PROB macro.
 *
 *	Return value is the number of bytes that could have been sent, or
 *	-1 in case of error.
 *
 */
ssize_t send_pkt(int socketfd, datapkt_t *pkt) {

	int return_value;

	double random_norm;

	random_norm = (double)rand() / (double)RAND_MAX;

	if(verify_datapkt(pkt) < 0) {
		log_write("send_pkt(): Corrupted packet, can't send.");

		return_value = -1;
	} else if(snd_buf_mark_snt(pkt->seqn) < 0) {
		sprintf(log_msg, "snd_pkt(): The packet %d is not in the snd_buf, can't send.", pkt->seqn);
		log_write(log_msg);

		return_value = -1;
	} else if(random_norm > LOSS_PROB) {	//	Random number verification for packet loss simulation
		sprintf(log_msg, "send_pkt(): Sending pkt n. %d.", pkt->seqn);
		log_write(log_msg);

		return_value = sendto(socketfd, pkt, sizeof(datapkt_t), 0, &remote_addr,
				remote_addr_len);
	} else {
		sprintf(log_msg, "send_pkt(): Sending pkt n. %d. Loss simulation ON.", pkt->seqn);
		log_write(log_msg);

		return_value = sizeof(datapkt_t);
	}

	return return_value;
}

/*
 *	This functions sends a single servicepkt, and transparently discards it
 *	with probability defined in the LOSS_PROB macro.
 *
 *	Return value is the number of bytes that could have been sent, or
 *	-1 in case of error.
 *
 */
ssize_t send_ack(int socketfd, servicepkt_t *pkt) {

	int return_value;

	double random_norm;

	random_norm = (double)rand() / (double)RAND_MAX;

	if(random_norm > LOSS_PROB) {

		sprintf(log_msg, "send_ack(): Sending ack n. %d (type %d).", pkt->seqn, pkt->type);
		log_write(log_msg);

		return_value = sendto(socketfd, pkt, sizeof(servicepkt_t), 0, &remote_addr,
			sizeof(struct sockaddr));
	} else {
		sprintf(log_msg, "send_ack(): Sending ack n. %d (type %d). Loss simulation ON.", pkt->seqn, pkt->type);
		log_write(log_msg);

		return_value = sizeof(servicepkt_t);
	}

	return return_value;
}

/*
 *	This function is used from the listening thread to deliver data to the
 *	rcv_buf, which shall be read from the application by a gbn_read().
 * 
 *	Return value is the amount of data written into the rcv_buf, or -1
 *	in case of error.
 * 
 */
int deliver_data(datapkt_t *pkt) {
	ssize_t return_value = 0;

	errno = 0;		// Zero out errno

	if((return_value = rcv_buffer_write(&pkt->payload, pkt->len)) < 0) {
		sprintf(log_msg, "deliver_data(): Error delivering packet n.%d. rcv_buffer_write failed (%s)", pkt->seqn, strerror(errno));
		log_write(log_msg);
		perror("deliver_data");
	} else {
		sprintf(log_msg, "deliver_data(): Packet n.%d delivered.", pkt->seqn);
		log_write(log_msg);
	}

	return return_value;
}

/*
 *	Signal handler for SIGALRM, this function launches a new retransmission thread,
 *	or closes the connection if in timed wait state.
 *
 */
void timeout_handler() {

	pthread_t temp_thread_id;
	int ret = -1;

	errno = 0;

	//	Cannot call this function in a signal handler.
	//log_write("timeout_handler(): Timeout triggered SIGALRM");

	if(connsocket != -1) {
		if(status == timedwait) {
			while(ret != 0)
				if((ret = pthread_create(&temp_thread_id, NULL, (void*)gbn_core_fin, NULL)) != 0)
					printf("pthread_create for timedwait failed %s", strerror(errno));
		} else {
			while(ret != 0)
				if((ret = pthread_create(&temp_thread_id, NULL, (void*)retransmission, NULL)) != 0)
				printf("pthread_create for retransmission failed. %s", strerror(errno));
		}
	}
}

/*
 *	Retransmission routine.
 *
 */
void retransmission() {

	int buf_len;

	switch(status) {
	case established:
	case closewait:
	case sending:

			lock_mutex(__func__);

			log_println();
			log_write("retransmission(): Trying to re-send the window (and last valid ack)");

			if(valid_ack.type == ACK)
				if(send_ack(connsocket, &valid_ack) < 0)
					printf("retransmission(): send_ack failed.\n");

			//	Backup window pkts (not doing so will cause some pkts retransmission as valid acks arrives)
			for(buf_len=0; buf_len<=WIN; buf_len++)
				if(snd_buf_get(pkt_win+buf_len, buf_len))
					break;

			// Send again all packets in the window (buf_len is useful for sending only valid pkts)
			for(int i=0; i<buf_len; i++) {
				sprintf(log_msg, "retransmission(): About to send pkt n.%d.", pkt_win[i].seqn);
				log_write(log_msg);
				send_pkt(connsocket, pkt_win+i);
			}

			unlock_mutex(__func__);

			//	Start timer again
			start_timer(__func__);

		break;
	case closed:
	case synsent:
	case synrcvd:
		log_write("Timeout reached for SYN message, re-sending.");
		if(send_ack(connsocket, &valid_syn) < 0)
			printf("retransmission(): send_ack for syn retransmission failed.\n");
		start_timer(__func__);
		break;
	case finwait1:
	case lastack:
		log_write("Timeout reached for FIN message, resending.");
		if(send_ack(connsocket, &valid_fin) < 0)
			printf("retransmission(): send_ack for fin retransmission failed.\n");
		start_timer(__func__);
		break;
	}

	//	Necessary to free the thread memory before exiting
	pthread_detach(pthread_self());

	pthread_exit(NULL);
}

/*
 *	Handles incoming packets.
 *
 */
void incoming_pkt_handler() {

	unsigned char *buffer_pkt;		//	Pointer to the generic buffer
	int n;							//	Bytes read

	datapkt_t *datapkt;				//	datapkt pointer, for handling the generic buffer as a datapkt
	servicepkt_t *servicepkt;		//	servicepkt pointer, for handling the generic buffer as a servicepkt

	struct timespec measured_rtt;	//	measured rtt fot the datapkt
		measured_rtt.tv_nsec = 0;
		measured_rtt.tv_sec = 0;

	char* snd_buf_presentation;

	//	Allocating space for the generic packet buffer (size of the largest one)
	buffer_pkt = malloc(sizeof(datapkt_t));

	memset(buffer_pkt, 0, sizeof(datapkt_t));	//	Zeroing out &buffer_pkt

	//	Printing a blank line into the log
	log_println();

	//	Reading packet
	if((n = read(connsocket, buffer_pkt, sizeof(datapkt_t))) >= 0) {

		//	Checking packet category
		switch(n) {

			//	Data packet handling
			case sizeof(datapkt_t):

				datapkt = (datapkt_t*) buffer_pkt;

				//	Condition to analyze datapkt
				if(status == established ||
					status == finwait1 ||
					status == finwait2 ||
					status == sending) {		//	NB: During the sending status, the peer must
												//	still be able to detect duplicate pkts!

					// Verify _checksum for datapkt
					if(verify_datapkt(datapkt) == 0) {

						if(status != finwait2) {

							//	Handling packet
							if(datapkt->seqn == expected_seqn && status != sending) {

								if(expected_seqn == 1)		//	To stop the last ack timer for the connection handshake
									stop_timer(__func__);

								sprintf(log_msg, "incoming_pkt_handler(): datapkt incoming. seqn:%d. OK. Delivering.",
										datapkt->seqn);
								log_write(log_msg);

								// Deliver data to upper layer
								if(deliver_data(datapkt) < 0)
									log_write("incoming_pkt_handler(): Error delivering data to receiving buffer");

								// Update valid_ack
								if(make_servicepkt(datapkt->seqn, ACK, &valid_ack) < 0)
									printf("inc_pkt_hand(): make servicepkt failed.\n");

								// Update expectedseqn (in isolation)
								lock_mutex(__func__);
								expected_seqn++;
								unlock_mutex(__func__);
							} else {
								if(status == sending) {
									sprintf(log_msg, "incoming_pkt_handler(): datapkt incoming. seqn:%d. I'm still waiting for acks tho, ignoring datapkt and sending duplicate ack.",
											datapkt->seqn);
									log_write(log_msg);
								} else {
									sprintf(log_msg, "incoming_pkt_handler(): datapkt incoming. seqn:%d. Not delivering as it is not the packet I expect (%d).",
											datapkt->seqn, expected_seqn);
									log_write(log_msg);
								}
							}

							// Send valid_ack
							if(send_ack(connsocket, &valid_ack) != sizeof(servicepkt_t))
								log_write("incoming_pkt_handler(): Error sending ACK.");

						} else {
							log_write("Datapkt incoming. I'm closing tho, so I'm not processing it, just acking.");

							// Update valid_ack
							if(make_servicepkt(datapkt->seqn, ACK, &valid_ack) < 0)
								log_write("incoming_pkt_handler(): Error making ACK.");

							// Send valid_ack
							if(send_ack(connsocket, &valid_ack) != sizeof(servicepkt_t))
								log_write("incoming_pkt_handler(): Error sending ACK.");
						}
					} else {
						log_write("incoming_pkt_handler(): Received a corrupted datapkt. Ignoring.");
					}

				} else if(status == synrcvd) {
					log_write("Client already sending, but I'm still waiting for the 3wh ACK. Sending SYNACK again.");
					send_ack(connsocket, &valid_syn);
				} else {
					log_write("Ignoring datapkt (connection not established).");
				}

				break;

			// Service packet handling
			case sizeof(servicepkt_t):

				servicepkt = (servicepkt_t*) buffer_pkt;

				// Verifying checksum for servicepkt
				if(verify_servicepkt(servicepkt) == 0) {

					switch(servicepkt->type) {
					case FIN:
						if(valid_fin.type == 0 || status == finwait2 || status == finwait1) {
							log_write("incoming_pkt_handler(): passive_close");
							passive_close(servicepkt->seqn);
						} else {
							log_write("Duplicate FIN received. Sending FINACK again.");
							send_ack(connsocket, &valid_fin);
						}
						break;
					case FINACK:
						log_write("incoming_pkt_handler(): passive_close");
						passive_close(servicepkt->seqn);
						break;
					case ACK:

						if(status == synrcvd) {
							lock_mutex(__func__);
							status = established;
							stop_timer(__func__);
							pthread_cond_broadcast(&conn_status_chng);
							unlock_mutex(__func__);
						} else if(servicepkt->seqn < base + WIN &&
							servicepkt->seqn >= base) {		//	Verifying seqn

							//	Move window (in isolation)----------------------
							lock_mutex(__func__);

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

							if(RTT_EST)
								estimate_rtt(measured_rtt);

							sprintf(log_msg, "incoming_pkt_handler(): measured rtt for acked pkt %lds.%ldns", measured_rtt.tv_sec, measured_rtt.tv_nsec);
							log_write(log_msg);
							

							snd_buf_presentation = malloc(sizeof(char)*((WIN*10)+1));
							snd_buf_p(snd_buf_presentation);
							sprintf(log_msg, "incoming_pkt_handler(): New base: %d. snd_buf is now %s", base, snd_buf_presentation);
							free(snd_buf_presentation);
							log_write(log_msg);

							//	Signal the base change
							if(pthread_cond_broadcast(&base_chng) != 0)
								printf("Error signaling window base change!\n");

							unlock_mutex(__func__);
							//	------------------------------------------------

						} else {
							sprintf(log_msg, "incoming_pkt_handler(): Received a duplicate ack (%d).", servicepkt->seqn);
							log_write(log_msg);
						}

						break;
					case SYNACK:
						lock_mutex(__func__);

						stop_timer(__func__);

						log_write("SYNACK received. ACK-ing it.");

						// Sending ACK
						if(make_servicepkt(0, ACK, &valid_syn) < 0) {
							printf("gbnc_connect(): make_servicepkt error.");
						}

						// Sending ACK
						if(make_servicepkt(0, ACK, &valid_ack) < 0) {
							printf("gbnc_connect(): make_servicepkt error.");
						}

						if(send_ack(connsocket, &valid_syn) < 0) {
							perror("send_ack in gbn_connect");
						}

						status = established;

						start_timer(__func__);

						pthread_cond_broadcast(&conn_status_chng);

						unlock_mutex(__func__);
						break;
					default:
						log_write("Received an un-handleable service_pkt");
						break;
					}

				}

				break;

			// Not recognized packet, discarded (?)
			default:
				log_write("incoming_pkt_handler(): Unrecognized pkt received. (?)");
				break;
			}

	} else {
		log_write("incoming_pkt_handler(): Read failed.");
	}

	free(buffer_pkt);
}

/*
 *	This function is called from incoming_pkt_handler(), when detecting
 *	a FIN or a FINACK. Here are defined all behaviours to switch between
 *	closing statuses.
 *
 *	Return value is the connection status.
 *
 */
int passive_close(int seqn) {

	log_write("passive_close()");

	switch(status) {

		//	Passive close steps
		case established:

			lock_mutex(__func__);

			if(valid_fin.type == 0) {
				log_write("New FIN received. FINACKing and FINning.");

				make_servicepkt(seqn, FINACK, &valid_fin);
				send_ack(connsocket, &valid_fin);

				make_servicepkt(seqn+1, FIN, &valid_fin);
			} else {
				log_write("Finished to write. FINning");
			}

			send_ack(connsocket, &valid_fin);
			next_seqn++;
			log_write("status: lastack");
			status = lastack;

			start_timer(__func__);

			unlock_mutex(__func__);

			break;

		case sending:

			lock_mutex(__func__);

			log_write("FIN received, FINACKing. Will send FIN at the end of gbn_write().");
			make_servicepkt(seqn, FINACK, &valid_fin);
			send_ack(connsocket, &valid_fin);

			make_servicepkt(seqn+1, FIN, &valid_fin);

			log_write("status: closewait");
			status = closewait;

			unlock_mutex(__func__);

			break;

		case closewait:

			lock_mutex(__func__);

			log_write("Finished send! FINning and waiting for last ack.");
			send_ack(connsocket, &valid_fin);
			next_seqn++;
			log_write("status: lastack");
			start_timer(__func__);
			status = lastack;

			unlock_mutex(__func__);

			break;

		case lastack:
			stop_timer(__func__);
			gbn_core_fin();
			break;


		//	Active close steps
		case finwait1:

			lock_mutex(__func__);

			stop_timer(__func__);
			status = finwait2;
			log_write("status: finwait2");

			unlock_mutex(__func__);

			break;

		case finwait2:

			lock_mutex(__func__);

			stop_timer(__func__);
			make_servicepkt(seqn, FINACK, &valid_fin);
			send_ack(connsocket, &valid_fin);
			log_write("status: timedwait");
			status = timedwait;
			start_timer(__func__);

			unlock_mutex(__func__);

			break;
		default:
			break;
	}

	return status;
}

/*
 *	This function starts a 4 way handshake to close the connecion, and then
 *	waits the status to be closed.
 *
 *	Return value is 0 in case of success, -1 otherwise.
 * 
 */
int gbnc_shutdown(int socket) {

	int return_value = -1;

	if(socket == connsocket) {

		lock_mutex(__func__);

		if(status == established || status == sending
				|| valid_fin.seqn != 0) {

			//	Sending a new FIN servicepkt
			make_servicepkt(next_seqn, FIN, &valid_fin);
			send_ack(connsocket, &valid_fin);

			status = finwait1;
			log_write("status: finwait1");

			start_timer(__func__);

			if(status != sending) {
				while(status != closed)
					pthread_cond_wait(&conn_closed, &core_mutex);
			}

			return_value = 0;
		} else if(status == closed) {
			log_write("gbn_shutdown(): Called while in closed status.");
			return_value = -1;
		} else {
			log_write("gbn_shutdown(): Called while already closing.");
			return_value = -1;
		}

		unlock_mutex(__func__);
	}

	return return_value;
}

/*
 *	This function closes the UDP socket, if the connection is closed (if not,
 *	that would mean that a fork of the caller has an established connection to
 *	the peer, so the UDP socket still can't be closed.
 *
 *	Return value is 0 in case of success, or -1 in case of error.
 * 
 */
int gbnc_close(int socket) {
	int return_value = -1;

	errno = 0;

	if(socket == connsocket) {
		//	See fcn header
		if(status == closed)
			return_value = close(socket);
		else
			return_value = 0;

		gbn_core_fin();
	} else
		errno = ENOTCONN;

	return return_value;
}

/*
 *	This functions waits for all the pkts in the window to be acked, and then
 *	exits the sending status. This is called by gbn_send() to guarantee reliability.
 *
 */
void wait_delivery() {

	lock_mutex(__func__);

	//	While there are pkts in window, wait for base to change
	while(base < next_seqn) {
		log_write("wait_delivery(): Waiting for the last acks to arrive.");
		log_println();
		if(pthread_cond_wait(&base_chng, &core_mutex) < 0) {	// NB: this releases the mutex
			log_write("wait_delivery(): pthread_cond_wait() failed.");
		}
	}

	log_write("wait_delivery(): All packets ACKed, transmission ended.");

	unlock_mutex(__func__);

	switch(status) {
	case sending:
		log_write("status: established");
		status = established;
		break;

	case closewait:
		printf("Finished sending, calling passive_close...\n");
		passive_close(0);
		lock_mutex(__func__);
		while(status != closed)
			pthread_cond_wait(&conn_closed, &core_mutex);
		unlock_mutex(__func__);
		break;
	}

}

/*
 *	This is the function to be run in the listening thread, to catch incoming data
 *	via the select() syscall, and then call incoming_pkt_handler() to read them and respond.
 *
 */
void listen_routine(void* arg) {

	char log_msg_lr[80];
	fd_set readset;

	//	Predisposing the readset for the select()
	FD_ZERO(&readset);
	FD_SET(connsocket, &readset);

	log_write("listen_routine(): Listening for incoming packets.");

	while(1) {

		while(select(connsocket + 1, &readset, NULL, NULL, NULL) == 1) {
			incoming_pkt_handler();
			log_write("listen_routine(): Listening again.");
		}

		sprintf(log_msg_lr, "listen_routine(): select() anomally stopped with errno=%d -> %s", errno, strerror(errno));
		log_write("Starting select() again.");
	}
}

/*
 *	A function to launch the listening thread
 *
 */
void launch_listening_thread() {
	int returned;

	//	Creating the listening thread
	if((returned = pthread_create(&listening_thread_id, NULL, (void*)listen_routine, NULL)) != 0)
		printf("ERROR LAUNCHING LISTENING THREAD! (%s)\n", strerror(returned));
}

/*
 *	A function to cancel the listening thread
 *
 */
void cancel_listening_thread() {

	//	Canceling the listening thread
	pthread_cancel(listening_thread_id);
}

/*
 *	This routine initializes the peer's connection by allocating buffers,
 *	and configuring initial states of global variables.
 *
 *	Return value is -1 in case of error, 0 otherwise
 *
 */
int gbn_core_init() {
	int return_value;

	struct sigaction on_sigalrm, old_action;

	on_sigalrm.sa_handler = timeout_handler;
	sigemptyset(&on_sigalrm.sa_mask);
	on_sigalrm.sa_flags = 0;


	if(gbn_init != 1) {

		//	Setting the null itimerval, used to stop the timer
		null_timer.it_interval.tv_usec = null_timer.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once
		null_timer.it_value.tv_sec = 0;
		null_timer.it_value.tv_usec = 0;

		//	Binding signal handlers
		sigaction(SIGALRM, &on_sigalrm, &old_action);

		//	Allocate the send buffer for unacked pkts (of size WIN)
		snd_buf_init(WIN);

		//	Cleaning the valid_ack and valid_fin buffers
		make_servicepkt(0, 0, &valid_ack);
		make_servicepkt(0, 0, &valid_fin);

		lock_mutex(__func__);

		//	Allocating space for pkt_win, a buffer to temporarly save the
		//	packets in the window for a retransmission
		pkt_win = malloc(WIN*sizeof(datapkt_t));

		//	Initializing timers
		rtt_est.tv_nsec = 800000;
		rtt_est.tv_sec = 0;
		rtt_dev.tv_nsec = 80000;
		rtt_dev.tv_sec = 0;
		timer_timeout.tv_nsec = TIMEOUT_NS;
		timer_timeout.tv_sec = TIMEOUT_S;
		timed_wait_timeout.tv_nsec = 0;
		timed_wait_timeout.tv_sec = 2;

		//	Initializing core variables
		base = 1;
		next_seqn = 1;
		expected_seqn = 1;

		//	Save 0 for return value, and 1 for the init flag
		return_value = 0;
		gbn_init = 1;

		unlock_mutex(__func__);
	} else
		return_value = -1;

	return return_value;
}

/*
 *	This routine initializes the peer's connection.
 *
 *	Return value is -1 in case of error, 0 otherwise
 *
 */
int gbn_core_fin() {

	int return_value;

	if(gbn_init == 1) {

		log_println();
		log_write("Finalizing connection.");

		//	Destroying snd_buf
		snd_buf_destroy(WIN);

		rcv_buffer_destroy();

		lock_mutex(__func__);

		base = -1;
		next_seqn = -1;
		expected_seqn = -1;

		gbn_init = 0;
		return_value = 0;

		if(listening_thread_id != 0)
			cancel_listening_thread();

		free(pkt_win);
		free(log_msg);

		log_msg = NULL;

		log_write("Closing log..");
		log_dump();

		status = closed;

		pthread_cond_broadcast(&conn_closed);

		unlock_mutex(__func__);

	} else
		return_value = -1;

	return return_value;
}

/*
 *	Function to start a timer
 *
 */
int start_timer(const char* func_name) {

	int return_value;

	struct itimerval timer_val;
	struct itimerval timer_val_old;

	if(status == timedwait) {
		//	Parse timed_wait timeout

		timer_val.it_interval.tv_usec = timer_val.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once

		timer_val.it_value.tv_sec = timed_wait_timeout.tv_sec;
		timer_val.it_value.tv_usec = timed_wait_timeout.tv_nsec/1000;
	} else {
		//	Parse timer timeout

		timer_val.it_interval.tv_usec = timer_val.it_interval.tv_sec = 0;	//	So that the timer will trigger SIGALRM only once

		timer_val.it_value.tv_sec = timer_timeout.tv_sec;
		timer_val.it_value.tv_usec = timer_timeout.tv_nsec/1000;
	}

	return_value = setitimer(ITIMER_REAL, &timer_val, &timer_val_old);

	if(return_value != 0) {
		sprintf(log_msg, "start_timer(): attempt to start timer failed with error (%s)", strerror(errno));
		log_write(log_msg);
	} else {
		sprintf(log_msg, "start_timer(): timer started by %s() with a value of %lds%ldns", func_name, timer_val.it_value.tv_sec, timer_val.it_value.tv_usec*1000);
		log_write(log_msg);
	}

	return return_value;
}

/*
 *	Function to stop a timer
 *
 */
int stop_timer(const char* func_name) {
	struct itimerval timer_val_old;

	int return_value = setitimer(ITIMER_REAL, &null_timer, &timer_val_old);

	if(return_value != 0) {
		sprintf(log_msg, "start_timer(): attempt to stop timer failed with error (%s)", strerror(errno));
		log_write(log_msg);
	} else {
		sprintf(log_msg, "stop_timer(): timer stopped by %s().", func_name);
		log_write(log_msg);
	}

	return return_value;
}

/*
 *	Function to lock the core mutex
 *
 */
void lock_mutex(const char* func_name) {

	int returned;

	if((returned = pthread_mutex_lock(&core_mutex)) != 0) {
		sprintf(log_msg, "%s():\t\t\t pthread_mutex_lock() failed with error (%s)", func_name, strerror(returned));
		log_write(log_msg);
	}
}

/*
 *	Function to unlock the core mutex
 *
 */
void unlock_mutex(const char* func_name) {

	int returned;

	if((returned = pthread_mutex_unlock(&core_mutex)) != 0) {
			sprintf(log_msg, "%s():\t\t\t pthread_mutex_unlock() failed with error (%s)", func_name, strerror(returned));
			log_write(log_msg);
	}
}

/*
 *	Function to compute a new round trip time estimation.
 *
 *	Returns 0 in case of success, -1 otherwise.
 *
 */
int estimate_rtt(struct timespec rtt_sample) {

	rtt_est = ts_sum(ts_times(rtt_est, (1-ALPHA)), ts_times(rtt_sample, ALPHA));

	rtt_dev = ts_sum(ts_times(rtt_dev, (1-BETA)), ts_times(ts_abs_diff(rtt_sample, rtt_est), BETA));

	timer_timeout = ts_sum(rtt_est, ts_times(rtt_dev, 4));

	sprintf(log_msg, "rtt_sample: %lds%ldns -----> new_timeout: %lds%ldns", rtt_sample.tv_sec, rtt_sample.tv_nsec,
			timer_timeout.tv_sec, timer_timeout.tv_nsec);
	log_write(log_msg);

	return 0;
}
