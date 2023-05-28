#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <bitset>
#include <ctime>
#include <endian.h>

#include "err.h"
#include "common.h"

struct sockaddr_in get_send_address(const char *host, uint16_t port) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	
	struct addrinfo *address_result;
	CHECK(getaddrinfo(host, NULL, &hints, &address_result));
	
	struct sockaddr_in send_address;
	send_address.sin_family = AF_INET; // IPv4
	send_address.sin_addr.s_addr =
			((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr; // IP address
	send_address.sin_port = htons(port); // port from the command line
	
	freeaddrinfo(address_result);
	
	return send_address;
}

void send_message(int socket_fd, const struct sockaddr_in *send_address, const uint8_t *message, size_t message_length) {
	int send_flags = 0;
	socklen_t address_length = (socklen_t) sizeof(*send_address);
	errno = 0;
	ssize_t sent_length = sendto(socket_fd, message, message_length, send_flags,
	                             (struct sockaddr *) send_address, address_length);
	if (sent_length < 0) {
		PRINT_ERRNO();
	}
	ENSURE(sent_length == (ssize_t) message_length);
}

int main(int argc, char **argv) {
	std::string DEST_ADDR = "";
	uint16_t    DATA_PORT = 20000 + (440014 % 10000);
	uint64_t    PSIZE     = 512;
	std::string NAZWA     = "Nienazwany Nadajnik";
	
	int opt;
	while ((opt = getopt(argc, argv, "a:P:p:n:")) != -1) {
		if (opt == 'a') {
			DEST_ADDR = optarg;
		}
		else if (opt == 'P') {
			uint64_t tmp = string_to_int(optarg);
			if (tmp > UINT16_MAX) {
				std::cerr << "Too big number.\n";
				exit(1);
			}
			DATA_PORT = tmp;
		}
		else if (opt == 'p') {
			PSIZE = string_to_int(optarg);
		}
		else if (opt == 'n') {
			NAZWA = optarg;
		}
		else if (opt == ':') {
			std::cerr << "Option " << optopt << " requires a value.\n";
		}
		else if (opt == '?') {
			std::cerr << "Unknown option: " << optopt << "\n";
		}
	}
	
	if (DEST_ADDR.empty()) {
		std::cerr << "Option -a must be specified.\n";
		exit(1);
	}
	
	struct sockaddr_in send_address = get_send_address(DEST_ADDR.c_str(), DATA_PORT);
	
	int socket_fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (socket_fd < 0) {
		PRINT_ERRNO();
	}
	
	uint64_t session_id = time(NULL), first_byte_num = 0, first_byte_num_htobe = 0;
	session_id = htobe64(session_id);
	uint8_t buffer[2 * sizeof(uint64_t) + PSIZE];
	while (true) {
		memset(buffer, 0, PSIZE + 2 * sizeof(uint64_t));
		memcpy(buffer, &session_id, sizeof(uint64_t));
		memcpy(buffer + sizeof(uint64_t), &first_byte_num_htobe, sizeof(uint64_t));
		
		if (fread(buffer + 2 * sizeof(uint64_t), 1, PSIZE, stdin) != PSIZE) {
			break;
		}
		
		send_message(socket_fd, &send_address, buffer, 2 * sizeof(uint64_t) + PSIZE);
		
		first_byte_num += PSIZE;
		first_byte_num_htobe = htobe64(first_byte_num);
	}
	
	CHECK_ERRNO(close(socket_fd));
	
	return 0;
}
