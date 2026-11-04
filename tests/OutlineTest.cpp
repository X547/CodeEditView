#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <OS.h>

#include <CodeEditView/TextModel.h>

#include "view/EditorCore.h"


namespace CodeEdit {


static constexpr const char *kSample =
	"namespace ns {\n"
	"class Foo {\n"
	"public:\n"
	"\tint Get() const { return 1; }\n"
	"};\n"
	"}\n"
	"struct Point { int x; };\n"
	"static int *Make(int a,\n"
	"\tint b)\n"
	"{\n"
	"\treturn 0;\n"
	"}\n"
	"int &Ref() { static int r; return r; }\n"
	"void Foo::Set(int v) {}\n"
	"enum { A };\n";


static BReference<TextModel> MakeModel(std::string_view text, const char *language)
{
	BReference<TextModel> model(new TextModel(), true);
	uint64_t revision;
	{
		ModelLock lock(*model);
		model->SetText(text);
		model->SetLanguage(language);
		revision = model->SyntaxRevision();
	}
	if (language == nullptr) {
		return model;
	}
	for (int i = 0; i < 500; i++) {
		{
			ModelLock lock(*model);
			if (model->SyntaxRevision() > revision) {
				break;
			}
		}
		snooze(10000);
	}
	return model;
}


static size_t Find(std::string_view needle)
{
	return std::string_view(kSample).find(needle);
}


TEST_CASE("outline of definitions")
{
	EditorCore core(MakeModel(kSample, "cpp"));
	const std::vector<OutlineItem> outline = core.Outline();
	REQUIRE(outline.size() == 6);

	const OutlineItem &ns = outline[0];
	CHECK(ns.kind == OutlineKind::Namespace);
	CHECK(ns.label == "ns");
	REQUIRE(ns.children.size() == 1);
	const OutlineItem &foo = ns.children[0];
	CHECK(foo.kind == OutlineKind::Class);
	CHECK(foo.label == "Foo");
	CHECK(foo.nameStart == Find("Foo {"));
	CHECK(foo.nameEnd == Find("Foo {") + 3);
	REQUIRE(foo.children.size() == 1);
	CHECK(foo.children[0].kind == OutlineKind::Function);
	CHECK(foo.children[0].label == "Get()");

	CHECK(outline[1].kind == OutlineKind::Struct);
	CHECK(outline[1].label == "Point");
	CHECK(outline[1].children.empty());

	// Return type declarators and line breaks in parameter lists.
	CHECK(outline[2].kind == OutlineKind::Function);
	CHECK(outline[2].label == "Make(int a, int b)");
	CHECK(outline[2].start == Find("static int *Make"));
	CHECK(outline[2].nameStart == Find("Make("));
	CHECK(outline[3].label == "Ref()");

	CHECK(outline[4].label == "Foo::Set(int v)");
	CHECK(outline[4].nameStart == Find("Foo::Set"));
	CHECK(outline[5].kind == OutlineKind::Enum);
	CHECK(outline[5].label == "(anonymous enum)");
}


TEST_CASE("outline without a syntax tree")
{
	EditorCore core(MakeModel(kSample, nullptr));
	CHECK(core.Outline().empty());
}


} // CodeEdit
