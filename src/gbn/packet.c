#include "gbn/packet.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

/*						PRIVATE FCTNS PROTOTYPES							*/

uint16_t ones_complement_sum(uint16_t a, uint16_t b);
uint16_t data_checksum(datapkt_t *packet);
uint16_t service_checksum(servicepkt_t *packet);

/*
 *	Initializes the datapkt pointed by packet with given attributes
 *
 */
int make_datapkt(int seq, void *message, size_t message_len,
		datapkt_t* packet) {

	int return_value = 0;

	if(packet == NULL) {
		return_value = -1;
	} else {
		// Put zeros in packet's memory
		memset(packet, 0, sizeof(datapkt_t));

		packet->type = DATA;
		packet->seqn = seq;
		packet->len = message_len;
		memcpy(packet->payload, message, message_len);
		packet->checksum = data_checksum(packet);
	}

	return return_value;
}

/*
 *	Initializes the servicepkt pointed by packet with given attributes
 *	(if type not valid, does nothing)
 *
 */
int make_servicepkt(int seq, const int type, servicepkt_t* packet) {

	int return_value = 0;

	if(packet == NULL) {
		return_value = -1;
	} else {
		switch(type) {
			case ACK:
			case SYN:
			case SYNACK:
			case FIN:
			case FINACK:
				// Put zeros in packet's memory
				memset(packet, 0, sizeof(servicepkt_t));

				packet->type = type;
				packet->seqn = seq;
				packet->checksum = service_checksum(packet);
				break;
		}
	}

	return return_value;
}

/*
 *	Returns 1 if the checksum field is valid for the packet data
 *
 */
int verify_datapkt(datapkt_t *pkt) {

	int return_value = (data_checksum(pkt) == pkt->checksum) ? 0 : -1;

	return return_value;
}

/*
 *	Returns 1 if the checksum field is valid for the packet data
 *
 */
int verify_servicepkt(servicepkt_t *pkt) {

	return (service_checksum(pkt) == pkt->checksum) ? 0 : -1;
}

void print_servicepkt(servicepkt_t *pkt) {
	printf("servicepkt:");
	printf("\ttype: %d\n", pkt->type);
	printf("\tseqn: %d\n", pkt->seqn);
	printf("\tchecksum: %u\n", (unsigned int) pkt->checksum);
}

/*
 *	Implementation of the ones complement sum
 *
 */
uint16_t ones_complement_sum(uint16_t a, uint16_t b) {
	uint32_t sum = (uint32_t) a + (uint32_t) b;

	if(sum >= pow(2,16))
		sum++;

	return (uint16_t) sum;
}

/*
 *	Computes and return checksum for the datapkt pointed by packet
 *
 *	The function does not read the checksum field of the packet, and
 *	treat the field as zeroed
 *
 */
uint16_t data_checksum(datapkt_t *packet) {

	// Backup checksum value for the packet and set the field to zero
	uint16_t pkt_old_checksum = packet->checksum;
	packet->checksum = 0;

	uint16_t* pktstart = (uint16_t*) packet;
	uint16_t sum = 0;	// Two bytes to keep the partial sum

	// For each uint16 of the packet stack computes the ocs
	for(int i=0; i<sizeof(datapkt_t)/2; i++)
		sum = ones_complement_sum(sum, pktstart[i]);

	// Restoring the old checksum in the packet
	packet->checksum = pkt_old_checksum;

	return ~sum;
}

/*
 *	Computes and return checksum for the servicepkt pointed by packet
 *
 *	The function does not read the checksum field of the packet, and
 *	treat the field as zeroed
 *
 */
uint16_t service_checksum(servicepkt_t *packet) {

	// Backup checksum value for the packet and set the field to zero
	uint16_t pkt_old_checksum = packet->checksum;
	packet->checksum = 0;

	uint16_t* pktstart = (uint16_t*) packet;
	uint16_t sum = 0;	// Two bytes to keep the partial sum

	// For each uint16 of the packet stack computes the ocs
	for(int i=0; i<sizeof(servicepkt_t)/2; i++)
		sum = ones_complement_sum(sum, pktstart[i]);

	// Restoring the old checksum in the packet
	packet->checksum = pkt_old_checksum;

	return ~sum;
}
