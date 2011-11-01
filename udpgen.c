/*
 ============================================================================
 Name        : udpgen.c
 Author      : Surat Teerapittayanon (steerapi@mit.edu)
 Version     : 0.0.1
 Copyright   :

 Copyright (c) 2011 MIT

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
 OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 Description:

 A simple  UDP TRAFFIC GENERATOR
 Evaluate % of successful packets compared to transmitted packets

 Compilation:

 gcc ipgen.c -o ipgen -O3

 There are two types of unsuccessful packages
 1. Loss or drop by lower layers.
 2. Fail the checksum.
 This program counts both losses.

 Packet structure:
 4 bytes for seq | n bytes for Data | 2 bytes for checksum

 Usage: ./udpgen [-v] -[s|c] [-n NUM_PACKET] [-l PACKET_LENGTH_IN_BYTES] [-b BANDWIDTH_IN_BYTES_PER_SEC] IPADDRESS PORT
 -v means verbose

 e.g.
 ./udpgen -vcl 1000 -b 10000 127.0.0.1 5556
 ./udogen -vsl 1000 127.0.0.1 5556

 TODO:
 -Add high accuracy timer.

 ============================================================================
 */

#define USAGE "Usage: %s [-v] -[s|c] [-n NUM_PACKET] [-l PACKET_LENGTH_IN_BYTES] [-b BANDWIDTH_IN_BYTES_PER_SEC] IPADDRESS PORT\n"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>/* close() */
#include <string.h> /* memset() */
#include <time.h>

#define MODE_CLIENT 1
#define MODE_SERVER 2

uint16_t csum(uint16_t * addr, int len) {
	int nleft = len;
	uint32_t sum = 0;
	uint16_t *w = addr;
	uint16_t answer = 0;

	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}
	if (nleft == 1) {
		*(uint8_t *) (&answer) = *(uint8_t *) w;
		sum += answer;
	}
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

int successPkg = 0;
int totalPkg = 0;
int sock, mode = MODE_SERVER;
time_t seconds;
int total_bytes_recv = 0;
int max_seq = 0;
int max = 0;
int numfailchecksum = 0;

void print_summary() {
	printf("================================================\n");
	printf("SUMMARY\n");
	printf("================================================\n");
	printf("PERCENT SUCCESS: %d/%d = %.2f %%\n", successPkg, max,
			100.0 * successPkg / max);
	printf("PERCENT FAILED: %d/%d = %.2f %%\n", numfailchecksum, max,
			100.0 * numfailchecksum / max);
	int drop = max - successPkg - numfailchecksum;
	printf("PERCENT DROP: %d/%d = %.2f %%\n", drop, max, 100.0 * drop / max);
	time_t elapsed_time = time(NULL) - seconds;
	printf("THROUGHPUT %0.2f BYTES/SEC\n",
			1.0 * total_bytes_recv / elapsed_time);
}

void sighandler(int sig) {
	if (mode == MODE_SERVER) {
		printf("\n");
		print_summary();
	}
	printf("EXITING...\n");
	close(sock);
	exit(EXIT_SUCCESS);
}

