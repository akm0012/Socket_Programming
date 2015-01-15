/*	
*	Comp4320: Lab 3
*
*	File: Client.c	
*	Author: Andrew K. Marshall (akm0012)
*	Group ID: 15
*	Date: 12/2/14
*	Version: 1.0
*	Version Notes: Final Version. 
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
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10	 // how many pending connections queue will hold


#define MAX_MESSAGE_LEN 1024
#define MAX_PACKET_LEN 1029	// 1Kb for message, and 5 bytes for header
#define GROUP_ID 15 

#define DEBUG 0	// Used for debugging 1 = ON, 0 = OFF


// Struct that will be used to send data to the Server
struct transmitted_packet
{
	unsigned short magic_num;
	unsigned char GID_client;
	unsigned short port_num;
} __attribute__((__packed__));

typedef struct transmitted_packet tx_packet;

// Struct that will be used to recieve unverified incoming packets.
struct incoming_unverified_packet
{
	unsigned short short_1;
	unsigned char char_2;
	unsigned char extra_char[6];
} __attribute__((__packed__));

typedef struct incoming_unverified_packet rx_verify;

// Struct that wil be used after the incoming packet has been verified.
// Indicates we need to wait for another client
struct incoming_verified_packet_wait
{
	unsigned short magic_num;
	unsigned char GID_server;
	unsigned short port_num;
} __attribute__((__packed__));

typedef struct incoming_verified_packet_wait rx_wait;

// Struct that wil be used after the incoming packet has been verified.
// Indicates we can pair with another client
struct incoming_verified_packet_pair
{
	unsigned short magic_num;
	unsigned char GID_server;
	unsigned int IP_addr;
	unsigned short port_num;
} __attribute__((__packed__));

typedef struct incoming_verified_packet_pair rx_pair;

// Struct that wil be used if we recieve an error
struct incoming_error_packet
{
	unsigned short magic_num;
	unsigned char GID_server;
	unsigned short error_code;
} __attribute__((__packed__));

typedef struct incoming_error_packet rx_error;

// Struct that is used to contain the a NIM Move
struct nim_move_packet
{
	unsigned short magic_num;
	unsigned char GID_client;
	unsigned char row_num;
	unsigned char tokens_remove;
} __attribute__((__packed__));

typedef struct nim_move_packet nim_move;

// Prototypes
unsigned short make_short(unsigned char, unsigned char);
unsigned int make_int(unsigned char, unsigned char, 
	unsigned char, unsigned char);
int create_and_run_TCP_server(tx_packet);
unsigned char get_char_from_user(char);
void print_board(int[]);

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int main(int argc, char *argv[])
{
    
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes_tx;
	int numbytes_rx;
	struct sockaddr_in their_addr;
	socklen_t addr_len;
	
	char *my_server;	// The host server name
	char *server_port;	// The port we will be using
	char *my_port;      // The port we are willing to play on
	
	// The packet we will send
	tx_packet packet_out;
	
	if (argc != 4) {
		fprintf(stderr,"Incorrect arguments. Refer to the README.\n");
		exit(1);
	}

	// Get the params from the command line
	my_server = argv[1];
	server_port = argv[2];
	my_port = argv[3];

	// Check to make sure the Port Number is in the correct range
	// atoi() - used to convert strings to int
   if (atoi(my_port) < (10010 + (GROUP_ID * 5))
         || atoi(my_port) > (10010 + (GROUP_ID * 5) + 4))
	{
        printf("Error: Port number was '%s' this is not in range of [",
               my_port);
        printf("%d, %d]\n", 10010 + GROUP_ID * 5,
               10010 + GROUP_ID * 5 + 4);
		exit(1);
	}

	if (DEBUG) {
		printf("----- Parameters -----\n");
		printf("Server: %s\n", my_server);
		printf("Server Port: %s\n", server_port);
		printf("My Port: %s\n", my_port);
	}

	// Get the Packet Ready to Send
	packet_out.magic_num = htons(0x1234);
	packet_out.GID_client = GROUP_ID;
	packet_out.port_num = htons((unsigned short) strtoul(my_port, NULL, 0));
	
	if (DEBUG) {
		printf("\n----- Packet Out -----\n");
		printf("packet_out.magic_num: %X\n", ntohs(packet_out.magic_num));
		printf("packet_out.GID_client: %d\n", packet_out.GID_client);
		printf("packet_out.port_num: %d\n", ntohs(packet_out.port_num));

	}
	
	memset(&hints, 0, sizeof hints);	// put 0's in all the mem space for hints (clearing hints)
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(my_server, server_port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) 
		{
			perror("Error: socket");
			continue;
		}
		
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "Error: failed to bind socket\n");
		return 2;
	}

	// Send the data to the server
	if ((numbytes_tx = sendto(sockfd, (char *)&packet_out, sizeof(packet_out), 0, 
		p->ai_addr, p->ai_addrlen)) == -1) 
	{    
		perror("Error: sendto");
		exit(1);
    }

	if (DEBUG) {
		printf("Sent %d bytes to %s\n", numbytes_tx, argv[1]);
		printf("Waiting for responce...\n\n");
	}

	addr_len = sizeof their_addr;

	// Create the structs needed for receiving a packet	
	rx_verify rx_check;
	rx_pair rx_pair_info;
	
	if ((numbytes_rx = recvfrom(sockfd, (char *)&rx_check, MAX_PACKET_LEN, 0, 
		(struct sockaddr *)&their_addr, &addr_len)) == -1)
	{
		perror("recvfrom");
		exit(1);
	}

	if (DEBUG) {
		printf("Incoming Packet Size: %d\n", numbytes_rx);
	}

	// Check and see if the packet was an error.
	if (numbytes_rx == 5 && rx_check.extra_char[0] == 0x00)
	{
		// Check so see what error is was.
		char error_code = 0x00;
		error_code = rx_check.extra_char[1];

		// Incorrect Magic Number
		if (error_code == 0x01) 
		{
			printf("Error: The magic number in the sent request was incorrect.\n");
			printf("Error code: %X\n", error_code);
			exit(1);
		}

		// Incorrect Length 
		else if (error_code == 0x02) 
		{
			printf("Error: The packet length in the sent request was incorrect.\n");
			printf("Error code: %X\n", error_code);
			exit(1);
		}

		// Port not in correct range 
		else if (error_code == 0x04)
		{
			printf("Error: The port in the sent request was not in the correct range.\n");
			printf("Error code: %X\n", error_code);
			exit(1);
		}

		// Unknown error occured.
		else
		{
			printf("Error: An unknown error occured.\n");
			printf("Error code: %X\n", error_code);
			exit(1);
		}
	}

	// This is a wait packet
	else if (numbytes_rx == 5)
	{
		printf("Need to wait until another client wants to play. Creating TCP Server.\n");

		//TODO: Create a TCP Server with the sent port address. 
		create_and_run_TCP_server(packet_out);

	}

	// This is a pair packet
	else if (numbytes_rx == 9)
	{
		printf("The server has sent match making information.\n");

		rx_pair_info.magic_num = ntohs(rx_check.short_1);
		rx_pair_info.GID_server = rx_check.char_2;	

		int IP_in = make_int(rx_check.extra_char[0],
			rx_check.extra_char[1],
			rx_check.extra_char[2],
			rx_check.extra_char[3]);

		rx_pair_info.IP_addr = IP_in;
		rx_pair_info.port_num = make_short(rx_check.extra_char[4], rx_check.extra_char[5]);

		if (DEBUG) {
			printf("rx_pair_info.magic_num = %X\n", rx_pair_info.magic_num);
			printf("rx_pair_info.GID_server = %d\n", rx_pair_info.GID_server);
			printf("rx_pair_info.IP_addr = %X (", rx_pair_info.IP_addr);
         printf("%d.%d.%d.%d)\n",
				(int)(their_addr.sin_addr.s_addr & 0xFF),
				(int)((their_addr.sin_addr.s_addr & 0xFF00)>>8),
				(int)((their_addr.sin_addr.s_addr & 0xFF0000)>>16),
				(int)((their_addr.sin_addr.s_addr & 0xFF000000)>>24));
			printf("rx_pair_info.port_num = %d\n", rx_pair_info.port_num);
		}

		// Connect to a TCP server with the above information.
		connect_to_TCP_server(rx_pair_info);
	}

	else
	{
		//TODO: This should never happen
	}


	freeaddrinfo(servinfo);
	close(sockfd);

	return 0;
}

// Support Functions

/*
* This function combines four bytes to get an int. 
*
*/
unsigned int make_int(unsigned char a, unsigned char b, 
	unsigned char c, unsigned char d) 
{
	unsigned int val = 0;
	val = a;
	val <<= 8;
	val |= b;
	
	val <<= 8;
	val |= c;

	val <<= 8;
	val |= d;	

	return val;
}

