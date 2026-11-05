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


static constexpr const char *kMarks =
	"#include <stdio.h>\n"
	"// #pragma mark -\n"
	"void A() {}\n"
	"//#pragma mark - First\n"
	"void B() {}\n"
	"void C() {}\n"
	"// #pragma mark -\n"
	"void D() {}\n"
	"#pragma mark -\n"
	"#pragma mark ----- Second -----\n"
	"static const int kTable[] = {\n"
	"\t// #pragma mark - InInitializer\n"
	"\t1\n"
	"};\n"
	"void E()\n"
	"{\n"
	"\t//#pragma mark - InFunction\n"
	"}\n"
	"namespace ns {\n"
	"//#pragma mark - Inner\n"
	"class K {\n"
	"//#pragma mark - Members\n"
	"\tvoid M() {}\n"
	"};\n"
	"}\n"
	"#ifdef X\n"
	"//#pragma mark - Branch\n"
	"void F() {}\n"
	"#endif\n"
	"void G() {}\n"
	"//#pragma mark Trailing\n"
	"//#pragma mark -\n";


static size_t FindIn(std::string_view text, std::string_view needle)
{
	return text.find(needle);
}


TEST_CASE("outline sections from pragma marks")
{
	EditorCore core(MakeModel(kMarks, "cpp"));
	const std::vector<OutlineItem> outline = core.Outline();
	const std::string_view text(kMarks);

	// A; First {B, C}; separator; D; Second {E, ns {Inner {K {Members {M}}}}};
	// Branch {F}; G; Trailing.
	REQUIRE(outline.size() == 8);
	CHECK(outline[0].label == "A()");

	const OutlineItem &first = outline[1];
	CHECK(first.kind == OutlineKind::Section);
	CHECK(first.label == "First");
	CHECK(first.start == FindIn(text, "//#pragma mark - First"));
	CHECK(first.end == FindIn(text, "// #pragma mark -\nvoid D"));
	CHECK(first.nameStart == FindIn(text, "First"));
	CHECK(first.nameEnd == FindIn(text, "First") + 5);
	REQUIRE(first.children.size() == 2);
	CHECK(first.children[0].label == "B()");
	CHECK(first.children[1].label == "C()");

	CHECK(outline[2].kind == OutlineKind::Separator);
	CHECK(outline[2].start == outline[2].end);
	CHECK(outline[2].children.empty());
	CHECK(outline[3].label == "D()");

	// The unnamed mark directly before "Second" is dropped; marks in an
	// initializer or a function body are ignored.
	const OutlineItem &second = outline[4];
	CHECK(second.kind == OutlineKind::Section);
	CHECK(second.label == "Second");
	CHECK(second.end == FindIn(text, "//#pragma mark - Branch"));
	REQUIRE(second.children.size() == 2);
	CHECK(second.children[0].label == "E()");
	CHECK(second.children[0].children.empty());

	const OutlineItem &ns = second.children[1];
	CHECK(ns.kind == OutlineKind::Namespace);
	REQUIRE(ns.children.size() == 1);
	CHECK(ns.children[0].kind == OutlineKind::Section);
	CHECK(ns.children[0].label == "Inner");
	CHECK(ns.children[0].end == ns.end);
	REQUIRE(ns.children[0].children.size() == 1);
	const OutlineItem &k = ns.children[0].children[0];
	CHECK(k.label == "K");
	REQUIRE(k.children.size() == 1);
	CHECK(k.children[0].kind == OutlineKind::Section);
	CHECK(k.children[0].label == "Members");
	REQUIRE(k.children[0].children.size() == 1);
	CHECK(k.children[0].children[0].label == "M()");

	// A section in a preprocessor branch ends with the branch.
	const OutlineItem &branch = outline[5];
	CHECK(branch.kind == OutlineKind::Section);
	CHECK(branch.label == "Branch");
	REQUIRE(branch.children.size() == 1);
	CHECK(branch.children[0].label == "F()");
	CHECK(outline[6].label == "G()");

	// The trailing unnamed mark has no items after it.
	CHECK(outline[7].kind == OutlineKind::Section);
	CHECK(outline[7].label == "Trailing");
	CHECK(outline[7].children.empty());
}


TEST_CASE("outline without a syntax tree")
{
	EditorCore core(MakeModel(kSample, nullptr));
	CHECK(core.Outline().empty());
}


} // CodeEdit
