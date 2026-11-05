#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "TextSide.h"


namespace DiffKit {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


TEST_CASE("text sides")
{
	TextSide side("x.c");
	CHECK_FALSE(side.Exists());
	side.Load(std::string("a\r\nb\r\n"), false);
	CHECK(side.Exists());
	CHECK(side.Problem().empty());
	CHECK(Text(*side.Model()) == "a\nb\n");
	CHECK(side.SavedData() == "a\r\nb\r\n");

	// Edits are kept when the data changes, unless forced.
	{
		ModelLock lock(*side.Model());
		side.Model()->Insert(0, "0\n");
	}
	CHECK(side.IsModified());
	side.Load(std::string("a\r\nb\r\n"), false);
	CHECK_FALSE(side.ChangedOnDisk());
	side.Load(std::string("other\n"), false);
	CHECK(side.ChangedOnDisk());
	CHECK(Text(*side.Model()) == "0\na\nb\n");

	const std::string saved = side.SavedData();
	side.MarkSaved(saved);
	CHECK_FALSE(side.IsModified());
	CHECK_FALSE(side.ChangedOnDisk());
	// What was saved is not a change on disk.
	side.Load(saved, false);
	CHECK(Text(*side.Model()) == "0\na\nb\n");

	side.Load(std::nullopt, true);
	CHECK_FALSE(side.Exists());
	CHECK(Text(*side.Model()).empty());

	side.Load(std::string("x\r\ny\n"), true);
	CHECK(side.Problem() == "has mixed line endings");
	side.Load(std::string("a\0b", 3), true);
	CHECK(side.Problem() == "is a binary file");
	side.Load(std::string("fine\n"), true);
	CHECK(side.Problem().empty());
}


} // DiffKit