int main(int argc, // Number of strings in array argv
		char *argv[], // Array of command-line argument strings
		char **envp) { // Array of environment variable strings

	char *ipaddr = "127.0.0.1";
	int plen = 100;
	double bw = 100.0;
	int port = 5556;
	int verbose = 0;
	int numpkt = 0;
	char c;
	printf("================================================\n");
	printf("IP TRAFFIC GENERATOR\n");
	while (1) {
		if ((c = getopt(argc, argv, "scn:l:b:hv")) == EOF)
			break;
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		case 's':
			mode = MODE_SERVER;
			break;
		case 'c':
			mode = MODE_CLIENT;
			break;
		case 'l':
			plen = atol(optarg);
			break;
		case 'b':
			bw = atof(optarg);
			break;
		case 'n':
			numpkt = atol(optarg);
			break;
		case 'h':
		default:
			printf(USAGE, argv[0]);
			printf("================================================\n");
			return EXIT_FAILURE;
		}
	}

	if (mode == MODE_CLIENT) {
		printf("MODE: CLIENT\n");
	} else {
		printf("MODE: SERVER\n");
	}

	printf("PACKET LENGTH: %d BYTES\n", plen);
	if (mode == MODE_CLIENT) {
		printf("BANDWIDTH: %0.2f BYTES/SEC\n", bw);
	}
	if (mode == MODE_CLIENT && numpkt != 0) {
		printf("NUM PACKET: %d PACKETS\n", numpkt);
	}

	// Get ip address
	if (optind < argc) {
		ipaddr = argv[optind++];
		port = atol(argv[optind++]);
	}
	printf("IPADDRESS: %s\n", ipaddr);
	printf("PORT: %d\n", port);

	//	assert( plen!=0);
	//	assert( bw!=0);
	//	assert( ipaddr!=0);
	//	assert( mode!=0);
	//	assert( port!=0);

	printf("================================================\n");

	//int sock;
	struct sockaddr_in sa;
	int total = plen;

	int size = total - 2; //2 for checksum
	int fill = size - 1;//1 for end string 8 for sequence number
	char* buffertotal = (char*) malloc(total);
	int* seq = (int*) buffertotal;
	char* buffer = buffertotal;
	uint16_t* sum = (uint16_t*) (buffer + size);
	// Temp var for loop
	int i;

	// Create socket
	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (-1 == sock) /* if socket failed to initialize, exit */
	{
		printf("Error creating socket.\n");
		exit(EXIT_FAILURE);
	}

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(ipaddr);
	sa.sin_port = htons(port);

	//Setup interupt signal
	signal(SIGABRT, &sighandler);
	signal(SIGTERM, &sighandler);
	signal(SIGINT, &sighandler);

	//Prepare buffer
	char
			* str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	int len = strlen(str);
	int offset = 4;

	//Calculate checksum
	if (mode == MODE_CLIENT) {
		while (offset < fill - offset) {
			memcpy(buffer + offset, str, len);
			offset += len;
		}
		i = 0;
		while (offset < fill) {
			buffer[offset++] = str[i];
			i++;
		}
		buffer[offset] = '\0';
		//CLIENT
		//Send
		ssize_t bytes_sent;
		int num = 0;
		printf("SENDING...\n");
		for (; num < numpkt || numpkt == 0;) {
			*sum = csum((unsigned short *) buffer, size);
			*sum = htons(*sum);
			if (verbose) {
				printf(
						"SENDING PACKET SEQ#[%d] LENGTH [%d] BYTES BANDWIDTH [%f] BYTES/SEC\n",
						num, plen, bw);
			}
			bytes_sent = sendto(sock, buffer, total, 0, (struct sockaddr*) &sa,
					sizeof sa);
			int interval = (int) (1000000.0 * plen / bw);
			usleep(interval);
			if (bytes_sent < 0) {
				fprintf(stderr, "Error sending packet.\n");
				close(sock);
				exit(EXIT_FAILURE);
			}
			num++;
			*seq = htonl(num);
		}
		if (numpkt != 0) {
			*seq = htonl(0xffffffff);
			bytes_sent = sendto(sock, buffertotal, total, 0,
					(struct sockaddr*) &sa, sizeof sa);
		}
	} else {
		//SERVER
		if (-1 == bind(sock, (struct sockaddr *) &sa, sizeof(sa))) {
			perror("Error bind failed.\n");
			close(sock);
			exit(EXIT_FAILURE);
		}

		ssize_t recsize;
		socklen_t fromlen = sizeof(sa);

		int juststart = 1;
		printf("RECEIVING... CTRL-C TO EXIT AND PRINT A SUMMARY\n");
		for (;;) {
			memset(buffer, 0, total);
			recsize = recvfrom(sock, (void *) buffer, total, 0,
					(struct sockaddr *) &sa, &fromlen);
			if (recsize < 0) {
				fprintf(stderr, "Error receiving packet.\n");
				close(sock);
				exit(EXIT_FAILURE);
			}
			if (juststart) {
				seconds = time(NULL);
				juststart = 0;
			} else {
				total_bytes_recv += recsize;
			}
			total_bytes_recv += recsize;
			int size = recsize - 2;
			uint16_t sum = *(uint16_t*) (buffer + size);
			sum = ntohs(sum);
			uint16_t checksum = csum((unsigned short *) buffer, size);
			int seq = *(int*) buffer;
			seq = ntohl(seq);
			if (seq == 0xffffffff) {
				print_summary();
				exit(EXIT_SUCCESS);
			}
			if (seq > max_seq) {
				max_seq = seq;
			}
			char* check;
			if (sum == checksum) {
				successPkg++;
				check = "PASSED";
			} else {
				numfailchecksum++;
				check = "FAILED";
				//				if (verbose) {
				//					printf("CHECKSUM: %d\n", checksum);
				//					printf("CONTENT: ");
				//					for (i = 0; i < recsize - ipheader_size; ++i) {
				//						printf("%c", (char) *(buffer + 4 + i));
				//					}
				//					printf("\n");
				//				}
			}
			totalPkg++;
			max = totalPkg > max_seq ? totalPkg : max_seq;
			if (verbose) {
				time_t elapsed_time = time(NULL) - seconds;
				printf(
						"%s RECEIVED SEQ#[%d] LENGTH [%d] BYTES [%d/%d] PACKETS THROUGHPUT [%f] BYTES/SEC\n",
						check, seq, recsize, successPkg, max,
						1.0 * total_bytes_recv / elapsed_time);
				//				printf("CONTENT: ");
				//				for (i = 0; i < recsize - ipheader_size; ++i) {
				//					printf("%c", (char) *(buffer + 4 + i));
				//				}
				//				printf("\n");
			}
			recsize = 0;
		}
	}

	return EXIT_SUCCESS;
}
