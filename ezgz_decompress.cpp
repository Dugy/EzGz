//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include "ezgz.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>

// For testing purposes only, gunzip is faster because it can write into files much more efficiently

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cout << "Usage: " << argv[0] << " name_of_file_to_decompress" << std::endl;
		return 1;
	}

	std::string inputName = argv[1];
	if (inputName.find(".gz") == std::string::npos) {
		std::cout << "File name must contain .gz" << std::endl;
		return 2;
	}
	ssize_t inputSize = std::filesystem::file_size(inputName);
	std::string outputName = inputName.substr(0, inputName.find(".gz"));

	EzGz::IGzStream decompressor(inputName);
	decompressor.exceptions(std::ifstream::failbit);
	std::ofstream output(outputName, std::ios::binary);
	output.exceptions(std::ifstream::failbit);

	std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
	output << decompressor.rdbuf();
	output.close();
	std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
	std::chrono::microseconds duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

	ssize_t outputSize = std::filesystem::file_size(outputName);
	std::cout << "Compression ratio was " << (float(inputSize) / outputSize * 100) << "%" << std::endl;
	std::cout << "Decompressed " << outputSize << " bytes at speed " << ((float(outputSize) / (1024 * 1024)) / (float(duration.count()) / 1000000)) << " MiB/s" << std::endl;

}
