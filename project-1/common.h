uint64_t string_to_int(const std::string &number) {
	uint64_t result = 0;
	
	for (size_t i = 0; i < number.size(); i++) {
		if (!('0' <= number[i] && number[i] <= '9')) {
			std::cerr << "Wrong character.\n";
			exit(1);
		}
		
		if (result > (UINT64_MAX - (number[i] - '0')) / 10) {
			std::cerr << "Too big number.\n";
			exit(1);
		}
		
		result = result * 10 + (number[i] - '0');
	}
	
	return result;
}
