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
	"void f()\n"        // 0
	"{\n"               // 1
	"\tint a = 1;\n"    // 2
	"\tif (a) {\n"      // 3
	"\t\ta++;\n"        // 4
	"\t}\n"             // 5
	"}\n"               // 6
	"int g() { return 0; }\n"; // 7, then empty line 8


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


static size_t LineStart(TextModel &model, size_t line)
{
	ModelLock lock(model);
	return model.Text().LineStart(line);
}


static size_t LineEnd(TextModel &model, size_t line)
{
	ModelLock lock(model);
	return model.Text().LineEnd(line);
}


static size_t CaretLine(EditorCore &core)
{
	return core.CaretLocation().line;
}


TEST_CASE("revealing a folded line and mark changes")
{
	BReference<TextModel> model = MakeModel(kSample, nullptr);
	EditorCore core(model);
	core.Fold({1, 5});
	CHECK(core.RowCount() == 5);
	// Line 4 is hidden; revealing it unfolds.
	CHECK(core.RevealLine(4) == 4);
	CHECK(core.Folds().empty());
	CHECK(core.RevealLine(100) == 8);

	core.TakeDirty();
	{
		ModelLock lock(*model);
		model->SharedMarks().AddMark(kMarkExecLine, 3);
	}
	REQUIRE(core.Marks(0, 8).size() == 1);
	CHECK(core.Marks(0, 8).front().line == 3);
	const DirtyRows dirty = core.TakeDirty();
	CHECK(dirty.marksChanged);
	CHECK(dirty.first == 0);
	CHECK(dirty.last == SIZE_MAX);
}


TEST_CASE("fold candidates from the syntax tree")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	const std::vector<FoldRange> candidates = core.FoldCandidates(0, 8);
	REQUIRE(candidates.size() == 2);
	CHECK(candidates[0] == FoldRange {1, 5});
	CHECK(candidates[1] == FoldRange {3, 4});
	CHECK(core.FoldCandidates(3, 3).size() == 1);

	CHECK(core.FoldMarkers(0, 8) == std::vector<FoldMarker> {{1, false}, {3, false}});
	core.ToggleFoldAtLine(3);
	CHECK(core.FoldMarkers(0, 8) == std::vector<FoldMarker> {{1, false}, {3, true}});
	core.ToggleFoldAtLine(3);
	CHECK(core.Folds().empty());
	core.ToggleFoldAtLine(2);
	CHECK(core.Folds().empty());
}


TEST_CASE("sticky scopes of the top of the view")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	CHECK(core.EnclosingScopes(4, 5) == std::vector<FoldRange> {{1, 5}, {3, 4}});
	CHECK(core.EnclosingScopes(4, 1) == std::vector<FoldRange> {{1, 5}});
	CHECK(core.EnclosingScopes(1, 5).empty());

	// The function's bracket is on its own line: its line before is shown.
	CHECK(core.StickyScopes(0, 5).empty());
	CHECK(core.StickyScopes(2, 5) == std::vector<FoldRange> {{0, 5}});
	CHECK(core.StickyScopes(3, 5) == std::vector<FoldRange> {{0, 5}, {3, 4}});
	CHECK(core.StickyScopes(3, 1) == std::vector<FoldRange> {{0, 5}});
	// The if block would take the row of its closing line: it is over.
	CHECK(core.StickyScopes(4, 5) == std::vector<FoldRange> {{0, 5}});
	CHECK(core.StickyScopes(6, 5).empty());
	CHECK(core.StickyScopes(2, 0).empty());
}


