#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Fingerprint.h"


namespace Workspace {


TEST_CASE("fingerprints do not depend on how data is split")
{
	std::string data;
	for (int i = 0; i < 1000; i++) {
		data += static_cast<char>(i * 7 + i / 13);
	}
	const Fingerprint whole = FingerprintBuilder::Of(data);
	CHECK(whole.size == data.size());
	for (const size_t pieceSize : {1, 3, 7, 8, 9, 64, 999}) {
		FingerprintBuilder builder;
		for (size_t pos = 0; pos < data.size(); pos += pieceSize) {
			builder.Append(std::string_view(data).substr(pos, pieceSize));
		}
		CHECK(builder.Result() == whole);
	}
}


TEST_CASE("fingerprints tell data apart")
{
	CHECK(FingerprintBuilder::Of("") == FingerprintBuilder().Result());
	CHECK(FingerprintBuilder::Of("") != FingerprintBuilder::Of(std::string(1, '\0')));
	CHECK(FingerprintBuilder::Of("abc") != FingerprintBuilder::Of("abd"));
	CHECK(FingerprintBuilder::Of("abcdefgh") != FingerprintBuilder::Of(std::string("abcdefgh\0", 9)));
	CHECK(FingerprintBuilder::Of("12345678abcdefgh") != FingerprintBuilder::Of("abcdefgh12345678"));
}


} // Workspace
