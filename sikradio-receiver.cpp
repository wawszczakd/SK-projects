#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <bitset>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <endian.h>
#include <set>
#include <atomic>

#include "err.h"
#include "common.h"

class semaphore {
private:
	std::mutex mut;
	std::condition_variable cv;
	std::atomic<uint64_t> cnt = 0;
	
public:
	void init(uint64_t _cnt) {
		cnt = _cnt;
	}
	
	void release() {
		std::lock_guard<decltype(mut)> lock(mut);
		cnt++;
		cv.notify_one();
	}
	
	void acquire() {
		std::unique_lock<decltype(mut)> lock(mut);
		while (cnt.load() == 0) {
			cv.wait(lock);
		}
		cnt--;
	}
};

int bind_socket(uint16_t port) {
	int socket_fd = socket(AF_INET, SOCK_DGRAM, 0); // creating IPv4 UDP socket
	ENSURE(socket_fd >= 0);
	// after socket() call; we should close(sock) on any execution path;
	
	struct sockaddr_in server_address;
	server_address.sin_family = AF_INET; // IPv4
	server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
	server_address.sin_port = htons(port);
	
	// bind the socket to a concrete address
	CHECK_ERRNO(bind(socket_fd, (struct sockaddr *) &server_address,
	                 (socklen_t) sizeof(server_address)));
	
	return socket_fd;
}

size_t read_message(int socket_fd, struct sockaddr_in *client_address, uint8_t *buffer, size_t max_length) {
	socklen_t address_length = (socklen_t) sizeof(*client_address);
	int flags = 0; // we do not request anything special
	errno = 0;
	ssize_t len = recvfrom(socket_fd, buffer, max_length, flags,
	                       (struct sockaddr *) client_address, &address_length);
	if (len < 0) {
		PRINT_ERRNO();
	}
	return (size_t) len;
}

in_addr_t convert_addr(char* adr) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; // IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	
	struct addrinfo *address_result;
	getaddrinfo(adr, NULL, &hints, &address_result);
	
	return ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
}

int main(int argc, char **argv) {
	std::string ADDR      = "";
	uint16_t    DATA_PORT = 20000 + (440014 % 10000);
	uint64_t    BSIZE     = 65536;
	
	int opt;
	while ((opt = getopt(argc, argv, "a:P:b:")) != -1) {
		if (opt == 'a') {
			ADDR = optarg;
		}
		else if (opt == 'P') {
			uint64_t tmp = string_to_int(optarg);
			if (tmp > UINT16_MAX) {
				exit(1);
			}
			DATA_PORT = tmp;
		}
		else if (opt == 'b') {
			BSIZE = string_to_int(optarg);
		}
		else if (opt == ':') {
			std::cerr << "Option " << optopt << " requires a value.\n";
		}
		else if (opt == '?') {
			std::cerr << "Unknown option: " << optopt << "\n";
		}
	}
	
	if (ADDR.empty()) {
		std::cerr << "Option -a must be specified.\n";
		exit(1);
	}
	
	uint64_t current_session_id = 0, first_byte_num = 0, max_position = 0;
	uint64_t psize = 0, start_printing_byte = 0;
	
	uint8_t read_buffer[2 * sizeof(uint64_t) + BSIZE];
	uint8_t print_buffer[BSIZE];
	
	uint64_t print_position = 0, n = 0;
	std::atomic<bool> printing = false;
	semaphore free, taken;
	std::condition_variable cv;
	std::mutex mut, print_mut;
	
	auto printer_function = [&]() {
		while (true) {
			taken.acquire();
			
			std::unique_lock<decltype(mut)> lock(mut);
			while (!printing.load()) {
				cv.wait(lock);
			}
			
			fwrite(print_buffer + (print_position % n) * psize, 1, psize, stdout);
			memset(print_buffer + (print_position % n) * psize, 0, psize);
			
			print_mut.lock();
			print_position++;
			print_mut.unlock();
			
			free.release();
		}
	};
	std::thread printer{printer_function};
	
	std::set<uint64_t> lost;
	
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	
	struct addrinfo *address_result;
	CHECK(getaddrinfo(ADDR.c_str(), NULL, &hints, &address_result));
	
	struct sockaddr_in desired_address;
	desired_address.sin_family = AF_INET;
	desired_address.sin_addr.s_addr = ((struct sockaddr_in *)(address_result->ai_addr))->sin_addr.s_addr;
	desired_address.sin_port = htons(DATA_PORT);
	freeaddrinfo(address_result);
	
	int socket_fd = bind_socket(DATA_PORT);
	struct sockaddr_in client_address;
	while (true) {
		memset(read_buffer, 0, 2 * sizeof(uint64_t) + BSIZE);
		uint64_t read_length = read_message(socket_fd, &client_address, read_buffer, 2 * sizeof(uint64_t) + BSIZE);
		
		if ((std::string) inet_ntoa(desired_address.sin_addr) != (std::string) inet_ntoa(client_address.sin_addr)) {
			continue;
		}
		
		uint64_t session_id = 0, byte_num = 0;
		memcpy(&session_id, read_buffer, sizeof(uint64_t));
		session_id = be64toh(session_id);
		memcpy(&byte_num, read_buffer + sizeof(uint64_t), sizeof(uint64_t));
		byte_num = be64toh(byte_num);
		
		if (session_id < current_session_id) {
			continue;
		}
		else if (session_id > current_session_id) {
			for (uint64_t i = 0; i < n; i++) {
				free.acquire();
			}
			// The printer is now waiting on the semaphore taken.
			printing.store(false);
			
			current_session_id = session_id;
			first_byte_num = byte_num;
			psize = read_length - 2 * sizeof(uint64_t);
			start_printing_byte = first_byte_num + BSIZE * 3 / 4;
			
			memset(print_buffer, 0, BSIZE);
			n = BSIZE / psize;
			free.init(n);
			print_position = first_byte_num / psize;
			
			lost.clear();
			max_position = first_byte_num / psize;
		}
		
		free.acquire();
		
		uint64_t data_position = byte_num / psize;
		print_mut.lock();
		if (data_position >= print_position) {
			memcpy(print_buffer + (data_position % n) * psize, read_buffer + 2 * sizeof(uint64_t), psize);
		}
		print_mut.unlock();
		
		taken.release();
		
		lost.erase(data_position);
		lost.erase(data_position - n);
		for (uint64_t i = max_position + 1; i < data_position; i++) {
			lost.insert(i);
		}
		max_position = std::max(max_position, data_position);
		
		for (auto i : lost) {
			std::cerr << "MISSING: BEFORE " << data_position << " EXPECTED " << i << "\n";
		}
		
		if (byte_num >= start_printing_byte && !printing.load()) {
			printing.store(true);
			cv.notify_one();
		}
	}
	
	return 0;
}