/*
* This function combines two bytes to get a short. 
*
*/
unsigned short make_short(unsigned char a, unsigned char b) 
{
	unsigned short val = 0;
	val = a;
	val <<= 8;
	val |= b;
	return val;
}


int create_and_run_TCP_server(tx_packet server_info)
{
	if (DEBUG){
		printf("Creating TCP Server...\n");
	}

	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	nim_move move_in, move_out;

	char my_port[5] = {0};      // The port we are willing to play on

	int tokens[4];
	
	// Converts the short back to a char*
	sprintf(my_port, "%d", ntohs(server_info.port_num));

	if (DEBUG) {
		printf("my_port* = %s\n", my_port);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	
	if ((rv = getaddrinfo(NULL, my_port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
							 p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
					   sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
		
		break;
	}
	
	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}
	
	freeaddrinfo(servinfo); // all done with this structure
	
	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	if (DEBUG) {	
		printf("server: waiting for connections...\n");
	}	

	// Print game board, wait for first move from other client 

	// Initialize the game
	tokens[0] = 1;
	tokens[1] = 3;
	tokens[2] = 5;
	tokens[3] = 7;

	print_board(tokens);

	int token_count = count_array(tokens);

		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
		//	continue;
		}
		
		inet_ntop(their_addr.ss_family,
				  get_in_addr((struct sockaddr *)&their_addr),
				  s, sizeof s);
		if (DEBUG) {
			printf("server: got connection from %s\n", s);
		}		
	do 
	{  // main accept() loop


		if (DEBUG) {
			printf("Waiting to recv...\n");
		}		
		if((recv(new_fd, (char*)&move_in, sizeof(move_in), 0)) == -1)
		{
			perror("recv_error");
			exit(1);
		}
		
		if (DEBUG) {
			printf("\n-----Recieved a move!-----\n");
			printf("Magic Number: %X\n", ntohs(move_in.magic_num));
			printf("Client GID: %d\n", move_in.GID_client);
			printf("Row Number: %d\n", move_in.row_num);
			printf("Tokens to Remove: %d\n\n", move_in.tokens_remove);
		}		

		// Check for errors, or if we sent an error  
		
		// Check and see if we sent a bad move
		if (move_in.row_num == 0xFF && move_in.tokens_remove == 0xFF)
		{
			print_board(tokens);	
			printf("You sent an invalid move. Please make your move again.\n");
			move_out.magic_num = htons(0x1234);
			move_out.GID_client = GROUP_ID;
			move_out.row_num = get_char_from_user('r');
			move_out.tokens_remove = get_char_from_user('t');
		}

		// Else they sent us a move and we need to check if it is valid
		else
		{
			if (move_in.row_num < 1 || move_in.row_num > 4 )
			{
				// Row out of range
				// Send an error packet 
				move_out.magic_num = htons(0x1234);
				move_out.GID_client = GROUP_ID;
				move_out.row_num = 0xFF;
				move_out.tokens_remove = 0xFF;
			}

			else if (move_in.tokens_remove > tokens[move_in.row_num - 1])
			{
				// Trying to remove more tokens than are present
				// Send an error packet 
				move_out.magic_num = htons(0x1234);
				move_out.GID_client = GROUP_ID;
				move_out.row_num = 0xFF;
				move_out.tokens_remove = 0xFF;
			}

			else
			{
				// A Valid move was made. Update the game board.
				tokens[move_in.row_num - 1] = 
					tokens[move_in.row_num - 1] - move_in.tokens_remove;

				print_board(tokens);	
	
				if (count_array(tokens) != 0) {
					// Have the user enter the next move	
					move_out.magic_num = htons(0x1234);
					move_out.GID_client = GROUP_ID;
					move_out.row_num = get_char_from_user('r');
					move_out.tokens_remove = get_char_from_user('t');
				
					printf("Move Sent.\n");
				}

				// GAME OVER, You lose. 
				else
				{
					printf("\n----- GAME OVER -----\n");
					printf("\n----- You Lost! -----\n");
					close(sockfd);
					exit(1);
				}
			}

		}

		if (DEBUG) {
			printf("\n-----Sending move.-----\n");
			printf("Magic Number: %X\n", ntohs(move_out.magic_num));
			printf("Client GID: %d\n", move_out.GID_client);
			printf("Row Number: %d\n", move_out.row_num);
			printf("Tokens to Remove: %d\n\n", move_out.tokens_remove);
		}
		
		if (send(new_fd, (char*)&move_out, sizeof(move_out), 0) == -1){
			perror("send");
		}
		
		
		if (DEBUG) {
			printf("Sent.\n");
		}

		// Check and see if we sent a valid packet
		// This is here just so the we can know who won the game
		if (move_out.row_num > 0 && move_out.row_num < 5
			&& move_out.tokens_remove <= tokens[move_out.row_num - 1])
		{

			// Update the game board
			tokens[move_out.row_num - 1] = 
				tokens[move_out.row_num - 1] - move_out.tokens_remove;

			// Check to see if there are still tokens on the board
			token_count = count_array(tokens);
		}

	} while (token_count != 0);

	// Token Count == 0; You Win!
	printf("\n----- GAME OVER -----\n");
	printf("\n----- You WON! -----\n");
	
	return 0;
    
    
}

int connect_to_TCP_server(rx_pair server_info_in)
{
	if(DEBUG) {
		printf("CONNECT_TO_TCP_SERVER() fx()\n");
	}

	int sockfd, numbytes;
	char buf[MAX_PACKET_LEN];
	struct addrinfo hints, *servinfo, *p;
	int status;
	char s[INET6_ADDRSTRLEN];
	
	char* hostname;
	char port[5] = {0};      // The port we are willing to play on

	int tokens[4];
	nim_move move_out, move_in;
	
	// Converts the short back to a char*
	sprintf(port, "%d", server_info_in.port_num);

	// Converts the hex IP address into a char* using dotted notation
	struct in_addr addr;
	addr.s_addr = htonl(server_info_in.IP_addr); 
	hostname = inet_ntoa(addr);
	
	if (DEBUG) {
		printf("hostname: %s\n", hostname);
		printf("port: %s\n", port);
	}
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	if ((status = getaddrinfo(hostname, port, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return 1;
	}
	
	// Loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			perror("Socket error");
			continue;
		}
		
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("Connect error");
			continue;
		}
		
		break;
	}
	
	if (p == NULL)
	{
		fprintf(stderr, "Failed to connect!\n");
		return 2;
	}
	
	if (DEBUG) {
		inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
		
		printf("Connected to: %s\n", s);
	}

	freeaddrinfo(servinfo); 	// All done with this structure

	// Initliaize game
	tokens[0] = 1;
	tokens[1] = 3;
	tokens[2] = 5;
	tokens[3] = 7;

	// Print out game board
	print_board(tokens);

	int token_count = count_array(tokens);

	move_out.magic_num = htons(0x1234);
	move_out.GID_client = GROUP_ID;
	move_out.row_num = get_char_from_user('r');
	move_out.tokens_remove = get_char_from_user('t');

	if (DEBUG) {
		printf("\n-----Sending move.-----\n");
		printf("Magic Number: %X\n", ntohs(move_out.magic_num));
		printf("Client GID: %d\n", move_out.GID_client);
		printf("Row Number: %d\n", move_out.row_num);
		printf("Tokens to Remove: %d\n\n", move_out.tokens_remove);
	}


	if (send(sockfd, (char *)&move_out, sizeof(move_out), 0) == -1)
	{
		perror("Send Error");
	}

	printf("Move Sent.\n");
	
	// Check and see if we sent a valid packet
	if (move_out.row_num > 0 && move_out.row_num < 5
		&& move_out.tokens_remove <= tokens[move_out.row_num - 1])
	{
		// Update the game board
		tokens[move_out.row_num - 1] = 
			tokens[move_out.row_num - 1] - move_out.tokens_remove;

		// Update token count
		token_count = count_array(tokens);

	}	

	do
	{	// Main Loop 

		// Get user input to make the first move
	

		

		if (DEBUG) {
			printf("Waiting to recv...\n");
		}
		if ((recv(sockfd, (char *)&move_in, sizeof(move_in), 0)) == -1)
		{
			perror("recv error");
			exit(1);
		}
		
		if (DEBUG) {
			printf("\n-----Recieved a move!-----\n");
			printf("Magic Number: %X\n", ntohs(move_in.magic_num));
			printf("Client GID: %d\n", move_in.GID_client);
			printf("Row Number: %d\n", move_in.row_num);
			printf("Tokens to Remove: %d\n\n", move_in.tokens_remove);
		}		

		// Check for errors, or if we sent an error  
		
		// Check and see if we sent a bad move
		if (move_in.row_num == 0xFF && move_in.tokens_remove == 0xFF)
		{
			print_board(tokens);	
			printf("You sent an invalid move. Please make your move again.\n");
			move_out.magic_num = htons(0x1234);
			move_out.GID_client = GROUP_ID;
			move_out.row_num = get_char_from_user('r');
			move_out.tokens_remove = get_char_from_user('t');
		}

		// Else they sent us a move and we need to check if it is valid
		else
		{
			if (move_in.row_num < 1 || move_in.row_num > 4 )
			{
				// Row out of range
				// Send an error packet 
				move_out.magic_num = htons(0x1234);
				move_out.GID_client = GROUP_ID;
				move_out.row_num = 0xFF;
				move_out.tokens_remove = 0xFF;
			}

			else if (move_in.tokens_remove > tokens[move_in.row_num - 1])
			{
				// Trying to remove more tokens than are present
				// Send an error packet 
				move_out.magic_num = htons(0x1234);
				move_out.GID_client = GROUP_ID;
				move_out.row_num = 0xFF;
				move_out.tokens_remove = 0xFF;
			}

			else
			{
				// A Valid move was made. Update the game board.
				tokens[move_in.row_num - 1] = 
					tokens[move_in.row_num - 1] - move_in.tokens_remove;

				print_board(tokens);	
	
				if (count_array(tokens) != 0) {
					// Have the user enter the next move	
					move_out.magic_num = htons(0x1234);
					move_out.GID_client = GROUP_ID;
					move_out.row_num = get_char_from_user('r');
					move_out.tokens_remove = get_char_from_user('t');
					printf("Move Sent.\n");
				}

				// GAME OVER, You lose. 
				else
				{
					printf("\n----- GAME OVER -----\n");
					printf("\n----- You Lost! -----\n");
					close(sockfd);
					exit(1);
				}
			}

		}


		if (DEBUG) {
			printf("\n-----Sending move.-----\n");
			printf("Magic Number: %X\n", ntohs(move_out.magic_num));
			printf("Client GID: %d\n", move_out.GID_client);
			printf("Row Number: %d\n", move_out.row_num);
			printf("Tokens to Remove: %d\n\n", move_out.tokens_remove);
		}

		if (send(sockfd, (char *)&move_out, sizeof(move_out), 0) == -1)
		{
			perror("Send Error");
		}

		// Check and see if we sent a valid packet
		// This is here just so the we can know who won the game
		if (move_out.row_num > 0 && move_out.row_num < 5
			&& move_out.tokens_remove <= tokens[move_out.row_num - 1])
		{

			// Update the game board
			tokens[move_out.row_num - 1] = 
				tokens[move_out.row_num - 1] - move_out.tokens_remove;

			// Check to see if there are still tokens on the board
			token_count = count_array(tokens);
		}

	} while (token_count != 0);

	// Token Count == 0; You Win!
	printf("\n----- GAME OVER -----\n");
	printf("\n----- You WON! -----\n");
	
	return 0;
}

