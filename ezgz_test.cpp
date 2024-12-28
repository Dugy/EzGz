//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <string>
#include "ezgz.hpp"

#if EZGZ_HAS_CPP20
constexpr std::string_view CppVersion = "C++20";
#else
constexpr std::string_view CppVersion = "C++17";
#endif


template <int MaximumSize, int MinimumSize = 0, int LookAheadSize = sizeof(uint32_t)>
struct SettingsWithInputSize : EzGz::DefaultDecompressionSettings {
	using Checksum = EzGz::NoChecksum;
	struct Input : DefaultDecompressionSettings::Input {
		constexpr static int maxSize = MaximumSize;
		constexpr static int minSize = MinimumSize;
		constexpr static int lookAheadSize = LookAheadSize;
	};
};

template <int MaxSize, int MinSize>
struct TestStreamSettings {
	constexpr static int maxSize = MaxSize;
	constexpr static int minSize = MinSize;
};

template <int MaxSize, int MinSize>
struct SettingsWithOutputSize : EzGz::DefaultDecompressionSettings {
	using Output = TestStreamSettings<MaxSize, MinSize>;
};

template <int MaxSize, int MinSize = 0, int LookAheadSize = sizeof(uint32_t)>
struct InputHelper : EzGz::Detail::ByteInputWithBuffer<typename SettingsWithInputSize<MaxSize, MinSize, LookAheadSize>::Input, typename SettingsWithInputSize<MaxSize, MinSize, LookAheadSize>::Checksum> {
	InputHelper(std::span<const uint8_t> source)
	: EzGz::Detail::ByteInputWithBuffer<typename SettingsWithInputSize<MaxSize, MinSize, LookAheadSize>::Input, typename SettingsWithInputSize<MaxSize, MinSize, LookAheadSize>::Checksum>(
				[source, position = 0] (std::span<uint8_t> toFill) mutable -> int {
		int filling = int(std::min(source.size() - position, toFill.size()));
//		std::cout << "Providing " << filling << " bytes of data, " << (source.size() - position - filling) << " left" << std::endl;
		if(filling != 0)
			memcpy(toFill.data(), &source[position], filling);
		position += filling;
		return filling;
	}) {}
};

template <typename Settings>
struct DeduplicationVerifier {
	std::string parsed;
	int duplicationsFound = 0;
	bool done = false;

	int consume(typename EzGz::Detail::DeduplicatedStream<Settings>::Section section) {
		while (!section.atEnd()) {
			int word = 0;
			typename EzGz::Detail::DeduplicatedStream<Settings>::Section::CodeRemainderWithLength lengthRemainder;
			int distanceWord = 0;
			typename EzGz::Detail::DeduplicatedStream<Settings>::Section::CodeRemainderWithLength distanceRemainder;
			word = section.readWord([&] (auto lengthRemainderCopy, int distanceWordCopy, auto distanceRemainderCopy) {
				lengthRemainder = lengthRemainderCopy;
				distanceWord = distanceWordCopy;
				distanceRemainder = distanceRemainderCopy;
			});
			if (word < 256) {
				parsed.push_back(char(word));
//				std::cout << "Got " << char(word) << std::endl;
			} else if (word > 256) {
				duplicationsFound++;
				int length = (word <= 264) ? (word - 254) : lengthRemainder.remainder + EzGz::Detail::lengthOffsets[word - 257];
				int distance = (distanceWord >= -4) ? (-distanceWord) : EzGz::Detail::distanceOffsets[-1 - distanceWord] + distanceRemainder.remainder;
				std::string appended;
				for (int i = 0; i < length; i++) {
					appended.push_back(parsed[parsed.size() - distance]);
					parsed.push_back(parsed[parsed.size() - distance]);
				}
//				std::cout << "Got " << appended << " from " << distance << " behind" << std::endl;
			} else {
				done = true;
				return section.position;
			}
		}
		return section.position;
	}
	auto reader() {
		return [this] (typename EzGz::Detail::DeduplicatedStream<Settings>::Section section, bool) {
			return consume(section);
		};
	}
};

