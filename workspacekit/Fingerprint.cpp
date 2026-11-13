#include "Fingerprint.h"

#include <string.h>

#include <algorithm>
#include <bit>


namespace Workspace {


static constexpr uint64_t kSeed1 = 0x243f6a8885a308d3;
static constexpr uint64_t kSeed2 = 0x13198a2e03707344;
static constexpr uint64_t kMultiplier1 = 0x9e3779b97f4a7c15;
static constexpr uint64_t kMultiplier2 = 0xc2b2ae3d27d4eb4f;
static constexpr uint64_t kMultiplier3 = 0x165667b19e3779f9;


static uint64_t Mix(uint64_t value)
{
	value ^= value >> 33;
	value *= kMultiplier2;
	value ^= value >> 29;
	value *= kMultiplier3;
	value ^= value >> 32;
	return value;
}


FingerprintBuilder::FingerprintBuilder():
	fHash1(kSeed1),
	fHash2(kSeed2)
{
}


// Two lanes with different mixing, so that both must collide.
void FingerprintBuilder::AddWord(uint64_t word)
{
	fHash1 = (fHash1 ^ word) * kMultiplier1;
	fHash1 ^= fHash1 >> 32;
	fHash2 = std::rotl(fHash2 + word * kMultiplier2, 31) * kMultiplier3;
}


void FingerprintBuilder::Append(std::string_view data)
{
	fSize += data.size();
	const char *bytes = data.data();
	size_t size = data.size();
	if (fTailSize > 0) {
		const size_t take = std::min(size, sizeof(fTail) - fTailSize);
		memcpy(fTail + fTailSize, bytes, take);
		fTailSize += take;
		bytes += take;
		size -= take;
		if (fTailSize < sizeof(fTail)) {
			return;
		}
		uint64_t word;
		memcpy(&word, fTail, sizeof(word));
		AddWord(word);
		fTailSize = 0;
	}
	while (size >= sizeof(uint64_t)) {
		uint64_t word;
		memcpy(&word, bytes, sizeof(word));
		AddWord(word);
		bytes += sizeof(word);
		size -= sizeof(word);
	}
	memcpy(fTail, bytes, size);
	fTailSize = size;
}


Fingerprint FingerprintBuilder::Result() const
{
	uint64_t hash1 = fHash1;
	uint64_t hash2 = fHash2;
	uint64_t last = 0;
	memcpy(&last, fTail, fTailSize);
	hash1 = (hash1 ^ last ^ fTailSize) * kMultiplier1;
	hash2 = std::rotl(hash2 + (last ^ fSize) * kMultiplier2, 31) * kMultiplier3;
	return Fingerprint {fSize, Mix(hash1 ^ fSize), Mix(hash2 ^ hash1)};
}


Fingerprint FingerprintBuilder::Of(std::string_view data)
{
	FingerprintBuilder builder;
	builder.Append(data);
	return builder.Result();
}


} // Workspace
