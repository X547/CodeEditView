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


TEST_CASE("outline of shell functions")
{
	const std::string_view text =
		"f() {\n"
		"\ttrue\n"
		"}\n"
		"function g {\n"
		"\tfalse\n"
		"}\n";
	EditorCore core(MakeModel(text, "shell"));
	const std::vector<OutlineItem> outline = core.Outline();
	REQUIRE(outline.size() == 2);
	CHECK(outline[0].kind == OutlineKind::Function);
	CHECK(outline[0].label == "f()");
	CHECK(outline[0].nameStart == 0);
	CHECK(outline[0].nameEnd == 1);
	CHECK(outline[1].kind == OutlineKind::Function);
	CHECK(outline[1].label == "g()");
	CHECK(outline[1].nameStart == text.find("g {"));
	CHECK(outline[1].start == text.find("function"));
}


TEST_CASE("outline of jam rules and actions")
{
	const std::string_view text =
		"rule A x : y\n"
		"{\n"
		"}\n"
		"actions quietly A\n"
		"{\n"
		"\techo\n"
		"}\n"
		"rule B { }\n";
	EditorCore core(MakeModel(text, "jam"));
	const std::vector<OutlineItem> outline = core.Outline();
	REQUIRE(outline.size() == 3);
	CHECK(outline[0].kind == OutlineKind::Function);
	CHECK(outline[0].label == "A");
	CHECK(outline[0].nameStart == 5);
	CHECK(outline[0].nameEnd == 6);
	CHECK(outline[1].kind == OutlineKind::Function);
	CHECK(outline[1].label == "actions A");
	CHECK(outline[1].nameStart == text.find("A\n{\n\techo"));
	CHECK(outline[2].label == "B");
}


TEST_CASE("outline of a component pascal module")
{
	const std::string_view text =
		"MODULE M;\n"
		"\tTYPE\n"
		"\t\tView* = POINTER TO ABSTRACT RECORD END;\n"
		"\t\tDesc = RECORD x: INTEGER END;\n"
		"\t\tInt = INTEGER;\n"
		"\tPROCEDURE (v: View) Restore*, NEW, ABSTRACT;\n"
		"\tPROCEDURE Do*;\n"
		"\t\tPROCEDURE Local; END Local;\n"
		"\tEND Do;\n"
		"END M.\n";
	EditorCore core(MakeModel(text, "componentpascal"));
	const std::vector<OutlineItem> outline = core.Outline();
	REQUIRE(outline.size() == 1);
	const OutlineItem &module = outline[0];
	CHECK(module.kind == OutlineKind::Namespace);
	CHECK(module.label == "M");
	// Types other than records are not listed.
	REQUIRE(module.children.size() == 4);
	CHECK(module.children[0].kind == OutlineKind::Class);
	CHECK(module.children[0].label == "View");
	CHECK(module.children[0].nameStart == text.find("View*"));
	CHECK(module.children[1].kind == OutlineKind::Struct);
	CHECK(module.children[1].label == "Desc");
	CHECK(module.children[2].kind == OutlineKind::Function);
	CHECK(module.children[2].label == "View.Restore");
	CHECK(module.children[2].nameStart == text.find("Restore"));
	const OutlineItem &procedure = module.children[3];
	CHECK(procedure.kind == OutlineKind::Function);
	CHECK(procedure.label == "Do");
	REQUIRE(procedure.children.size() == 1);
	CHECK(procedure.children[0].label == "Local");
}


TEST_CASE("outline of python, cmake and make")
{
	EditorCore python(MakeModel(
		"class A(B):\n"
		"    def f(self, x):\n"
		"        pass\n"
		"def g():\n"
		"    pass\n",
		"python"
	));
	const std::vector<OutlineItem> pythonOutline = python.Outline();
	REQUIRE(pythonOutline.size() == 2);
	CHECK(pythonOutline[0].kind == OutlineKind::Class);
	CHECK(pythonOutline[0].label == "A");
	REQUIRE(pythonOutline[0].children.size() == 1);
	CHECK(pythonOutline[0].children[0].kind == OutlineKind::Function);
	CHECK(pythonOutline[0].children[0].label == "f(self, x)");
	CHECK(pythonOutline[1].label == "g()");

	const std::string_view cmakeText =
		"function(add_thing name)\n"
		"endfunction()\n"
		"macro(m)\n"
		"endmacro()\n";
	EditorCore cmake(MakeModel(cmakeText, "cmake"));
	const std::vector<OutlineItem> cmakeOutline = cmake.Outline();
	REQUIRE(cmakeOutline.size() == 2);
	CHECK(cmakeOutline[0].kind == OutlineKind::Function);
	CHECK(cmakeOutline[0].label == "add_thing");
	CHECK(cmakeOutline[0].nameStart == cmakeText.find("add_thing"));
	CHECK(cmakeOutline[1].label == "m");

	EditorCore make(MakeModel(
		"CC = gcc\n"
		"all: a b\n"
		"\tcc\n"
		"%.o: %.c\n"
		"\tcc\n"
		"define RULE\n"
		"x\n"
		"endef\n",
		"make"
	));
	const std::vector<OutlineItem> makeOutline = make.Outline();
	REQUIRE(makeOutline.size() == 3);
	CHECK(makeOutline[0].kind == OutlineKind::Function);
	CHECK(makeOutline[0].label == "all");
	CHECK(makeOutline[1].label == "%.o");
	CHECK(makeOutline[2].label == "RULE");
}


