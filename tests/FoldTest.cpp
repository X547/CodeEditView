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
