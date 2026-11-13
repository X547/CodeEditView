#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string_view>


namespace Workspace {


// Tells data apart without keeping it: its size and a 128-bit hash. Not
// cryptographic; made to notice changes of a file.
struct Fingerprint {
	uint64_t size {};
	uint64_t hash1 {};
	uint64_t hash2 {};

	bool operator==(const Fingerprint &other) const = default;
};


// Computes the fingerprint of data given in pieces; the result does not
// depend on how the data is split.
class FingerprintBuilder {
private:
	uint64_t fSize {};
	uint64_t fHash1;
	uint64_t fHash2;
	// Bytes of an incomplete word.
	uint8_t fTail[8] {};
	size_t fTailSize {};

	void AddWord(uint64_t word);

public:
	FingerprintBuilder();

	void Append(std::string_view data);
	Fingerprint Result() const;

	static Fingerprint Of(std::string_view data);
};


} // Workspace