TEST_CASE("outline of a devicetree")
{
	const std::string_view text =
		"/dts-v1/;\n"
		"/ {\n"
		"  uart0: serial@1000 {\n"
		"  };\n"
		"};\n"
		"&uart0 {\n"
		"};\n";
	EditorCore core(MakeModel(text, "devicetree"));
	const std::vector<OutlineItem> outline = core.Outline();
	REQUIRE(outline.size() == 2);
	CHECK(outline[0].kind == OutlineKind::Struct);
	CHECK(outline[0].label == "/");
	REQUIRE(outline[0].children.size() == 1);
	CHECK(outline[0].children[0].label == "uart0: serial@1000");
	CHECK(outline[0].children[0].nameStart == text.find("serial"));
	CHECK(outline[1].label == "&uart0");
}


TEST_CASE("outline of javascript, typescript and css")
{
	const std::string_view javaScript =
		"class Foo {\n"
		"  bar(a, b) {}\n"
		"  baz = (x) => x;\n"
		"}\n"
		"function top() {}\n"
		"const arrow = y => y;\n"
		"let value = 1;\n";
	EditorCore javaScriptCore(MakeModel(javaScript, "javascript"));
	const std::vector<OutlineItem> javaScriptOutline = javaScriptCore.Outline();
	REQUIRE(javaScriptOutline.size() == 3);
	CHECK(javaScriptOutline[0].kind == OutlineKind::Class);
	CHECK(javaScriptOutline[0].label == "Foo");
	REQUIRE(javaScriptOutline[0].children.size() == 2);
	CHECK(javaScriptOutline[0].children[0].kind == OutlineKind::Function);
	CHECK(javaScriptOutline[0].children[0].label == "bar(a, b)");
	CHECK(javaScriptOutline[0].children[1].label == "baz(x)");
	CHECK(javaScriptOutline[1].label == "top()");
	CHECK(javaScriptOutline[2].kind == OutlineKind::Function);
	CHECK(javaScriptOutline[2].label == "arrow(y)");
	CHECK(javaScriptOutline[2].nameStart == javaScript.find("arrow"));

	EditorCore typeScriptCore(MakeModel(
		"interface Shape { area(): number; }\n"
		"type Id = string;\n"
		"enum Color { Red }\n"
		"namespace NS { export function f(): void {} }\n"
		"abstract class Base { abstract g(): void; }\n",
		"typescript"
	));
	const std::vector<OutlineItem> typeScriptOutline = typeScriptCore.Outline();
	REQUIRE(typeScriptOutline.size() == 5);
	CHECK(typeScriptOutline[0].kind == OutlineKind::Class);
	CHECK(typeScriptOutline[0].label == "Shape");
	CHECK(typeScriptOutline[1].kind == OutlineKind::Struct);
	CHECK(typeScriptOutline[1].label == "Id");
	CHECK(typeScriptOutline[2].kind == OutlineKind::Enum);
	CHECK(typeScriptOutline[3].kind == OutlineKind::Namespace);
	CHECK(typeScriptOutline[3].label == "NS");
	REQUIRE(typeScriptOutline[3].children.size() == 1);
	CHECK(typeScriptOutline[3].children[0].label == "f()");
	CHECK(typeScriptOutline[4].kind == OutlineKind::Class);
	CHECK(typeScriptOutline[4].label == "Base");

	const std::string_view css =
		"a, .b > c { color: red; }\n"
		"@media (max-width: 600px) {\n"
		"  #x { margin: 0; }\n"
		"}\n"
		"@keyframes spin { from { opacity: 0; } }\n";
	EditorCore cssCore(MakeModel(css, "css"));
	const std::vector<OutlineItem> cssOutline = cssCore.Outline();
	REQUIRE(cssOutline.size() == 3);
	CHECK(cssOutline[0].kind == OutlineKind::Struct);
	CHECK(cssOutline[0].label == "a, .b > c");
	CHECK(cssOutline[1].kind == OutlineKind::Namespace);
	CHECK(cssOutline[1].label == "@media (max-width: 600px)");
	CHECK(cssOutline[1].nameEnd == css.find(" {\n  #x"));
	REQUIRE(cssOutline[1].children.size() == 1);
	CHECK(cssOutline[1].children[0].label == "#x");
	CHECK(cssOutline[2].label == "@keyframes spin");
}


TEST_CASE("outline without a syntax tree")
{
	EditorCore core(MakeModel(kSample, nullptr));
	CHECK(core.Outline().empty());
}


} // CodeEdit