int main(int, char**) {

	std::cout << "EzGz Tests (compiled for " << CppVersion << ")" << std::endl;
	std::cout << "===============================" << std::endl;


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
		std::cout << "Testing basic input" << std::endl;
		constexpr static std::array<uint8_t, 5> data = { 'a', 'b', 'c', 'd', 'e' };
		InputHelper<1> byteReader(data);
		doATest(byteReader.getInteger<char>(), 'a');
		auto nextTwoBytes = byteReader.getRange(2);
		doATest(nextTwoBytes.size() != 0u, true);
		doATest(nextTwoBytes[0], 'b');
		if (nextTwoBytes.size() == 2u) {
			doATest(nextTwoBytes[1], 'c');
		} else {
			auto nextOneByte = byteReader.getRange(1);
			doATest(nextOneByte.size(), 1u);
			doATest(nextOneByte[0], 'c');
		}
		byteReader.getBytes(2);
		byteReader.returnBytes(1);
		doATest(byteReader.getInteger<char>(), 'e');
	}
	{
		std::cout << "Testing basic input 2" << std::endl;
		constexpr static std::array<uint8_t, 10> data = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j' };

		InputHelper<4, 3, 1> byteReader(data);
		doATest(byteReader.getInteger<char>(), 'a');
		doATest(byteReader.getAtPosition(byteReader.getPosition() - 1), 'a');
		doATest(byteReader.getAtPosition(byteReader.getPosition()), 'b');
		doATest(byteReader.availableAhead() >= 1, true);

		int consumed = 0;
		while (consumed < 3) {
			consumed += int(byteReader.getRange(3 - consumed).size());
		}
		doATest(byteReader.getPosition() + byteReader.getPositionStart(), 4);
		for (int i = 1; i <= 4; i++) {
			doATest(byteReader.getAtPosition(byteReader.getPosition() + i - 4), data[i]);
		}
		doATest(byteReader.availableAhead() >= 1, true);

		doATest(byteReader.getInteger<char>(), 'e');
		consumed = 0;
		while (consumed < 3) {
			consumed += int(byteReader.getRange(3 - consumed).size());
		}
		for (int i = 5; i <= 8; i++) {
			doATest(byteReader.getAtPosition(byteReader.getPosition() + i - 8), data[i]);
		}
		doATest(byteReader.availableAhead() >= 1, true);
		doATest(byteReader.getInteger<char>(), 'i');
		doATest(byteReader.availableAhead(), 1);
		doATest(byteReader.getInteger<char>(), 'j');
		doATest(byteReader.availableAhead(), 0);
	}

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
			unsigned int twoBytes = unsigned(byteReader.getBytes(2));
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
				read += int(readTenBytes.size());
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
		std::cout << "Testing DeduplicatedStream" << std::endl;
		int duplicationsFound = 0;
		auto checker = [&, step = 0] (Detail::DeduplicatedStream<TestStreamSettings<6, 2>>::Section section, bool isLast) mutable {
			auto shouldntFindDup = [&] (auto, int, auto) {doATest("IsDuplication", "Shouldn't be duplication"); };
			do {
				if (step == 0) doATest(section.readWord(shouldntFindDup), 'a');
				else if (step == 1) doATest(section.readWord(shouldntFindDup), 'b');
				else if (step == 2) doATest(section.readWord(shouldntFindDup), 'c');
				else if (step == 3) doATest(section.readWord([&] (auto, int distanceWord, auto) {
					doATest(distanceWord, -2);
					duplicationsFound++;
				}), 257);
				else if (step == 4) doATest(section.readWord(shouldntFindDup), 'd');
				else if (step == 5) doATest(section.readWord([&] (auto, int distanceWord, auto) {
					doATest(distanceWord, -3);
					duplicationsFound++;
				}), 258);
				else if (step == 6)
					break;
				step++;
			} while (isLast || section.position < 2);
			return section.position;
		};
		{
			Detail::DeduplicatedStream<TestStreamSettings<6, 2>> stream(checker);
			stream.addByte('a');
			stream.addByte('b');
			stream.addByte('c');
			stream.addDuplication(3, 2);
			stream.addByte('d');
			stream.addDuplication(4, 3);
		}
		doATest(duplicationsFound, 2);
	}

	{
		std::cout << "Testing DeduplicatedStream 2" << std::endl;
		int duplicationsFound = 0;
		auto checker = [&, step = 0] (Detail::DeduplicatedStream<TestStreamSettings<10, 4>>::Section section, bool) mutable {
			while (!section.atEnd()) {
				if (step == 0) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.length, 0);
					doATest(distanceWord, -3);
					doATest(distanceRemainder.length, 0);
					duplicationsFound++;
				}), 263);
				else if (step == 1) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.length, 1);
					doATest(lengthRemainder.remainder, 1);
					doATest(distanceWord, -6);
					doATest(distanceRemainder.remainder, 1);
					doATest(distanceRemainder.length, 1);
					duplicationsFound++;
				}), 265);
				else if (step == 2) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.remainder, 1);
					doATest(lengthRemainder.length, 2);
					doATest(distanceWord, -9);
					doATest(distanceRemainder.remainder, 2);
					doATest(distanceRemainder.length, 3);
					duplicationsFound++;
				}), 270);
				else if (step == 3) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.remainder, 6);
					doATest(lengthRemainder.length, 4);
					doATest(distanceWord, -23);
					doATest(distanceRemainder.remainder, 256);
					doATest(distanceRemainder.length, 10);
					duplicationsFound++;
				}), 279);
				else if (step == 4) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.length, 0);
					doATest(distanceWord, -30);
					doATest(distanceRemainder.remainder, 8191);
					doATest(distanceRemainder.length, 13);
					duplicationsFound++;
				}), 285);
				else if (step == 5)
					break;
				step++;
			}
			return section.position;
		};
		{
			Detail::DeduplicatedStream<TestStreamSettings<10, 4>> stream(checker);
			stream.addDuplication(9, 3);
			stream.addDuplication(12, 8);
			stream.addDuplication(24, 19);
			stream.addDuplication(105, 2305);
			stream.addDuplication(258, 32768);
		}
		doATest(duplicationsFound, 5);
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
		EncodedTable<288> table(reader, 260, codeCodingLookup, codeCodingLengths);
		reader.getBits(15);
		reader.getBits(14);

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
			position += int(reading.size());
			return int(reading.size());
		};

		{
			ByteOutput<SettingsWithOutputSize<4, 2>::Output, NoChecksum> output = {};
			auto inspect = inspectStart;
			output.addByte('W');
			output.addByte('h');
			output.addByte('a');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('t');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte(' ');
			output.addByte('a');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte(' ');
			output.addByte('d');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('i');
			output.addByte('s');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('a');
			output.addByte('a');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.repeatSequence(2, 2);
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('s');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('t');
			output.addByte('e');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.repeatSequence(2, 1);
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.repeatSequence(2, 1);
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('r');
			output.addByte('!');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte(' ');
			output.addByte('H');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('a');
			output.addByte('h');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.repeatSequence(2, 2);
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.repeatSequence(2, 2);
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.addByte('a');
			output.addByte('!');
			inspect(output.getBuffer());
			output.cleanBuffer();
			output.done();
			doATest(output.available() > 0, true);
			inspect(output.getBuffer());
			output.cleanBuffer();
		}
		{
			ByteOutput<SettingsWithOutputSize<8, 3>::Output, NoChecksum> output = {};
			auto inspect = inspectStart;
			output.addByte('W');
			output.addByte('h');
			output.addByte('a');
			output.addByte('t');
			output.addByte(' ');
			output.addByte('a');
			inspect(output.getBuffer());
			output.cleanBuffer(4);
			output.addByte(' ');
			output.addByte('d');
			inspect(output.getBuffer());
			output.cleanBuffer(5);
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
		EncodedTable<288> table(reader, 270, codeCodingLookup, codeCodingLengths);

		doATest(table.readWord(), 'R');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'A');
		doATest(table.readWord(), 'A');
	}

	{
		std::cout << "Testing Deduplicator" << std::endl;
		std::string input = "hello hello hello hello\n";
		InputHelper<50, 20, 10> byteReader(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(input.c_str()), input.size()));
		int duplicationsFound = 0;
		auto checker = [&, step = 0] (Detail::DeduplicatedStream<TestStreamSettings<10, 4>>::Section section, bool) mutable {
			auto shouldntFindDup = [&] (auto, int, auto) {doATest("IsDuplication", "Shouldn't be duplication"); };
			while (!section.atEnd()) {
				if (step == 0) doATest(section.readWord(shouldntFindDup), 'h');
				else if (step == 1) doATest(section.readWord(shouldntFindDup), 'e');
				else if (step == 2) doATest(section.readWord(shouldntFindDup), 'l');
				else if (step == 3) doATest(section.readWord(shouldntFindDup), 'l');
				else if (step == 4) doATest(section.readWord(shouldntFindDup), 'o');
				else if (step == 5) doATest(section.readWord(shouldntFindDup), ' ');
				else if (step == 6) doATest(section.readWord([&] (auto lengthRemainder, int distanceWord, auto distanceRemainder) {
					doATest(lengthRemainder.remainder, 0);
					doATest(lengthRemainder.length, 1);
					doATest(distanceWord, -5);
					doATest(distanceRemainder.remainder, 1);
					doATest(distanceRemainder.length, 1);
					duplicationsFound++;
				}), 268);
				else if (step == 7) doATest(section.readWord(shouldntFindDup), '\n');
				else {
					doATest("More than 7 calls", "7 calls");
					break;
				}
				step++;
			}
			return section.position;
		};
		{
			Detail::DeduplicatedStream<TestStreamSettings<10, 4>> output(checker);
			Detail::Deduplicator<TestStreamSettings<10, 4>, Detail::PrefixBasedDuplicationIndex<Detail::RepetitionCircularBuffer<>>, INCLUDE_SMALL_DUPLICATES> deduplicator(byteReader, output);
			deduplicator.deduplicateSome();
		}
	}

	{
		std::cout << "Testing Deduplicator 2" << std::endl;
		std::string input = "abaabbbabaababbaababaaaabaaabbbbbaa";
		InputHelper<40, 22, 0> byteReader(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(input.c_str()), input.size()));
		DeduplicationVerifier<TestStreamSettings<12, 5>> verifier;
		{
			Detail::DeduplicatedStream<TestStreamSettings<12, 5>> output(verifier.reader());
			Detail::Deduplicator<TestStreamSettings<12, 5>, Detail::PrefixBasedDuplicationIndex<Detail::RepetitionCircularBuffer<>>, INCLUDE_SMALL_DUPLICATES> deduplicator(byteReader, output);
			deduplicator.deduplicateSome();
		}
		doATest(verifier.parsed, "abaabbbabaababbaababaaaabaaabbbbbaa");
		doATest(verifier.duplicationsFound >= 6, true); // Unreliable because hash collisions are purposefully not addressed
	}

	{
		std::cout << "Testing Deduplicator 3" << std::endl;
		std::string input = "The main interesting thing about it is the deflate algorithm.";
		InputHelper<70, 20, 0> byteReader(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(input.c_str()), input.size()));
		DeduplicationVerifier<TestStreamSettings<10, 4>> verifier;
		{
			Detail::DeduplicatedStream<TestStreamSettings<10, 4>> output(verifier.reader());
			Detail::Deduplicator<TestStreamSettings<10, 4>, Detail::PrefixBasedDuplicationIndex<Detail::RepetitionCircularBuffer<>>, INCLUDE_SMALL_DUPLICATES> deduplicator(byteReader, output);
			deduplicator.deduplicateSome();
		}
		doATest(verifier.parsed, "The main interesting thing about it is the deflate algorithm.");
		doATest(verifier.duplicationsFound > 0, true);
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
		std::cout << "Testing Huffman compression fixed simple" << std::endl;
		Detail::ByteOutput<TestStreamSettings<20, 8>, NoChecksum> output;
		{
			Detail::HuffmanWriter<TestStreamSettings<20, 8>, TestStreamSettings<20, 8>> writer(output);
			auto reader = [&] (typename Detail::DeduplicatedStream<TestStreamSettings<20, 8>>::Section section, bool lastCall) {
				writer.writeBatch(section, lastCall);
				return section.position;
			};
			{
				Detail::DeduplicatedStream<TestStreamSettings<20, 8>> stream(reader);
				stream.addByte('a');
				stream.addByte('b');
				stream.addByte('c');
				stream.addByte('d');
				stream.addByte('\n');
			}
		}
		output.done();
		std::span<const char> obtained = output.getBuffer();
		doATest(obtained.size(), 7u);
		doATest(int(uint8_t(obtained[0])), 0x4b);
		doATest(int(uint8_t(obtained[1])), 0x4c);
		doATest(int(uint8_t(obtained[2])), 0x4a);
		doATest(int(uint8_t(obtained[3])), 0x4e);
		doATest(int(uint8_t(obtained[4])), 0xe1);
		doATest(int(uint8_t(obtained[5])), 0x02);
		doATest(int(uint8_t(obtained[6])), 0x00);
	}

	{
		std::cout << "Testing Huffman compression fixed repetition" << std::endl;
		Detail::ByteOutput<TestStreamSettings<20, 8>, NoChecksum> output;
		{
			Detail::HuffmanWriter<TestStreamSettings<20, 8>, TestStreamSettings<30, 13>> writer(output);
			auto reader = [&] (typename Detail::DeduplicatedStream<TestStreamSettings<30, 13>>::Section section, bool lastCall) {
				writer.writeBatch(section, lastCall);
				return section.position;
			};
			{
				Detail::DeduplicatedStream<TestStreamSettings<30, 13>> stream(reader);
				stream.addByte('h');
				stream.addByte('e');
				stream.addByte('l');
				stream.addByte('l');
				stream.addByte('o');
				stream.addByte(' ');
				stream.addByte('h');
				stream.addDuplication(16, 6);
				stream.addByte('\n');
			}
		}
		output.done();
		std::span<const char> obtained = output.getBuffer();
		doATest(std::ssize(obtained), 11);
		doATest(int(uint8_t(obtained[0])), 0xcb);
		doATest(int(uint8_t(obtained[1])), 0x48);
		doATest(int(uint8_t(obtained[2])), 0xcd);
		doATest(int(uint8_t(obtained[3])), 0xc9);
		doATest(int(uint8_t(obtained[4])), 0xc9);
		doATest(int(uint8_t(obtained[5])), 0x57);
		doATest(int(uint8_t(obtained[6])), 0xc8);
		doATest(int(uint8_t(obtained[7])), 0x40);
		doATest(int(uint8_t(obtained[8])), 0x27);
		doATest(int(uint8_t(obtained[9])), 0xb9);
		doATest(int(uint8_t(obtained[10])), 0x00);
	}

	{
		std::cout << "Testing Huffman compression dynamic" << std::endl;
		Detail::ByteOutput<TestStreamSettings<80, 35>, NoChecksum> output;
		{
			Detail::HuffmanWriter<TestStreamSettings<80, 35>, TestStreamSettings<80, 35>> writer(output);
			auto reader = [&] (typename Detail::DeduplicatedStream<TestStreamSettings<80, 35>>::Section section, bool lastCall) {
				writer.writeBatch(section, lastCall);
				return section.position;
			};
			{
				Detail::DeduplicatedStream<TestStreamSettings<80, 35>> stream(reader);
				stream.addByte('a');
				stream.addByte('b');
				stream.addByte('a');
				stream.addByte('a');
				stream.addByte('b');
				stream.addByte('b');
				stream.addByte('b');
				stream.addByte('a');
				stream.addDuplication(4, 7);
				stream.addDuplication(3, 9);
				stream.addDuplication(5, 6);
				stream.addByte('a');
				stream.addByte('a');
				stream.addByte('a');
				stream.addDuplication(5, 5);
				stream.addByte('b');
				stream.addDuplication(4, 1);
				stream.addByte('a');
				stream.addByte('a');
			}
		}
		output.done();
		std::span<const char> obtained = output.getBuffer();
		// TODO: Assert the obtained part is shorter than if it was compressed using static compression
		std::vector<char> decompressed = readDeflateIntoVector(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(obtained.data()), obtained.size()));
		std::string_view decompressedStr(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
		doATest(decompressedStr, "abaabbbabaababbaababaaaabaaabbbbbaa");
	}

	{
		std::cout << "Testing Huffman compression together" << std::endl;
		std::string_view text = "BAACCEACAAAEBAACEABAEDEACEAACAAECCAADAEAACAEADAA";
		std::vector<uint8_t> compressed = writeDeflateIntoVector<DefaultCompressionSettings>(text);
		std::vector<char> decompressed = readDeflateIntoVector(compressed);
		std::string_view decompressedStr(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
		doATest(decompressedStr, "BAACCEACAAAEBAACEABAEDEACEAACAAECCAADAEAACAEADAA");
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

		GzFileInfo info = file.info();
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

	{
		std::cout << "Testing Gz file writing" << std::endl;
		std::vector<uint8_t> compressed = {};
		std::function<void(std::span<const char> batch)> consumeFunction = [&] (std::span<const char> batch) {
			compressed.insert(compressed.end(), batch.begin(), batch.end());
		};
		GzFileInfo<std::string> info = {"secret"};
		OGzFile<DefaultCompressionSettings, std::string> compressor(info, consumeFunction);
		compressor.writeSome(std::string_view("Hahahahahaha!\n"));
		compressor.writeSome(std::string_view("Mwahahahahaha!"));
		compressor.flush();
		IGzFile reading(compressed);
		reading.readByLines([&, count = 0] (std::span<const char> line) mutable {
			std::string_view lineText = {line.data(), line.size()};
			if (count == 0) {
				doATest(lineText, "Hahahahahaha!");
			} else if (count == 1) {
				doATest(lineText, "Mwahahahahaha!");
			} else doATest("Is here", "Shouldn't get here");
			count++;
		});
	}

	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;
	return errors != 0;
}
