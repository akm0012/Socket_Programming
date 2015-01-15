/*	
*	Comp4320: Lab 3
*
*	File: Server.c	
*	Author: Andrew K. Marshall (akm0012)
*	Group ID: 15
*	Date: 12/2/14
*	Version: 1.0
*	Version Notes: Final working version. 
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define GROUP_PORT "10025"	// Port should be 10010 + Group ID
#define GROUP_ID 15

#define MAX_PACKET_LEN 1029	// 1Kb for message, and 5 bytes for header
#define V_LENGTH 85	// Operation: Count vowels
#define DISEMVOWEL 170	// Operation: Remove vowels

#define DEBUG 0	// Used for debugging: 1 = ON; 0 = OFF

#define TRUE 1
#define FALSE 0

// Prototypes

// Struct that will be received from the client
struct received_packet
{
	unsigned short magic_num;	// Magic Number (2 bytes)
	unsigned char GID_client;	// The Client's Group ID (1 byte)
	unsigned short port_num;	// Port number (2 bytes)
} __attribute__((__packed__));
typedef struct received_packet rx_packet;

// Struct that will be sent to the client, if no other clients are avaliable
struct transmitted_packet_wait
{
	unsigned short magic_num;	// Magic Number (2 bytes)
	unsigned char GID_server;	// The Server's Group ID (1 byte)
	unsigned short port_num;	// Port number (2 bytes)
} __attribute__((__packed__));
typedef struct transmitted_packet_wait tx_wait;

// Struct that will be sent to the client, if a client is avaliable
struct transmitted_packet_pair
{
	unsigned short magic_num;	// Magic Number (2 bytes)
	unsigned char GID_server;	// The Server's Group ID (1 byte)
	unsigned int IP_addr;	// IP Address of the waiting client (4 bytes)
	unsigned short port_num;	// Port number from the client (2 bytes)
} __attribute__((__packed__));
typedef struct transmitted_packet_pair tx_pair;

// Struct that will be sent to the client, if an error occured
struct transmitted_packet_error
{
	unsigned short magic_num;	// Magic Number (2 bytes)
	unsigned char GID_server;	// The Server's Group ID (1 byte)
	unsigned short error_code;	// The error code (2 bytes)
} __attribute__((__packed__));

typedef struct transmitted_packet_error tx_error;

// Used to determine if we are using IPv4 or IPv6
void *get_in_addr(struct sockaddr *sa) 
{
	if (sa->sa_family == AF_INET) 
	{
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	//else
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int main(int argc, char *argv[])
{
	// Variables 
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int status;
	int numbytes;
	char *my_port;

	// Saved Client info
	unsigned int saved_IP;
	unsigned short saved_port;
	int client_waiting;
	
	//struct sockaddr_storage their_addr;
	struct sockaddr_in their_addr;
	socklen_t addr_len;		

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;	// set to AF_INIT to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;	// use my IP

	// Check to make sure the command line arguments are valid
	if (argc != 2) 
	{
		fprintf(stderr, "Usage Error: Should be 1 argument: the port number.\n");
		exit(1);
	}

	// Get the port number from the command line
	my_port = argv[1];

	// Init client_waiting
	client_waiting = FALSE;
	
	if (DEBUG) {
		printf("DEBUG: Port number: %s\n", my_port);
	}
	
	printf("Starting Server... to stop, press 'Ctrl + c'\n");
	
	while(1)
	{		
		// 1. getaddrinfo
		if ((status = getaddrinfo(NULL, my_port, 
			&hints, 	// points to a struct with info we have already filled in
			&servinfo)) != 0)	// servinfo: A linked list of results
		{
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
			return 1;
		}

		// Loop through all the results and bind to the first we can
		for (p = servinfo; p != NULL; p = p->ai_next)
		{
			// 2. socket
			if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
			{	
				perror("Socket Error!");
				continue;
			}
		
			// 3. bind
			if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
			{
				close(sockfd);
				perror("Bind Error!");
				continue;
			}

			break;
		}
		
		if (p == NULL)
		{
			fprintf(stderr, "Failed to bind socket\n");
			return 2;
		}	
		

		freeaddrinfo(servinfo); 	// Call this when we are done with the struct "servinfo"

		if (DEBUG) {
			printf("Binding complete, waiting to recvfrom...\n");
		}

		addr_len = sizeof their_addr;

		rx_packet packet_in;

		// 4. recvfrom
		// MAX_PACKET_LEN -1: To make room for '\0'
		if ((numbytes = recvfrom(sockfd, (char *) &packet_in, MAX_PACKET_LEN - 1, 0,
			(struct sockaddr *)&their_addr, &addr_len)) == -1)
		{
			perror("recvfrom");
			exit(1);
		}


		printf("Packet Received! It contained: %d bytes.\n", numbytes);

		// Check to see if numbytes = 5, if not, send error msg
		if (numbytes != 5) 
		{
			printf("Error: Message received, but was the incorrect length.\n");			

			tx_error tx_err_len;

			tx_err_len.magic_num = htons(0x1234);
			tx_err_len.GID_server = GROUP_ID;
			tx_err_len.error_code = htons(0x0002);

			// 5. Send Error message to client
			if (sendto(sockfd, (char *)&tx_err_len, sizeof(tx_err_len), 
				0, (struct sockaddr *)&their_addr, addr_len) == -1)
			{
				perror("sendto error");
				exit(1);
			}		
		}
	
		// Check and see if the magic number is correct.
		else if (ntohs(packet_in.magic_num) != 0x1234)
		{
			printf("Error: Magic Number was '0x%X' should have been 0x1234.\n", 
				ntohs(packet_in.magic_num));

			tx_error tx_err_magic;

			tx_err_magic.magic_num = htons(0x1234);
			tx_err_magic.GID_server = GROUP_ID;
			tx_err_magic.error_code = htons(0x0001);

			// 5. Send Error message to client
			if (sendto(sockfd, (char *)&tx_err_magic, sizeof(tx_err_magic), 
				0, (struct sockaddr *)&their_addr, addr_len) == -1)
			{
				perror("sendto error");
				exit(1);
			}		
		}

		// Check and see if the port number is in range.
		else if (ntohs(packet_in.port_num) < (10010 + (packet_in.GID_client * 5)) 
			|| ntohs(packet_in.port_num) > (10010 + (packet_in.GID_client * 5) + 4))
		{
			printf("Error: Port number was '%d' this is not in range of [", 
				ntohs(packet_in.port_num)); 
			printf("%d, %d]\n", 10010 + packet_in.GID_client * 5, 
				10010 + packet_in.GID_client * 5 + 4);

			tx_error tx_err_port;

			tx_err_port.magic_num = htons(0x1234);
			tx_err_port.GID_server = GROUP_ID;
			tx_err_port.error_code = htons(0x0004);

			// 5. Send Error message to client
			if (sendto(sockfd, (char *)&tx_err_port, sizeof(tx_err_port), 
				0, (struct sockaddr *)&their_addr, addr_len) == -1)
			{
				perror("sendto error");
				exit(1);
			}		

		}	


		// A valid packet has arrived
		else
		{
		
			if (DEBUG) {
				//printf(
				printf("packet_in.magic_num = %X\n", ntohs(packet_in.magic_num));
				printf("packet_in.GID_client = %d\n", packet_in.GID_client);
				printf("packet_in.port_num = %d\n", ntohs(packet_in.port_num));
			}
			
			// Check to see if we have already found a client
			if (client_waiting == FALSE) 
			{

				// Indicate client is waiting, save IP and Port numbers
				client_waiting = TRUE;
				saved_port = ntohs(packet_in.port_num);
				saved_IP = htonl(their_addr.sin_addr.s_addr);
					
				if (DEBUG) {
					printf("No Client is waiting.\n");
					printf("Connected Client IP Address: \t(%X)\t", saved_IP);
					printf("%d.%d.%d.%d\n",
						(int)(their_addr.sin_addr.s_addr & 0xFF),
						(int)((their_addr.sin_addr.s_addr & 0xFF00)>>8),
						(int)((their_addr.sin_addr.s_addr & 0xFF0000)>>16),
						(int)((their_addr.sin_addr.s_addr & 0xFF000000)>>24));
				}
				
				// Create a tx_wait packet
				tx_wait tx_wait_packet;

				// Fill in the struct
				tx_wait_packet.magic_num = htons(0x1234);
				tx_wait_packet.GID_server = GROUP_ID;
				tx_wait_packet.port_num = htons(ntohs(packet_in.port_num));

				// 5. Send the tx_wait packet
				if (sendto(sockfd, (char *)&tx_wait_packet, sizeof(tx_wait_packet), 
					0, (struct sockaddr *)&their_addr, addr_len) == -1)
				{
					perror("sendto error");
					exit(1);
				}

				printf("Sending packet to client indicating it should wait.\n");		
	
			}

			else if (client_waiting == TRUE)
			{
				if (DEBUG) {
					printf("A client is already waiting, sending match making info.\n");
				}
				
				
				// Create a TX_Pair packet
				tx_pair tx_pair_packet;
				
				// Fill in the struct
				tx_pair_packet.magic_num = htons(0x1234);
				tx_pair_packet.GID_server = GROUP_ID;
				tx_pair_packet.IP_addr = htonl(saved_IP);
				tx_pair_packet.port_num = htons(saved_port);

				// 5. Send the tx_pair packet
				if (sendto(sockfd, (char *)&tx_pair_packet, sizeof(tx_pair_packet), 
					0, (struct sockaddr *)&their_addr, addr_len) == -1)
				{
					perror("sendto error");
					exit(1);
				}

				// Clear the IP and port info.
				client_waiting = FALSE;
				saved_port = 0;
				saved_IP = 0;
				
				printf("Sending packet to client with match making info.\n");		

			}


		}

		close(sockfd);
	}
	return 0;
}











 
