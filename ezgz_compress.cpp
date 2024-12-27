//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include "ezgz.hpp"
#include <filesystem>
#include <chrono>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

// For testing purposes only

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cout << "Usage: " << argv[0] << " name_of_file_to_compress" << std::endl;
		return 1;
	}

	std::string inputName = argv[1];
	ssize_t inputSize = std::filesystem::file_size(inputName);

	std::chrono::microseconds duration = {};
	{
		EzGz::BasicOGzStream<EzGz::DefaultCompressionSettings> compressor = {EzGz::GzFileInfo<std::string>(inputName)};
		compressor.exceptions(std::ifstream::failbit);
		std::ifstream input(inputName, std::ios::binary);
		input.exceptions(std::ifstream::failbit);

		std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
		compressor << input.rdbuf();
		input.close();
		std::chrono::steady_clock::time_point endTime = std::chrono::steady_clock::now();
		duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
	}

	ssize_t outputSize = std::filesystem::file_size(inputName + ".gz");
	std::cout << "Compression ratio was " << (float(outputSize) / inputSize * 100) << "%" << std::endl;
    std::cout << "Compressed the data to size " << outputSize << " bytes at speed " << ((float(inputSize) / (1024 * 1024)) / (float(duration.count()) / 1000000)) << " MiB/s" << std::endl;

}