/**
*	Returns the number of elements in the array.	
*	@param array_in: The array you want to count. 
*/
int count_array(int array_in[4])
{
	int sum = 0;
	int i = 0;
	for (i; i < 4; i++)
	{
		sum = sum + array_in[i];
	}

	return sum;
}

/**
*	Prints the game board.
*
*	@param tokens: The array holding the tokens count
*/
void print_board(int tokens[4])
{
	printf("\nRow #\t: Number of Tokens\n");
	
	int i = 0;
	for (i; i < 4; i++) {
		printf("%d\t: %d\n", i + 1, tokens[i]);
	}

}

/**
*	Asks the user for data input for this game.
*
*	@param x: Whether we want the row or tokens. 
*/
unsigned char get_char_from_user(char x)
{
	int char_out;
	int iterations = 0;

	char_out = 0;

	do {

	if (iterations != 0)
		{
			printf("Invalid input. Try Again.\n");
		}

		if (x == 'r')
		{
			if (0) {
				printf("Getting Row from user.\n");
			}

			printf("Enter the row number: ");
			if (scanf("%d", &char_out) == 0)
			{
				char c;
				while ((c = getchar()) != '\n'){
					// Clear buffer
				}
			}
		}

		else if (x == 't')
		{
			if (0) {
				printf("Getting Tokens to delete from user.\n");
			}

			printf("Enter number of tokens you wish to delete: ");
			if (scanf("%d", &char_out) == 0)
			{
				char c;
				while ((c = getchar()) != '\n'){
					// Clear buffer
				}
			}
		}	

		else
		{
			printf("Error: Unknown parameter in get_char_from_user()\n");
			exit(1);
		}

		if (0) {
			printf("char_out: %d\n", char_out);
		}
	
		iterations++;

	} while (char_out < 1 || char_out > 7);
	return (unsigned char) char_out;
}































