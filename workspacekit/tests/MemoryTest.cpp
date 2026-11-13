// For malloc_usable_size().
#define _DEFAULT_SOURCE

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <new>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "Document.h"
#include "TestSupport.h"


// Heap use of the test program, as far as it goes through operator new.
// Blocks are plain malloc() blocks, so that a block freed by a library with
// free() or allocated there with malloc() does no harm.
static std::atomic<int64_t> sHeapCurrent;
static std::atomic<int64_t> sHeapPeak;


static void CountAllocation(int64_t size)
{
	const int64_t current = sHeapCurrent.fetch_add(size) + size;
	int64_t peak = sHeapPeak.load();
	while (current > peak && !sHeapPeak.compare_exchange_weak(peak, current)) {
	}
}


void *operator new(size_t size)
{
	void *block = malloc(size > 0 ? size : 1);
	if (block == nullptr) {
		throw std::bad_alloc();
	}
	CountAllocation(static_cast<int64_t>(malloc_usable_size(block)));
	return block;
}


void operator delete(void *pointer) noexcept
{
	if (pointer == nullptr) {
		return;
	}
	sHeapCurrent.fetch_sub(static_cast<int64_t>(malloc_usable_size(pointer)));
	free(pointer);
}


void operator delete(void *pointer, size_t size) noexcept
{
	(void)size;
	operator delete(pointer);
}


namespace Workspace {


// Heap bytes in use, and the most in use since the last call.
static int64_t HeapNow()
{
	sHeapPeak.store(sHeapCurrent.load());
	return sHeapCurrent.load();
}


static int64_t HeapPeak()
{
	return sHeapPeak.load();
}


static std::string Lines(size_t size, char variant)
{
	std::string data;
	data.reserve(size + 64);
	for (size_t line = 0; data.size() < size; line++) {
		data += "line " + std::to_string(line) + " of the text " + variant + "\n";
	}
	return data;
}


TEST_CASE("documents hold one copy of their text")
{
	static constexpr size_t kSize = 8 * 1024 * 1024;
	// Bytes of the text in the model: the rope's leaves and nodes.
	static constexpr double kRopeFactor = 1.1;
	// Buffers and such independent of the size.
	static constexpr int64_t kSlack = 1024 * 1024;

	TempDirectory directory;
	directory.Write("big.txt", Lines(kSize, 'a'));
	DocumentRegistry registry;

	int64_t base = HeapNow();
	BReference<Document> document = registry.Open(Locator::File(directory / "big.txt"));
	registry.WaitIdle();
	REQUIRE(document->Status().state == DocumentState::Ready);
	const int64_t loadPeak = HeapPeak() - base;
	const int64_t loaded = HeapNow() - base;
	CHECK(loaded < kSize * kRopeFactor);
	CHECK(loadPeak < kSize * kRopeFactor + kSlack);

	SECTION("reloading unchanged data reads no copy") {
		base = HeapNow();
		registry.Reload(document);
		registry.WaitIdle();
		CHECK(HeapPeak() - base < kSlack);
	}

	SECTION("saving writes without a copy") {
		{
			CodeEdit::ModelLock lock(*document->Model());
			document->Model()->Insert(0, "edited\n");
		}
		base = HeapNow();
		registry.Save(document.Get());
		registry.WaitIdle();
		CHECK(HeapPeak() - base < kSlack);
		CHECK_FALSE(document->IsModified());
	}

	SECTION("reloading changed data holds two copies only while replacing") {
		directory.Write("big.txt", Lines(kSize, 'b'));
		base = HeapNow();
		registry.Reload(document);
		registry.WaitIdle();
		CHECK(HeapPeak() - base < kSize * kRopeFactor + kSlack);
		CHECK(HeapNow() - base < kSlack);
	}
}


} // Workspace