TEST_CASE("a new syntax tree redraws where the syntax changed")
{
	std::string text;
	for (int i = 0; i < 100; i++) {
		text += "int a" + std::to_string(i) + ";\n";
	}
	BReference<TextModel> model = MakeModel(text, "cpp");
	EditorCore core(model);
	core.TakeDirty();

	uint64_t revision;
	{
		ModelLock lock(*model);
		revision = model->SyntaxRevision();
		model->Insert(model->Text().LineStart(50), "x");
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
	{
		ModelLock lock(*model);
		REQUIRE(model->SyntaxRevision() == revision + 1);
	}
	core.Sync();
	const DirtyRows dirty = core.TakeDirty();
	CHECK(dirty.contentsChanged);
	REQUIRE_FALSE(dirty.IsEmpty());
	CHECK(dirty.first >= 45);
	CHECK(dirty.last <= 55);
}


TEST_CASE("sticky lines name scopes written over several lines")
{
	BReference<TextModel> model = MakeModel(
		"status_t\n"                // 0
		"reserve(int a,\n"          // 1
		"\tint b)\n"                // 2
		"{\n"                       // 3
		"\tif (a\n"                 // 4
		"\t\t&& b)\n"               // 5
		"\t{\n"                     // 6
		"\t\tx();\n"                // 7
		"\t\ty();\n"                // 8
		"\t}\n"                     // 9
		"\treturn 0;\n"             // 10
		"}\n",                      // 11
		"cpp"
	);
	EditorCore core(model);
	// The function by its declarator, not its return type or its last
	// parameters; the if statement from its start.
	CHECK(core.StickyScopes(5, 5) == std::vector<FoldRange> {{1, 10}});
	CHECK(core.StickyScopes(7, 5) == std::vector<FoldRange> {{1, 10}, {4, 8}});
}


TEST_CASE("fold candidates of other languages")
{
	// Closing keywords on their own line stay visible.
	BReference<TextModel> shell = MakeModel(
		"f() {\n"
		"\tif true; then\n"
		"\t\techo a\n"
		"\tfi\n"
		"}\n",
		"shell"
	);
	EditorCore shellCore(shell);
	CHECK(shellCore.FoldCandidates(0, 5) == std::vector<FoldRange> {{0, 3}, {1, 2}});

	BReference<TextModel> meson = MakeModel(
		"foreach x : y\n"
		"\tif x\n"
		"\t\tmessage(x)\n"
		"\tendif\n"
		"endforeach\n",
		"meson"
	);
	EditorCore mesonCore(meson);
	CHECK(mesonCore.FoldCandidates(0, 5) == std::vector<FoldRange> {{0, 3}, {1, 2}});

	// Rule invocations over several lines fold up to the closing ";".
	BReference<TextModel> jam = MakeModel(
		"rule F\n"
		"{\n"
		"\tLib x :\n"
		"\t\ta.c\n"
		"\t\t;\n"
		"}\n",
		"jam"
	);
	EditorCore jamCore(jam);
	CHECK(jamCore.FoldCandidates(0, 6) == std::vector<FoldRange> {{1, 4}, {2, 3}});

	// "END Name" stays visible.
	BReference<TextModel> componentPascal = MakeModel(
		"MODULE M;\n"
		"\tPROCEDURE P;\n"
		"\tBEGIN\n"
		"\t\tIF x THEN\n"
		"\t\t\ty := 1\n"
		"\t\tEND\n"
		"\tEND P;\n"
		"END M.\n",
		"componentpascal"
	);
	EditorCore componentPascalCore(componentPascal);
	CHECK(componentPascalCore.FoldCandidates(0, 8) == std::vector<FoldRange> {{1, 5}, {3, 4}});

	// The closing quotes of a docstring stay visible.
	BReference<TextModel> python = MakeModel(
		"class A:\n"
		"    def f(self):\n"
		"        \"\"\"Doc\n"
		"        more\n"
		"        \"\"\"\n"
		"        return 1\n",
		"python"
	);
	EditorCore pythonCore(python);
	CHECK(pythonCore.FoldCandidates(0, 6) == std::vector<FoldRange> {{0, 5}, {1, 5}, {2, 3}});

	// endif() and endfunction() stay visible.
	BReference<TextModel> cmake = MakeModel(
		"function(f)\n"
		"  if(X)\n"
		"    message(a\n"
		"      b)\n"
		"  endif()\n"
		"endfunction()\n",
		"cmake"
	);
	EditorCore cmakeCore(cmake);
	CHECK(cmakeCore.FoldCandidates(0, 6) == std::vector<FoldRange> {{0, 4}, {1, 3}, {2, 3}});

	BReference<TextModel> make = MakeModel(
		"ifdef X\n"
		"Y = 1\n"
		"endif\n"
		"all: a\n"
		"\tcc a\n"
		"\tcc b\n",
		"make"
	);
	EditorCore makeCore(make);
	CHECK(makeCore.FoldCandidates(0, 6) == std::vector<FoldRange> {{0, 1}, {3, 5}});

	// The "};" closing a node stays visible.
	BReference<TextModel> deviceTree = MakeModel(
		"/ {\n"
		"  a {\n"
		"    x = <1>;\n"
		"  };\n"
		"};\n",
		"devicetree"
	);
	EditorCore deviceTreeCore(deviceTree);
	CHECK(deviceTreeCore.FoldCandidates(0, 5) == std::vector<FoldRange> {{0, 3}, {1, 2}});
}


TEST_CASE("folded lines have no rows")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	CHECK(core.RowCount() == 9);
	core.Fold({3, 4});
	CHECK(core.RowCount() == 8);
	CHECK(core.DisplayRows(4, 1).front().line == 5);
	CHECK(core.PositionOfOffset(LineStart(*model, 5)).row == 4);

	core.Fold({1, 5});
	CHECK(core.RowCount() == 5);
	CHECK(core.DisplayRows(2, 1).front().line == 6);
	core.UnfoldAll();
	CHECK(core.RowCount() == 9);
}


