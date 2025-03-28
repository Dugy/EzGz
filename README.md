# EzGz
A single header library for easily and quickly decompressing Gz archives, written in modern C++. It's designed to be both easy to use and highly performant.

## Installation
Just add `ezgz.hpp` into your project, it contains all the functionality and depends only on the C++20 standard library (it will also run on C++17, but with some compromises). You can use git subtree to get updates cleanly.

## Usage
### Decompression
The easiest way of using this is to use the `IGzStream`. It inherits from `std::istream`, so it's usable as any other C++ input stream:
```C++
EzGz::IGzStream input("data.gz");
std::string line;
while (std::getline(input, line)) { // You can read it by lines for example
	std::cout << line << std::endl;
}
```

It can also be constructed from a `std::istream` to read data from, `std::span<const uint8_t>` holding raw data or a `std::function<int(std::span<uint8_t> batch)>` that fills the span in its argument with data and returns how many bytes it wrote. All constructors accept an optional argument that determines the number of bytes reachable through `unget()` (10 by default).

If you don't want to use a standard stream, you can use `IGzFile`, which gives a slightly lower level approach:
```C++
EzGz::IGzFile<> input(data); // Expecting the file's contents is already a contiguous container
while (std::optional<std::span<const char>> chunk = input.readSome()) {
	output.put(*chunk);
}
```

It supports some other ways of reading the data (the separator is newline by default, other separators can be set as second argument):
```C++
EzGz::IGzFile<> input([&file] (std::span<uint8_t> batch) -> int {
	// Fast reading from stream
	file.read(reinterpret_cast<char*>(batch.data()), batch.size());
	return input.gcount();
});
data.readByLines([&] (std::span<const char> chunk) {
	std::cout << chunk << std::endl;
});
```

Or simply:
```C++
std::vector<char> decompressed = EzGz::IGzFile<>("data.gz").readAll();
```

If the data is only deflate-compressed and not in an archive, you should use `IDeflateFile` instead of `IGzFile`. But in that case, it will most likely be already in some buffer, in which case, it's more convenient to do this:
```C++
std::vector<char> decompressed = EzGz::readDeflateIntoVector(data);
```
The function has an overload that accepts a functor that fill buffers with input data and returns the amount of data filled.

#### Configuration
Most classes and free functions accept a template argument whose values allow tuning some properties:
* `maxOutputBufferSize` - maximum number of bytes in the output buffer, if filled, decompression will stop to empty it
* `minOutputBufferSize` - must be at least 32768 for correct decompression, decompression may fail if smaller but can save some memory
* `inutBufferSize` - the input buffer's size, decides how often is the function to fill more data called
* `verifyChecksum` - boolean whether to verify the checksum after parsing the file
* `Checksum` - a class that computers the CRC32 checksum, 3 are available:
  * `NoChecksum` - does nothing, can save some time if checksum isn't checked or isn't known
  * `LightCrc32` - uses a 1 kiB table (precomputed at compile time), slow on modern CPUs
  * `FastCrc32` - uses a 16 kiB table (precomputed at compile time), works well with out of order execution
* StringType - type of string to save file name and comment into (must be default constructible, convertible to `std::string_view` and support the `+=` operator for `char`), `std::string` by default

You can either declare your own struct or inherit from a default one and adjust only what you want:
```C++
struct Settings : EzGz::DefaultDecompressionSettings {
	constexpr static int inputBufferSize = 30000;
	using Checksum = EzGz::NoChecksum;
	constexpr static bool verifyChecksum = false;
}; // This will skip checksum
std::vector<char> decompressed = EzGz::IGzFile<Settings>("data.gz").readAll();
```

If including `fstream` is undesirable, the `EZGZ_NO_FILE` macro can be defined to remove the constructors that accept file names. This does not restrict usability much.

### Compression (experimental)
The implementation of compression has worse ratios than zlib but can be faster. Working on improvements. It can still provide some utility.

You can use it this way:
```C++
EzGz::BasicOGzStream<EzGz::DefaultCompressionSettings> compressor = {EzGz::GzFileInfo<std::string>(inputName)};
compressor.exceptions(std::ifstream::failbit);
std::ifstream input(inputName, std::ios::binary);
input.exceptions(std::ifstream::failbit);

compressor << input.rdbuf();
input.close();
```

For smaller archives, using a function can be simpler (assuming there is a `std::vector` or `std::span` of `char` or `const char` named `data`):

```C++
std::vector<char> compressed = EzGz::writeDeflateIntoVector<DefaultCompressionSettings>(data);
```

It is configurable to some extent, but the details may be changed completely in a future version. `EzGz::DefaultCompressionSettings` can be replaced by other presets I will try to keep in future versions:

* `FastCompressionSettings` - very fast
* `DefaultCompressionSettings` - better compression ratio, relatively fast
* `DenseCompressionSettings` - relatively good compression ratio, slower
* `BestCompressionSettings` - best compression ratio of EzGz, slow (seems to suffer from a bug)

Compared to zlib, only `DenseCompressionSettings` have better compression ratio than its fastest settings but is slower. No compression settings have comparable ratios comparable to the denser settings of zlib.

Note: under some settings, the buffers may be too large to fit on stack, in which case it's necessary to to dynamically allocate the compressor object. `writeDeflateIntoVector` does this automatically because `std::vector` does plenty of dynamic allocation already.

## Performance
Decompression is about 30% faster than with `zlib`. Decompression speeds over 250 MiB/s are reachable on modern CPUs. It was tested on the standard Silesia Corpus file, compressed for minimum size.

Using it through `std::ostream` has no noticeable impact on performance but any type of parsing will impact it significantly.

`FastCompressionSettings` has a bad compression ratio but is significantly faster than zlib. Better compression ratios are generally slower than zlib.

## Code remarks
The type used to represent bytes of compressed data is `uint8_t`. The type to represent bytes of uncompressed data is `char`. Some casting is necessary, but it usually makes it clear which data are compressed which aren't.

`I` is the prefix for _input,_ `O` is the prefix for _output._

Errors are handled with exceptions. Unless there is a bug, an error happens only if the input file is incorrect. All exceptions inherit from `std::exception`, parsing errors are `std::runtime_error`, internal errors with `std::logic_error` (these should not appear unless there is a bug). In absence of RTTI, catching an exception almost certainly means the file is corrupted (if compiling with exceptions disabled, exceptions have to be enabled for the file that includes this header, performance would be worse without them). Exceptions thrown during decompression mean the entire output may be invalid (checksum failures are detected only at the end of file). If an exception is thrown inside a function that fills an input buffer, it will be propagated.

The decompression algorithm itself does not use dynamic allocation. All buffers and indexes are on stack. Exceptions, `std::string` values obtained from the files (like names) and callbacks done using `std::function` may dynamically allocate. The string type can be configured using a custom class as settings template argument.
