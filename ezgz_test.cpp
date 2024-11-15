//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <string>
#include "ezgz.hpp"

template <int Size>
struct SettingsWithInputSize : EzGz::DefaultDecompressionSettings {
	constexpr static int inputBufferSize = Size;
};

template <int MaxSize, int MinSize>
struct SettingsWithOutputSize : EzGz::DefaultDecompressionSettings {
	constexpr static int maxOutputBufferSize = MaxSize;
	constexpr static int minOutputBufferSize = MinSize;
};

template <int Size>
struct InputHelper : EzGz::Detail::ByteInput<SettingsWithInputSize<Size>> {
	InputHelper(std::span<const uint8_t> source)
	: EzGz::Detail::ByteInput<SettingsWithInputSize<Size>>([source, position = 0] (std::span<uint8_t> toFill) mutable -> int {
		int filling = static_cast<int>(std::min(source.size() - position, toFill.size()));
//		std::cout << "Providing " << filling << " bytes of data, " << (source.size() - position - filling) << " left" << std::endl;
		if(filling != 0)
			memcpy(toFill.data(), &source[position], filling);
		position += filling;
		return filling;
	}) {}
};

int main(int, char**) {

	int errors = 0;
	int tests = 0;

	auto doATest = [&] (auto is, auto shouldBe) {
		tests++;
		if constexpr(std::is_floating_point_v<decltype(is)>) {
			if ((is > 0 && (is > shouldBe * 1.0001 || is < shouldBe * 0.9999)) ||
					(is < 0 && (is < shouldBe * 1.0001 || is > shouldBe * 0.9999))) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
			}
		} else {
			if (is != shouldBe) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
			}
		}
	};

	using namespace EzGz;
	using namespace Detail;

	{
		std::cout << "Testing chunking" << std::endl;
		constexpr static std::array<uint8_t, 5> data = { 0b10101010, 0b10101010, 0b10101010, 0b10101010, 0b10101010 };
		InputHelper<1> byteReader(data);
		{
			BitReader reader(&byteReader);
			auto readTwoBits = reader.getBits(2);
			doATest(readTwoBits, 0b10u);
			auto readFourteenBits = reader.getBits(14);
			doATest(readFourteenBits, 0b10101010101010u);
		}

		{
			unsigned int twoBytes = static_cast<unsigned int>(byteReader.getBytes(2));
			doATest(twoBytes, 0b1010101010101010u);
		}

		{
			BitReader reader(&byteReader);
			auto readFourBits = reader.getBits(4);
			doATest(readFourBits, 0b1010u);
			reader.peekAByteAndConsumeSome([&] (uint8_t byte) {
				doATest(int(byte), 0b00001010);
				return 4;
			});
		}
	}

	{
		std::cout << "Testing chunking 2" << std::endl;
		constexpr static std::array<uint8_t, 17> data = { 0b00000001, 0b0000010, 0b00000011, 0b00000100, 0b00000101, 0b00000110, 0b00000111,
				0b000001000, 0b00001001, 0b00001010, 0b00001011, 0b00001100, 0b00001101, 0b00001110, 0b00001111, 0b00010000, 0b00010001 };
		InputHelper<3> reader(data);
		{
			auto readThreeBytes = reader.getRange(3);
			doATest(int(readThreeBytes.size()), 3);
		}
		{
			int read = 0;
			constexpr int tryingToRead = 10;
			while (read < tryingToRead) {
				auto readTenBytes = reader.getRange(tryingToRead - read);
				doATest(readTenBytes.size() < tryingToRead, true);
				read += static_cast<int>(readTenBytes.size());
			}
			doATest(read, tryingToRead);
		}

		{
			auto twoBytes = reader.getBytes(2);
			doATest(twoBytes, 0b0000111100001110u);
		}
	}

	{
		std::cout << "Testing ByteInput simple" << std::endl;
		constexpr static std::array<uint8_t, 5> data = { 0b10101010, 0b10101010, 0b10101010, 0b10101010, 0b10101010 };
		InputHelper<5> byteReader(data);
		BitReader reader(&byteReader);
		{
			auto readTwoBits = reader.getBits(2);
			doATest(readTwoBits, 0b10u);
			auto fourMoreBits = reader.getBits(4);
			doATest(fourMoreBits, 0b1010u);
		}

		{
			auto readSevenBits = reader.getBits(7);
			doATest(readSevenBits, 0b0101010u);
		}

		{
			auto readSomeBits = reader.getBits(10);
			doATest(readSomeBits, 0b0101010101u);
			auto readMoreBits = reader.getBits(13);
			doATest(readMoreBits, 0b1010101010101u);
		}
	}

	{
		std::cout << "Testing ByteInput forward" << std::endl;
		constexpr static std::array<uint8_t, 5> data = { 0b10011001, 0b10011001, 0b11110000, 0b11110000, 0b10000001 };
		InputHelper<32> byteReader(data);
		BitReader reader(&byteReader);
		doATest(reader.getBits(2), 0b01u);
		doATest(reader.getBits(7), 0b1100110u);
		doATest(reader.getBits(13), 0b1100001001100u);
	}

	{
		std::cout << "Testing ByteInput reading integers" << std::endl;
		constexpr static std::array<uint8_t, 6> data = { 0b10011001, 0x35, 0x25, 0xa8, 0xb3, 0xc7 };
		InputHelper<32> byteReader(data);
		{
			BitReader reader(&byteReader);
			doATest(reader.getBits(3), 0b001u);
		}
		doATest(byteReader.getBytes(2), 0x2535u); // TODO: Rename to getInteger()
		doATest(byteReader.getBytes(3), 0xc7b3a8u);
	}

	{
		std::cout << "Testing ByteInput reading byte ranges" << std::endl;
		constexpr static std::array<uint8_t, 6> data = { 'f', 'd', 'p', 'v', 'g', 'r' };
		InputHelper<2> reader(data);
		{
			BitReader bitReader(&reader);
			doATest(bitReader.getBits(6), 0b100110u);
		}
		std::span<const uint8_t> range1 = reader.getRange(2); // TODO: Rename to getByteRange()
		doATest(range1[0], 'd');
		doATest(range1[1], 'p');
		doATest(range1.size(), 2u);
		std::span<const uint8_t> range2 = reader.getRange(3);
		doATest(range2[0], 'v');
		doATest(range2[1], 'g');
		doATest(range2[2], 'r');
		doATest(range2.size(), 3u);
	}

	{
		std::cout << "Testing EncodedTable" << std::endl;
		constexpr static std::array<uint8_t, 47> data = { 0b00011101, 0b11001010, 0b10110001, 0b00001101, 0b00000000,
				0b00110000,	0b00001000, 0b00000011, 0b11000001, 0b00111110, 0b01010011, 0b11000000, 0b00101000,
				0b10101110, 0b01001100, 0b11111101, 0b00001101, 0b11111011, 0b01101111, 0b00010010, 0b01000000,
				0b01101110, 0b10101100, 0b11010011, 0b10000011, 0b10111010, 0b00011011, 0b10110000, 0b10101011,
				0b00111100, 0b11001011, 0b01000000, 0b00011010, 0b00100011, 0b11000011, 0b11100110, 0b00011110, 0b10101011,
				0b10011110, 0b01000110, 0b11011010, 0b10110000, 0b00001110, 0b11011110, 0b00000111, 0b00001111, 0b00011000 };
		constexpr int readerSize = 5;
		InputHelper<readerSize> byteReader(data);
		byteReader.getBytes(8);

		BitReader reader(&byteReader);
		reader.getBits(7);

		std::array<uint8_t, codeCodingReorder.size()> codeCodingLengths = {3, 4, 4, 3, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3};
		std::array<uint8_t, 256> codeCodingLookup = {4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4,
				5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4,
				17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4,
				5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4,
				17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4,
				5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4,
				17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4, 5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2, 4,
				5, 0, 18, 4, 17, 3, 1, 4, 5, 0, 18, 4, 17, 3, 2};
		EncodedTable<288, decltype(reader)> table(reader, 260, codeCodingLookup, codeCodingLengths);
		reader.getBits(29);

		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'W');
		doATest(table.readWord(), 'W');
		doATest(table.readWord(), 'W');
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'G');
		doATest(table.readWord(), 'G');
		doATest(table.readWord(), 'H');
		doATest(table.readWord(), 'H');
		doATest(table.readWord(), 'G');
		doATest(table.readWord(), 257);
		reader.getBits(3);
		doATest(table.readWord(), '!');
		doATest(table.readWord(), ' ');
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 257);
		reader.getBits(4);
		doATest(table.readWord(), 'R');
		doATest(table.readWord(), '!');
	}

	{
		std::cout << "Testing ByteOutput" << std::endl;
		constexpr std::string_view shouldBe("What a disaaaasteeeeer! Hahahaha!");
		auto inspectStart = [&doATest, shouldBe, position = 0] (std::span<const char> reading) mutable -> int {
			std::string_view correctPart = shouldBe.substr(position, reading.size());
			doATest(std::string_view(reading.data(), reading.size()), correctPart);
			position += static_cast<int>(reading.size());
			return static_cast<int>(reading.size());
		};

		{
			ByteOutput<SettingsWithOutputSize<4, 2>> output = {};
			auto inspect = inspectStart;
			output.addByte('W');
			output.addByte('h');
			output.addByte('a');
			inspect(output.consume());
			output.addByte('t');
			inspect(output.consume());
			output.addByte(' ');
			output.addByte('a');
			inspect(output.consume());
			output.addByte(' ');
			output.addByte('d');
			inspect(output.consume());
			output.addByte('i');
			output.addByte('s');
			inspect(output.consume());
			output.addByte('a');
			output.addByte('a');
			inspect(output.consume());
			output.repeatSequence(2, 2);
			inspect(output.consume());
			output.addByte('s');
			inspect(output.consume());
			output.addByte('t');
			output.addByte('e');
			inspect(output.consume());
			output.repeatSequence(2, 1);
			inspect(output.consume());
			output.repeatSequence(2, 1);
			inspect(output.consume());
			output.addByte('r');
			output.addByte('!');
			inspect(output.consume());
			output.addByte(' ');
			output.addByte('H');
			inspect(output.consume());
			output.addByte('a');
			output.addByte('h');
			inspect(output.consume());
			output.repeatSequence(2, 2);
			inspect(output.consume());
			output.repeatSequence(2, 2);
			inspect(output.consume());
			output.addByte('a');
			output.addByte('!');
			inspect(output.consume());
			output.done();
			doATest(output.available() > 0, true);
			inspect(output.consume());
		}
		{
			ByteOutput<SettingsWithOutputSize<8, 3>> output = {};
			auto inspect = inspectStart;
			output.addByte('W');
			output.addByte('h');
			output.addByte('a');
			output.addByte('t');
			output.addByte(' ');
			output.addByte('a');
			inspect(output.consume(4));
			output.addByte(' ');
			output.addByte('d');
			inspect(output.consume(5));
		}
	}

	{
		std::cout << "Testing EncodedTable with a long word" << std::endl;
		constexpr static std::array<uint8_t, 9> data = { 0b10110111, 0b00111001, 0b00100001, 0b11111101, 0b11111111, 0b10101000, 0b00000000, 0b000001000 };
		// The above means length of A is 1 (first 64 zeroes with 111 0110110, then 0 encoding length 1), length of Q is 13 (15 zeroes with 111 0000100,
		// then 100 encoding length 13), length of R is 14 (101 encoding length 14), then (111 1111111 111 0101000 to encode 187 zeroes),
		// ending with encoded RAAA (100000000000001 0 0 0)

		// The above means length of A is 1 (first 64 zeroes with 111 0110110, then 0 encoding length 1), length of R is 15 (16 zeroes with 111 0000101,
		// then 10 encoding length 15), then (111 1111111 111 0101000 to encode 187 zeroes), ending with encoded RAAA (100000000000001 0 0 0)
		InputHelper<200> byteReader(data);
		BitReader reader(&byteReader);

		std::array<uint8_t, codeCodingReorder.size()> codeCodingLengths = {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 3, 3};
		std::array<uint8_t, 256> codeCodingLookup = {1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1,
				18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18,
				1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1,
				13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13,
				1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17,
				1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14,
				1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18,
				1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18, 1, 13, 1, 17, 1, 14, 1, 18};
		EncodedTable<288, decltype(reader)> table(reader, 270, codeCodingLookup, codeCodingLengths);

		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'A');
	}

	{
		std::cout << "Testing Deflate literal" << std::endl;
		constexpr static std::array<uint8_t, 23> data = { 0x01, 0x12, 0x00, 0xed, 0xff, 0xc4, 0x8d, 0xc3, 0xb3,
				0xc5, 0xa1, 0xc3, 0xa9, 0xc5, 0x88, 0xc3, 0xa1, 0xc4, 0x8f, 0xc3, 0xb4, 0xc5, 0xbe };
		std::vector<char> output = readDeflateIntoVector(data);
		std::string_view outputStr(reinterpret_cast<const char*>(output.data()), output.size());
		doATest(outputStr, "čóšéňáďôž");
	}

	{
		std::cout << "Testing Deflate fixed" << std::endl;
		constexpr static std::array<uint8_t, 11> data = { 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0xb9, 0x00 };
		std::vector<char> output = readDeflateIntoVector(data);
		std::string_view outputStr(reinterpret_cast<const char*>(output.data()), output.size());
		doATest(outputStr, "hello hello hello hello\n");
	}

	{
		std::cout << "Testing Deflate dynamic" << std::endl;
		constexpr static std::array<uint8_t, 23> data = { 0x1d, 0xc6, 0x49, 0x01, 0x00, 0x00, 0x10, 0x40, 0xc0, 0xac,
				0xa3, 0x7f, 0x88, 0x3d, 0x3c, 0x20, 0x2a, 0x97, 0x9d, 0x37, 0x5e, 0x1d, 0x0c };
		std::vector<char> output = readDeflateIntoVector(data);
		std::string_view outputStr(output.data(), output.size());
		doATest(outputStr, "abaabbbabaababbaababaaaabaaabbbbbaa");
	}

	{
		std::cout << "Testing crc32" << std::endl;
		constexpr static std::array<uint8_t, 6> data = { 'J', 'e', 'd', 'e', 'n', ' '};
		constexpr static std::array<uint8_t, 7> data2 = { 'z', 'e', 'm', 'i', 'a', 'k', '!' };
		LightCrc32 crc = {};
		doATest(crc(data), 1956347882u);
		doATest(crc(data2), 916168997u);
	}

	{
		std::cout << "Testing Gz file parsing" << std::endl;
		constexpr static std::array<uint8_t, 53> data = { 0x1f, 0x8b, 0x08, 0x08, 0x82, 0x52, 0xc7, 0x62, 0x00, 0x03, 0x68, 0x65,
				0x6c, 0x6c, 0x6f, 0x20, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x68, 0x65, 0x6c,
				0x6c, 0x6f, 0x00, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0xb9, 0x00, 0x00, 0x88, 0x59, 0x0b, 0x18,
				0x00, 0x00, 0x00};
		IGzFile file(data);

		IGzFileInfo info = file.info();
		doATest(int(info.operatingSystem), int(CreatingOperatingSystem::UNIX_BASED));
		doATest(info.fastestCompression, false);
		doATest(info.densestCompression, false);
		doATest(info.name, "hello hello hello hello");
		doATest(info.comment, "");
		doATest(info.probablyText, false);
		doATest(info.extraData.has_value(), false);

		std::vector<char> decompressed = file.readAll();
		std::string_view decompressedStr(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
		doATest(decompressedStr, "hello hello hello hello\n");
	}

	{
		constexpr static std::array<uint8_t, 42> data = { 0x1f, 0x8b, 0x08, 0x08, 0xd5, 0x99, 0x5b, 0x63, 0x00, 0x03, 0x6d, 0x75,
				0x6c, 0x74, 0x69, 0x6c, 0x69, 0x6e, 0x65, 0x00, 0x4b, 0xe4, 0x4a, 0x4c, 0xe2, 0x4a, 0xe4, 0x02, 0xe2, 0x44, 0x2e,
				0x20, 0x0d, 0x00, 0xaf, 0xa7, 0xd4, 0x0f, 0x0f, 0x00, 0x00, 0x00};

		{
			std::cout << "Testing getline" << std::endl;
			IGzFile file(data);
			int linesParsed = 0;
			constexpr static std::array<std::string_view, 8> linesExpected = { "a", "ab", "a", "b", "aa", "", "a", "" };
			file.readByLines([&] (std::span<const char> line) mutable {
				doATest(std::string_view(line.data(), line.size()), linesExpected.at(linesParsed));
				linesParsed++;
			});
			doATest(linesParsed, std::ssize(linesExpected));
		}

		{
			std::cout << "Testing stream" << std::endl;
			IGzStream file(data);
			constexpr static std::array<std::string_view, 8> linesExpected = { "a", "ab", "a", "b", "aa", "", "a", "" };
			for (int i = 0; i < std::ssize(linesExpected); i++) {
				std::string line;
				std::getline(file, line, '\n');
				doATest(file.eof(), (i == std::ssize(linesExpected) - 1));
				doATest(line, linesExpected.at(i));
			}
		}
	}

	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;
	return 0;
}