TEST_CASE("caret movement around folds")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	core.Fold({3, 4});

	core.SetSelection({LineStart(*model, 5) + 1, LineStart(*model, 5) + 1});
	core.Move(Movement::PreviousLine, false, {});
	CHECK(CaretLine(core) == 3);
	core.Move(Movement::NextLine, false, {});
	CHECK(CaretLine(core) == 5);
	CHECK(core.Folds().size() == 1);

	core.SetSelection({LineEnd(*model, 3), LineEnd(*model, 3)});
	core.Move(Movement::CharacterRight, false, {});
	CHECK(core.GetSelection().caret == LineStart(*model, 5));
	core.Move(Movement::CharacterLeft, false, {});
	CHECK(core.GetSelection().caret == LineEnd(*model, 3));
	core.Move(Movement::CharacterRight, true, {});
	CHECK(core.GetSelection() == Selection {LineEnd(*model, 3), LineStart(*model, 5)});
	CHECK(core.Folds().size() == 1);

	// Placing the caret inside the fold unfolds it.
	core.SetSelection({LineStart(*model, 4), LineStart(*model, 4)});
	CHECK(core.Folds().empty());
	CHECK(core.RowCount() == 9);
}


TEST_CASE("folds and the selection follow many edits at once")
{
	std::string text;
	for (int i = 0; i < 200; i++) {
		text += "x" + std::to_string(i) + "\n";
	}
	BReference<TextModel> model = MakeModel(text, nullptr);
	EditorCore core(model);
	core.Fold({100, 110});
	// Inside line 150, after its first character.
	const size_t caret = LineStart(*model, 150) + 1;
	core.SetSelection({caret, caret});
	const size_t rows = core.RowCount();
	{
		// One undo step of many edits, as replace all makes it; the hidden
		// lines stay unchanged (editing them unfolds).
		ModelLock lock(*model);
		model->BeginGroup(kNoObserver, EditKind::Other, {0, 0});
		for (size_t line = 200; line-- > 0;) {
			if (line <= 100 || line > 110) {
				model->Insert(model->Text().LineStart(line), "y");
			}
		}
		model->EndGroup({0, 0});
	}
	CHECK(core.Folds() == std::vector<FoldRange> {{100, 110}});
	CHECK(core.RowCount() == rows);
	CHECK(core.GetSelection() == Selection {caret + 141, caret + 141});
}


TEST_CASE("folds follow edits")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	EditorCore other(model);
	core.Fold({3, 4});

	other.SetSelection({0, 0});
	other.InsertText("// x\n");
	CHECK(core.Folds() == std::vector<FoldRange> {{4, 5}});
	CHECK(core.RowCount() == 9);

	other.SetSelection({LineEnd(*model, 4), LineEnd(*model, 4)});
	other.InsertText(" // header");
	CHECK(core.Folds() == std::vector<FoldRange> {{4, 5}});

	other.SetSelection({LineStart(*model, 7), LineStart(*model, 7)});
	other.InsertText("\n");
	CHECK(core.Folds() == std::vector<FoldRange> {{4, 5}});

	// Joining the header with the first hidden line removes the fold.
	other.SetSelection({LineEnd(*model, 4), LineEnd(*model, 4)});
	other.Delete(Deletion::CharacterRight);
	CHECK(core.Folds().empty());
	CHECK(core.RowCount() == other.RowCount());
}


TEST_CASE("toggle fold at the caret and manual folds")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);

	core.SetSelection({LineStart(*model, 2) + 2, LineStart(*model, 2) + 2});
	core.ToggleFold();
	CHECK(core.Folds() == std::vector<FoldRange> {{1, 5}});
	CHECK(core.GetSelection().caret == LineEnd(*model, 1));
	core.ToggleFold();
	CHECK(core.Folds().empty());

	core.SetSelection({LineStart(*model, 3), LineStart(*model, 3)});
	core.ToggleFold();
	CHECK(core.Folds() == std::vector<FoldRange> {{3, 4}});

	BReference<TextModel> plain = MakeModel("a\nb\nc\nd\ne\n", nullptr);
	EditorCore plainCore(plain);
	plainCore.SetSelection({LineStart(*plain, 1), LineStart(*plain, 4)});
	plainCore.ToggleFold();
	CHECK(plainCore.Folds() == std::vector<FoldRange> {{1, 3}});
	CHECK(plainCore.RowCount() == 4);
	plainCore.SetSelection({0, 0});
	plainCore.ToggleFold();
	CHECK(plainCore.Folds().size() == 1);
}


TEST_CASE("host folds")
{
	BReference<TextModel> model = MakeModel("a\nb\nc\nd\ne\nf\ng\nh\n", nullptr);
	EditorCore core(model);
	core.Fold({4, 6});

	// Not over the caret, not on the header of a user fold.
	core.SetSelection({LineStart(*model, 2), LineStart(*model, 2)});
	core.SetHostFolds({{0, 2}, {4, 5}, {6, 7}}, false);
	CHECK(core.Folds() == std::vector<FoldRange> {
		{4, 6, FoldSource::User},
		{6, 7, FoldSource::Host},
	});
	// The region over the caret is still a candidate.
	CHECK(core.FoldMarkers(0, 0) == std::vector<FoldMarker> {{0, false}});

	core.SetSelection({0, 0});
	core.SetHostFolds({{0, 2}}, false);
	CHECK(core.Folds() == std::vector<FoldRange> {
		{0, 2, FoldSource::Host},
		{4, 6, FoldSource::User},
	});
	CHECK(core.RowCount() == 5);

	// The user unfolds and folds them again like other folds.
	core.ToggleFoldAtLine(0);
	CHECK(core.Folds() == std::vector<FoldRange> {{4, 6}});
	CHECK(core.FoldMarkers(0, 0) == std::vector<FoldMarker> {{0, false}});
	core.ToggleFoldAtLine(0);
	CHECK(core.Folds() == std::vector<FoldRange> {
		{0, 2, FoldSource::Host},
		{4, 6, FoldSource::User},
	});

	// Unfolded regions keep their markers; edits move them.
	core.SetHostFolds({{1, 3, false}}, false);
	CHECK(core.Folds() == std::vector<FoldRange> {{4, 6}});
	{
		ModelLock lock(*model);
		model->Insert(0, "x\n");
	}
	CHECK(core.FoldMarkers(0, 3) == std::vector<FoldMarker> {{2, false}});

	// Folding at the caret moves it to the header line.
	core.SetSelection({LineStart(*model, 3), LineStart(*model, 3)});
	core.SetHostFolds({{2, 4}}, true);
	CHECK(core.Folds().front() == FoldRange {2, 4, FoldSource::Host});
	CHECK(core.GetSelection().caret == LineEnd(*model, 2));

	core.SetHostFolds({}, false);
	CHECK(core.Folds() == std::vector<FoldRange> {{5, 7}});
	CHECK(core.FoldMarkers(0, 3).empty());
}


TEST_CASE("fold all and per-view folds")
{
	BReference<TextModel> model = MakeModel(kSample, "cpp");
	EditorCore core(model);
	EditorCore other(model);

	core.SetSelection({LineStart(*model, 4), LineStart(*model, 4)});
	core.FoldAll();
	CHECK(core.Folds().size() == 2);
	CHECK(core.RowCount() == 5);
	CHECK(CaretLine(core) == 1);
	CHECK(other.RowCount() == 9);
	CHECK(other.Folds().empty());

	core.UnfoldAll();
	CHECK(core.RowCount() == 9);
}


} // CodeEdit
