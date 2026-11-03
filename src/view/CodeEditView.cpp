#include <CodeEditView/CodeEditView.h>

#include <math.h>

#include <algorithm>
#include <string>
#include <vector>

#include <Clipboard.h>
#include <Cursor.h>
#include <Input.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Window.h>

#include <CodeEditView/CodeEditScrollView.h>

#include "EditorCore.h"
#include "GutterView.h"
#include "KeyMap.h"
#include "unicode/CharWidth.h"
#include "unicode/Utf8.h"


static constexpr uint32 kMsgCaretBlink = 'CEcb';
static constexpr uint32 kMsgAutoScroll = 'CEas';
static constexpr uint32 kMsgDropMove = 'CEdm';
static constexpr uint32 kMsgDropCopy = 'CEdc';
static constexpr uint32 kMsgKeyBinding = 'CEkb';

static constexpr bigtime_t kCaretBlinkInterval = 500000;
static constexpr float kHorizontalScrollMargin = 4;

// Auto-scrolling runs at about 60 Hz; the scroll distance of a tick depends on
// the elapsed time, so late ticks do not slow it down.
static constexpr bigtime_t kAutoScrollInterval = 16000;
static constexpr bigtime_t kMaxAutoScrollStep = 100000;
static constexpr float kAutoScrollLinearSpeed = 12;
static constexpr float kAutoScrollQuadraticSpeed = 0.25f;
static constexpr float kMaxAutoScrollSpeed = 20000;

// Pe starts a drag after the pointer moved more than 2 pixels.
static constexpr float kDragThreshold = 2;


namespace CodeEdit {


struct DrawContext {
	rgb_color background;
	rgb_color text;
	rgb_color currentLine;
	rgb_color selection;
	rgb_color placeholder;
	size_t selectionStart {};
	size_t selectionEnd {};
	size_t caretRow {};
	uint32_t caretColumn {};
	bool drawCaret {};
	std::optional<CaretPosition> dropCaret;
	std::optional<size_t> preeditRow;
	// Byte offset of the input method text in the spliced row text.
	size_t preeditStart {};
};


struct InputMethodState {
	BMessenger method;
	bool composing {};
	std::string text;
	// Byte ranges within text.
	int32 selectionStart {};
	int32 selectionEnd {};
	std::vector<std::pair<int32, int32>> clauses;
};


enum class TrackMode {
	None,
	Select,
	DragPending,
};


struct MouseState {
	TrackMode mode = TrackMode::None;
	SelectionUnit unit = SelectionUnit::Character;
	TextRange anchor;
	uint32 buttons {};
	BPoint downPoint;
	// Pointer position relative to the top left of the bounds; stays valid
	// while the view scrolls under a stationary pointer.
	BPoint trackPoint;
	size_t pendingOffset {};
	uint32 dragButtons {};
	std::optional<size_t> dropOffset;
	std::unique_ptr<BMessageRunner> autoScrollRunner;
	bigtime_t lastAutoScroll {};
	BPoint scrollRemainder;
	std::optional<bool> arrowCursor;
	bool pointerInside {};
	bool inScrollTracking {};
};


static bool IsInvisibleMark(char32_t codePoint)
{
	return codePoint == kZeroWidthJoiner
		|| (codePoint >= 0xfe00 && codePoint <= 0xfe0f)
		|| (codePoint >= 0xe0100 && codePoint <= 0xe01ef);
}


static BReference<TextModel> ModelReference(TextModel *model)
{
	if (model == nullptr) {
		return BReference<TextModel>(new TextModel(), true);
	}
	return BReference<TextModel>(model);
}


} // CodeEdit


CodeEditView::CodeEditView(const char *name, CodeEdit::TextModel *model):
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_INPUT_METHOD_AWARE),
	fCore(std::make_unique<CodeEdit::EditorCore>(CodeEdit::ModelReference(model))),
	fKeyMap(std::make_unique<CodeEdit::KeyMap>(CodeEdit::KeyMap::Default())),
	fMouse(std::make_unique<CodeEdit::MouseState>()),
	fInputMethod(std::make_unique<CodeEdit::InputMethodState>())
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetEditorFont(*be_fixed_font);
}


CodeEditView::~CodeEditView()
{
}


CodeEdit::TextModel *CodeEditView::Model() const
{
	return fCore->Model();
}


const CodeEdit::EditorSettings &CodeEditView::Settings() const
{
	return fCore->Settings();
}


void CodeEditView::SetSettings(const CodeEdit::EditorSettings &settings)
{
	fCore->SetSettings(settings);
	if (Window() != nullptr) {
		ApplyDirty();
	}
}


void CodeEditView::SetEditorFont(const BFont &font)
{
	SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	fAscent = ceilf(height.ascent);
	fLineHeight = fAscent + ceilf(height.descent) + ceilf(height.leading);
	fCellWidth = font.StringWidth("0");
	fGlyphCache.clear();

	if (fGutter != nullptr) {
		fGutter->SetFont(&font);
	}
	if (Window() != nullptr) {
		UpdateScrollBars();
		Invalidate();
		if (fScrollView != nullptr) {
			fScrollView->ContentsChanged();
		}
	}
}


void CodeEditView::SetTheme(const CodeEdit::Theme &theme)
{
	fTheme = theme;
	if (Window() != nullptr) {
		Invalidate();
	}
}


CodeEdit::Selection CodeEditView::GetSelection()
{
	return fCore->GetSelection();
}


void CodeEditView::SetSelection(const CodeEdit::Selection &selection)
{
	fCore->SetSelection(selection);
	if (Window() != nullptr) {
		ApplyDirty();
		RestartCaretBlink();
	}
}


void CodeEditView::ScrollToSelection()
{
	ScrollToCaret(false);
}


//#pragma mark - Drawing

std::pair<float, float> CodeEditView::GlyphMetrics(char32_t codePoint, const char *bytes, int32 length)
{
	const auto it = fGlyphCache.find(codePoint);
	if (it != fGlyphCache.end()) {
		return it->second;
	}
	BFont font;
	GetFont(&font);
	const float advance = font.StringWidth(bytes, length);
	BRect ink;
	font.GetBoundingBoxesAsGlyphs(bytes, 1, B_SCREEN_METRIC, &ink);
	const float inkCenter = ink.IsValid() ? (ink.left + ink.right) / 2 : advance / 2;
	return fGlyphCache.emplace(codePoint, std::pair(advance, inkCenter)).first->second;
}


void CodeEditView::Draw(BRect updateRect)
{
	using namespace CodeEdit;

	DrawContext context;
	context.background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	context.text = ui_color(B_DOCUMENT_TEXT_COLOR);
	context.currentLine = mix_color(context.background, context.text, 12);
	context.placeholder = mix_color(context.background, context.text, 128);
	const bool isActive = IsFocus() && Window()->IsActive();
	context.selection = isActive
		? mix_color(context.background, ui_color(B_KEYBOARD_NAVIGATION_COLOR), 80)
		: mix_color(context.background, context.text, 40);

	const Selection selection = fCore->GetSelection();
	context.selectionStart = std::min(selection.anchor, selection.caret);
	context.selectionEnd = std::max(selection.anchor, selection.caret);
	const CaretPosition caret = fCore->PositionOfOffset(selection.caret);
	context.caretRow = caret.row;
	context.caretColumn = caret.column;
	context.drawCaret = isActive && fCaretVisible && context.selectionStart == context.selectionEnd;
	if (fMouse->dropOffset.has_value()) {
		context.dropCaret = fCore->PositionOfOffset(*fMouse->dropOffset);
	}

	const size_t firstRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.top / fLineHeight)));
	const size_t lastRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.bottom / fLineHeight)));
	std::vector<RowData> rows = fCore->Rows(firstRow, lastRow - firstRow + 1);

	// The uncommitted input method text is shown inside the caret row.
	if (caret.row >= firstRow && caret.row < firstRow + rows.size()) {
		RowData &data = rows[caret.row - firstRow];
		if (SpliceInputMethod(data, selection.caret)) {
			const InputMethodState &input = *fInputMethod;
			context.preeditRow = caret.row;
			context.preeditStart = selection.caret - data.lineStart;
			context.caretColumn = data.layout.ColumnOfOffset(context.preeditStart + input.selectionStart);
			context.drawCaret = context.drawCaret && input.selectionStart == input.selectionEnd;
		}
	}

	SetDrawingMode(B_OP_OVER);
	for (size_t i = 0; i < rows.size(); i++) {
		DrawRow(firstRow + i, updateRect, rows[i], context);
	}

	const float rowsBottom = (firstRow + rows.size()) * fLineHeight;
	if (rowsBottom <= updateRect.bottom) {
		SetHighColor(context.background);
		FillRect(BRect(updateRect.left, rowsBottom, updateRect.right, updateRect.bottom));
	}
}


void CodeEditView::DrawRow(
	size_t row,
	const BRect &updateRect,
	const CodeEdit::RowData &data,
	const CodeEdit::DrawContext &context
)
{
	using namespace CodeEdit;

	const float top = row * fLineHeight;
	const float bottom = top + fLineHeight - 1;
	const float baseline = top + fAscent;
	const bool isCaretRow = row == context.caretRow;
	const bool hasSelection = context.selectionStart != context.selectionEnd;

	SetHighColor(isCaretRow && !hasSelection ? context.currentLine : context.background);
	FillRect(BRect(updateRect.left, top, updateRect.right, bottom));

	const size_t lineEnd = data.lineStart + data.text.size();
	const bool selectsNewline = data.hasNewline && context.selectionStart <= lineEnd
		&& context.selectionEnd > lineEnd;
	if (hasSelection && ((context.selectionEnd > data.lineStart && context.selectionStart < lineEnd) || selectsNewline)) {
		const float left = context.selectionStart <= data.lineStart
			? 0
			: data.layout.ColumnOfOffset(context.selectionStart - data.lineStart) * fCellWidth;
		const float right = selectsNewline
			? updateRect.right
			: data.layout.ColumnOfOffset(std::min(context.selectionEnd, lineEnd) - data.lineStart) * fCellWidth - 1;
		SetHighColor(context.selection);
		FillRect(BRect(left, top, right, bottom));
	}

	const float firstColumn = floorf(updateRect.left / fCellWidth) - 2;
	const float lastColumn = ceilf(updateRect.right / fCellWidth) + 2;

	std::string runBytes;
	std::vector<BPoint> runPoints;
	auto flush = [&]() {
		if (!runPoints.empty()) {
			DrawString(runBytes.data(), runBytes.size(), runPoints.data(), runPoints.size());
		}
		runBytes.clear();
		runPoints.clear();
	};

	rgb_color currentColor = context.text;
	SetHighColor(currentColor);
	for (const Cluster &cluster : data.layout.Clusters()) {
		if (cluster.EndColumn() < firstColumn) {
			continue;
		}
		if (cluster.column > lastColumn) {
			break;
		}
		if (cluster.kind == ClusterKind::Text) {
			const rgb_color color = data.styles.empty() ? context.text : fTheme.Color(data.styles[cluster.offset]);
			if (color != currentColor) {
				flush();
				SetHighColor(color);
				currentColor = color;
			}
		}
		const float x = cluster.column * fCellWidth;
		const float cellsWidth = cluster.width * fCellWidth;

		switch (cluster.kind) {
			case ClusterKind::Tab:
				break;
			case ClusterKind::Placeholder:
			case ClusterKind::InvalidByte: {
				flush();
				SetHighColor(context.placeholder);
				currentColor = context.placeholder;
				DrawString(PlaceholderText(cluster).c_str(), BPoint(x, baseline));
				break;
			}
			case ClusterKind::Text: {
				const Utf8Char base = DecodeUtf8(data.text, cluster.offset);
				const auto [baseAdvance, baseInkCenter] = GlyphMetrics(
					base.codePoint,
					data.text.data() + cluster.offset,
					base.length
				);
				const float baseX = x + (cellsWidth - baseAdvance) / 2;
				runBytes.append(data.text, cluster.offset, base.length);
				runPoints.push_back(BPoint(baseX, baseline));
				if (GetCharProperties(base.codePoint).emoji) {
					// Without emoji shaping only the base of a sequence is drawn.
					break;
				}
				// Without text shaping, combining marks are placed by hand: the
				// ink of each mark is centered over the ink of the base glyph.
				for (size_t pos = cluster.offset + base.length; pos < cluster.EndOffset();) {
					const Utf8Char mark = DecodeUtf8(data.text, pos);
					if (!IsInvisibleMark(mark.codePoint)) {
						const float markInkCenter = GlyphMetrics(mark.codePoint, data.text.data() + pos, mark.length).second;
						runBytes.append(data.text, pos, mark.length);
						runPoints.push_back(BPoint(baseX + baseInkCenter - markInkCenter, baseline));
					}
					pos += mark.length;
				}
				break;
			}
			default:
				break;
		}
	}
	flush();

	const float caretWidth = std::max(1.0f, roundf(fLineHeight / 16));
	if (context.preeditRow == row) {
		// Thin underline per clause (or the whole text), thick under the
		// clause being converted.
		const InputMethodState &input = *fInputMethod;
		auto underline = [&](int32 start, int32 end, float thickness) {
			const float left = data.layout.ColumnOfOffset(context.preeditStart + start) * fCellWidth;
			const float right = data.layout.ColumnOfOffset(context.preeditStart + end) * fCellWidth - 2;
			if (right > left) {
				FillRect(BRect(left + 1, bottom - thickness + 1, right, bottom));
			}
		};
		SetHighColor(context.text);
		if (input.clauses.empty()) {
			underline(0, static_cast<int32>(input.text.size()), caretWidth);
		}
		for (const auto &[start, end] : input.clauses) {
			underline(start, end, caretWidth);
		}
		if (input.selectionEnd > input.selectionStart) {
			underline(input.selectionStart, input.selectionEnd, 2 * caretWidth);
		}
	}
	if (isCaretRow && context.drawCaret) {
		const float x = context.caretColumn * fCellWidth;
		SetHighColor(context.text);
		FillRect(BRect(x, top, x + caretWidth - 1, bottom));
	}
	if (context.dropCaret.has_value() && context.dropCaret->row == row) {
		const float x = context.dropCaret->column * fCellWidth;
		SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
		FillRect(BRect(x, top, x + 2 * caretWidth - 1, bottom));
	}
}


void CodeEditView::InvalidateRows(size_t first, size_t last)
{
	const float top = first * fLineHeight;
	const float bottom = last == SIZE_MAX ? FLT_MAX : (last + 1) * fLineHeight - 1;

	const BRect bounds = Bounds();
	if (top <= bounds.bottom && bottom >= bounds.top) {
		Invalidate(BRect(bounds.left, std::max(top, bounds.top), bounds.right, std::min(bottom, bounds.bottom)));
	}
	if (fGutter == nullptr) {
		return;
	}
	const BRect gutterBounds = fGutter->Bounds();
	if (top <= gutterBounds.bottom && bottom >= gutterBounds.top) {
		fGutter->Invalidate(BRect(
			gutterBounds.left,  std::max(top, gutterBounds.top),
			gutterBounds.right, std::min(bottom, gutterBounds.bottom)
		));
	}
}


void CodeEditView::InvalidateCaret()
{
	const CodeEdit::CaretPosition caret = fCore->PositionOfOffset(fCore->GetSelection().caret);
	const float x = caret.column * fCellWidth;
	const float top = caret.row * fLineHeight;
	Invalidate(BRect(x - 1, top, x + fLineHeight / 8 + 1, top + fLineHeight - 1));
}


void CodeEditView::RestartCaretBlink()
{
	fCaretVisible = true;
	InvalidateCaret();
	if (!IsFocus() || Window() == nullptr || !Window()->IsActive()) {
		fCaretRunner.reset();
		return;
	}
	BMessage message(kMsgCaretBlink);
	fCaretRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kCaretBlinkInterval);
}


//#pragma mark - Scrolling

size_t CodeEditView::TopRow() const
{
	return static_cast<size_t>(std::max(0.0f, floorf(Bounds().top / fLineHeight)));
}


CodeEdit::PageInfo CodeEditView::CurrentPage() const
{
	const BRect bounds = Bounds();
	const size_t topRow = static_cast<size_t>(std::max(0.0f, ceilf(bounds.top / fLineHeight)));
	const float visibleHeight = bounds.bottom + 1 - topRow * fLineHeight;
	const size_t visibleRows = static_cast<size_t>(std::max(1.0f, floorf(visibleHeight / fLineHeight)));
	return {topRow, visibleRows};
}


void CodeEditView::UpdateScrollBars()
{
	const BRect bounds = Bounds();
	const float height = fCore->RowCount() * fLineHeight;
	const float width = (fCore->MaxColumns() + 2) * fCellWidth;

	if (BScrollBar *bar = ScrollBar(B_VERTICAL); bar != nullptr) {
		const float visible = bounds.Height() + 1;
		bar->SetRange(0, std::max(0.0f, height - visible));
		bar->SetProportion(height > 0 ? std::min(1.0f, visible / height) : 1.0f);
		bar->SetSteps(fLineHeight, std::max(fLineHeight, visible - fLineHeight));
	}
	if (BScrollBar *bar = ScrollBar(B_HORIZONTAL); bar != nullptr) {
		const float visible = bounds.Width() + 1;
		bar->SetRange(0, std::max(0.0f, width - visible));
		bar->SetProportion(width > 0 ? std::min(1.0f, visible / width) : 1.0f);
		bar->SetSteps(fCellWidth * 4, std::max(fCellWidth, visible - fCellWidth * 4));
	}
}


void CodeEditView::ScrollToClamped(float x, float y)
{
	const BRect bounds = Bounds();
	const float height = fCore->RowCount() * fLineHeight;
	const float width = (fCore->MaxColumns() + 2) * fCellWidth;
	const float maxX = std::max(0.0f, width - bounds.Width() - 1);
	const float maxY = std::max(0.0f, height - bounds.Height() - 1);
	x = std::clamp(roundf(x), 0.0f, maxX);
	y = std::clamp(roundf(y), 0.0f, maxY);
	if (x != bounds.left || y != bounds.top) {
		ScrollTo(BPoint(x, y));
	}
}


void CodeEditView::ScrollTo(BPoint where)
{
	BView::ScrollTo(where);
	if (fGutter != nullptr) {
		fGutter->ScrollTo(BPoint(0, where.y));
	}
	fCore->SetScrollAnchor(fCore->RowStartOffset(TopRow()));

	// Scrolling (e.g. by the mouse wheel) moves the text under a stationary
	// pointer without MouseMoved(); follow it while tracking.
	if (fMouse->inScrollTracking) {
		return;
	}
	fMouse->inScrollTracking = true;
	const BPoint pointer = Bounds().LeftTop() + fMouse->trackPoint;
	if (fMouse->mode == CodeEdit::TrackMode::Select) {
		TrackSelection(pointer);
	} else if (fMouse->dropOffset.has_value()) {
		SetDropOffset(OffsetAtPoint(ClampToBounds(pointer)));
	} else {
		RefreshCursor();
	}
	fMouse->inScrollTracking = false;
}


bool CodeEditView::CaretInContextArea()
{
	const size_t row = fCore->PositionOfOffset(fCore->GetSelection().caret).row;
	const CodeEdit::PageInfo page = CurrentPage();
	const size_t context = fCore->Settings().contextLines;
	return row >= page.topRow + context && row + context < page.topRow + page.visibleRows;
}


// Keeps the caret visible; with keepContext also keeps context rows around it
// (Pe behavior: only when the caret was not already inside the context area).
void CodeEditView::ScrollToCaret(bool keepContext)
{
	const CodeEdit::CaretPosition caret = fCore->PositionOfOffset(fCore->GetSelection().caret);
	const BRect bounds = Bounds();
	const float visibleHeight = bounds.Height() + 1;
	const float visibleWidth = bounds.Width() + 1;
	const size_t visibleRows = static_cast<size_t>(std::max(1.0f, floorf(visibleHeight / fLineHeight)));
	const size_t contextRows = keepContext
		? std::min<size_t>(fCore->Settings().contextLines, (visibleRows - 1) / 2)
		: 0;
	const float context = contextRows * fLineHeight;

	float y = bounds.top;
	const float caretTop = caret.row * fLineHeight;
	const float caretBottom = caretTop + fLineHeight;
	if (caretTop - context < y) {
		y = caretTop - context;
	} else if (caretBottom + context > y + visibleHeight) {
		y = caretBottom + context - visibleHeight;
	}

	float x = bounds.left;
	const float caretX = caret.column * fCellWidth;
	const float margin = kHorizontalScrollMargin * fCellWidth;
	if (caretX < x) {
		x = caretX - margin;
	} else if (caretX + fCellWidth > x + visibleWidth) {
		x = caretX + fCellWidth + margin - visibleWidth;
	}
	ScrollToClamped(x, y);
}


// Applies accumulated changes of the editor core to the screen.
void CodeEditView::ApplyDirty()
{
	const CodeEdit::DirtyRows dirty = fCore->TakeDirty();
	if (dirty.scrollRangeChanged) {
		UpdateScrollBars();
		if (fScrollView != nullptr) {
			fScrollView->ContentsChanged();
		}
	}

	// Keep the text at the top of the view in place when lines are inserted or
	// removed above it.
	const size_t anchorRow = fCore->PositionOfOffset(fCore->ScrollAnchor()).row;
	const size_t topRow = TopRow();
	if (anchorRow != topRow) {
		const float fraction = Bounds().top - topRow * fLineHeight;
		ScrollToClamped(Bounds().left, anchorRow * fLineHeight + fraction);
	}

	if (!dirty.IsEmpty()) {
		InvalidateRows(dirty.first, dirty.last);
		RefreshCursor();
	}
	if (dirty.marksChanged && fGutter != nullptr) {
		fGutter->Invalidate();
	}
}


//#pragma mark - Commands

void CodeEditView::ExecuteCommand(CodeEdit::EditorCommand command, bool extend)
{
	using namespace CodeEdit;

	CommitInputMethod();

	const bool keepContext = CaretInContextArea();
	const BRect bounds = Bounds();
	const PageInfo page = CurrentPage();
	bool scrollToCaret = true;
	auto move = [&](Movement movement) {
		fCore->Move(movement, extend, page);
	};
	auto scroll = [&](float x, float y) {
		ScrollToClamped(x, y);
		scrollToCaret = false;
	};

	switch (command) {
		case EditorCommand::MoveCharacterLeft:
			move(Movement::CharacterLeft);
			break;
		case EditorCommand::MoveCharacterRight:
			move(Movement::CharacterRight);
			break;
		case EditorCommand::MoveWordLeft:
			move(Movement::WordLeft);
			break;
		case EditorCommand::MoveWordRight:
			move(Movement::WordRight);
			break;
		case EditorCommand::MoveSubwordLeft:
			move(Movement::SubwordLeft);
			break;
		case EditorCommand::MoveSubwordRight:
			move(Movement::SubwordRight);
			break;
		case EditorCommand::MoveToLineStart:
			move(Movement::LineStart);
			break;
		case EditorCommand::MoveToLineEnd:
			move(Movement::LineEnd);
			break;
		case EditorCommand::MoveToPreviousLine:
			move(Movement::PreviousLine);
			break;
		case EditorCommand::MoveToNextLine:
			move(Movement::NextLine);
			break;
		case EditorCommand::MoveToPageTop:
			move(Movement::PageTop);
			break;
		case EditorCommand::MoveToPageBottom:
			move(Movement::PageBottom);
			break;
		case EditorCommand::MoveToFileStart:
			move(Movement::FileStart);
			break;
		case EditorCommand::MoveToFileEnd:
			move(Movement::FileEnd);
			break;
		case EditorCommand::DeleteCharacterLeft:
			fCore->Delete(Deletion::CharacterLeft);
			break;
		case EditorCommand::DeleteCharacterRight:
			fCore->Delete(Deletion::CharacterRight);
			break;
		case EditorCommand::DeleteToLineStart:
			fCore->Delete(Deletion::ToLineStart);
			break;
		case EditorCommand::DeleteToLineEnd:
			fCore->Delete(Deletion::ToLineEnd);
			break;
		case EditorCommand::ScrollLineUp:
			scroll(bounds.left, bounds.top - fLineHeight);
			break;
		case EditorCommand::ScrollLineDown:
			scroll(bounds.left, bounds.top + fLineHeight);
			break;
		case EditorCommand::ScrollColumnLeft:
			scroll(bounds.left - fCellWidth, bounds.top);
			break;
		case EditorCommand::ScrollColumnRight:
			scroll(bounds.left + fCellWidth, bounds.top);
			break;
		case EditorCommand::ScrollPageUp:
			scroll(bounds.left, bounds.top - std::max<size_t>(1, page.visibleRows - 1) * fLineHeight);
			break;
		case EditorCommand::ScrollPageDown:
			scroll(bounds.left, bounds.top + std::max<size_t>(1, page.visibleRows - 1) * fLineHeight);
			break;
		case EditorCommand::ScrollToStart:
			scroll(bounds.left, 0);
			break;
		case EditorCommand::ScrollToEnd:
			scroll(bounds.left, FLT_MAX);
			break;
		case EditorCommand::Undo:
			fCore->Undo();
			break;
		case EditorCommand::Redo:
			fCore->Redo();
			break;
		case EditorCommand::SelectAll:
			fCore->SelectAll();
			scrollToCaret = false;
			break;
		case EditorCommand::ExpandSelection:
			fCore->ExpandSelection();
			break;
		case EditorCommand::ShrinkSelection:
			fCore->ShrinkSelection();
			break;
		case EditorCommand::MoveToPreviousDefinition:
			fCore->MoveToDefinition(false);
			break;
		case EditorCommand::MoveToNextDefinition:
			fCore->MoveToDefinition(true);
			break;
		default:
			break;
	}

	ApplyDirty();
	if (scrollToCaret) {
		ScrollToCaret(keepContext);
	}
	RestartCaretBlink();
}


// BWindow consumes all Command key presses: it runs shortcuts and drops the
// rest before KeyDown(). Like BTextView, the focused view therefore installs
// window shortcuts for its Command bindings, leaving keys the window already
// uses (e.g. menu items) alone.
void CodeEditView::InstallCommandShortcuts()
{
	BWindow *window = Window();
	if (window == nullptr || !fInstalledShortcuts.empty()) {
		return;
	}
	for (const CodeEdit::KeyBinding &binding : fKeyMap->Bindings()) {
		if ((binding.modifiers & B_COMMAND_KEY) == 0 || window->HasShortcut(binding.key, binding.modifiers)) {
			continue;
		}
		BMessage *message = new BMessage(kMsgKeyBinding);
		message->AddInt32("command", static_cast<int32>(binding.command));
		message->AddBool("extend", binding.extend);
		window->AddShortcut(binding.key, binding.modifiers, message, this);
		fInstalledShortcuts.emplace_back(binding.key, binding.modifiers);
	}
}


void CodeEditView::RemoveCommandShortcuts()
{
	BWindow *window = Window();
	if (window != nullptr) {
		for (const auto &[key, modifiers] : fInstalledShortcuts) {
			window->RemoveShortcut(key, modifiers);
		}
	}
	fInstalledShortcuts.clear();
}


void CodeEditView::Copy()
{
	const std::string text = fCore->SelectedText();
	if (text.empty() || !be_clipboard->Lock()) {
		return;
	}
	be_clipboard->Clear();
	BMessage *clip = be_clipboard->Data();
	if (clip != nullptr) {
		clip->AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
		be_clipboard->Commit();
	}
	be_clipboard->Unlock();
}


void CodeEditView::Cut()
{
	CommitInputMethod();
	Copy();
	fCore->DeleteSelection();
	ApplyDirty();
	ScrollToCaret(false);
	RestartCaretBlink();
}


void CodeEditView::Paste()
{
	CommitInputMethod();
	std::string text;
	if (!be_clipboard->Lock()) {
		return;
	}
	BMessage *clip = be_clipboard->Data();
	const void *data = nullptr;
	ssize_t size = 0;
	if (clip != nullptr && clip->FindData("text/plain", B_MIME_TYPE, &data, &size) == B_OK) {
		text.assign(static_cast<const char*>(data), size);
	}
	be_clipboard->Unlock();

	fCore->Paste(text);
	ApplyDirty();
	ScrollToCaret(false);
	RestartCaretBlink();
}


//#pragma mark - Input method

// Inserts the uncommitted input method text at the caret into a row for
// display. It never enters the model, so other views do not see it.
bool CodeEditView::SpliceInputMethod(CodeEdit::RowData &data, size_t caret)
{
	const CodeEdit::InputMethodState &input = *fInputMethod;
	if (!input.composing || input.text.empty()) {
		return false;
	}
	if (caret < data.lineStart || caret > data.lineStart + data.text.size()) {
		return false;
	}
	if (!data.styles.empty()) {
		data.styles.insert(data.styles.begin() + (caret - data.lineStart), input.text.size(), CodeEdit::HighlightStyle::Default);
	}
	data.text.insert(caret - data.lineStart, input.text);
	data.layout = CodeEdit::LineLayout(data.text, fCore->GetLayoutOptions());
	return true;
}


void CodeEditView::InvalidateInputMethodRow()
{
	const size_t row = fCore->PositionOfOffset(fCore->GetSelection().caret).row;
	InvalidateRows(row, row);
}


void CodeEditView::HandleInputMethodEvent(BMessage *message)
{
	CodeEdit::InputMethodState &input = *fInputMethod;
	int32 opcode;
	if (message->FindInt32("be:opcode", &opcode) != B_OK) {
		return;
	}
	switch (opcode) {
		case B_INPUT_METHOD_STARTED: {
			BMessenger method;
			if (message->FindMessenger("be:reply_to", &method) == B_OK) {
				input = {};
				input.method = method;
			}
			break;
		}
		case B_INPUT_METHOD_STOPPED:
			InvalidateInputMethodRow();
			input = {};
			break;
		case B_INPUT_METHOD_CHANGED:
			HandleInputMethodChanged(message);
			break;
		case B_INPUT_METHOD_LOCATION_REQUEST:
			SendInputMethodLocation();
			break;
		default:
			break;
	}
}


void CodeEditView::HandleInputMethodChanged(BMessage *message)
{
	CodeEdit::InputMethodState &input = *fInputMethod;
	const char *string = nullptr;
	if (message->FindString("be:string", &string) != B_OK || string == nullptr) {
		return;
	}

	if (!input.composing) {
		// Composing replaces the selection, like typing.
		fCore->DeleteSelection();
		ApplyDirty();
		input.composing = true;
	}
	InvalidateInputMethodRow();

	if (message->GetBool("be:confirmed", false)) {
		const std::string text = string;
		input.composing = false;
		input.text.clear();
		input.clauses.clear();
		fCore->InsertText(text);
		ApplyDirty();
		ScrollToCaret(false);
		RestartCaretBlink();
		return;
	}

	input.text = string;
	const int32 length = static_cast<int32>(input.text.size());
	input.selectionStart = std::clamp(message->GetInt32("be:selection", 0, length), 0, length);
	input.selectionEnd = std::clamp(message->GetInt32("be:selection", 1, length), input.selectionStart, length);
	input.clauses.clear();
	int32 clauseStart;
	int32 clauseEnd;
	for (int32 i = 0; message->FindInt32("be:clause_start", i, &clauseStart) == B_OK
			&& message->FindInt32("be:clause_end", i, &clauseEnd) == B_OK; i++) {
		clauseStart = std::clamp(clauseStart, 0, length);
		input.clauses.emplace_back(clauseStart, std::clamp(clauseEnd, clauseStart, length));
	}

	InvalidateInputMethodRow();
	ScrollToCaret(false);
	RestartCaretBlink();
}


// Replies with the screen position of each character of the input method
// text, used to place the candidate window.
void CodeEditView::SendInputMethodLocation()
{
	CodeEdit::InputMethodState &input = *fInputMethod;
	if (!input.method.IsValid()) {
		return;
	}
	const size_t caret = fCore->GetSelection().caret;
	const CodeEdit::CaretPosition position = fCore->PositionOfOffset(caret);
	std::vector<CodeEdit::RowData> rows = fCore->Rows(position.row, 1);

	BMessage reply(B_INPUT_METHOD_EVENT);
	reply.AddInt32("be:opcode", B_INPUT_METHOD_LOCATION_REQUEST);
	const float top = position.row * fLineHeight;
	if (rows.empty() || !SpliceInputMethod(rows.front(), caret)) {
		reply.AddPoint("be:location_reply", ConvertToScreen(BPoint(position.column * fCellWidth, top)));
		reply.AddFloat("be:height_reply", fLineHeight);
	} else {
		const CodeEdit::RowData &data = rows.front();
		const size_t start = caret - data.lineStart;
		for (size_t i = 0; i < input.text.size(); i++) {
			if ((static_cast<uint8>(input.text[i]) & 0xc0) == 0x80) {
				continue;
			}
			const float x = data.layout.ColumnOfOffset(start + i) * fCellWidth;
			reply.AddPoint("be:location_reply", ConvertToScreen(BPoint(x, top)));
			reply.AddFloat("be:height_reply", fLineHeight);
		}
	}
	input.method.SendMessage(&reply);
}


// Ends an active composition by committing its current text, e.g. when the
// user clicks elsewhere, and tells the input method to stop.
void CodeEditView::CommitInputMethod()
{
	CodeEdit::InputMethodState &input = *fInputMethod;
	if (!input.composing) {
		return;
	}
	const std::string text = input.text;
	const BMessenger method = input.method;
	InvalidateInputMethodRow();
	input = {};

	if (method.IsValid()) {
		BMessage message(B_INPUT_METHOD_EVENT);
		message.AddInt32("be:opcode", B_INPUT_METHOD_STOPPED);
		method.SendMessage(&message);
	}
	fCore->InsertText(text);
	if (Window() != nullptr) {
		ApplyDirty();
	}
}


//#pragma mark - BView hooks

void CodeEditView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fCore->Attach(BMessenger(this));
	fCore->TakeDirty();
	UpdateScrollBars();
}


void CodeEditView::DetachedFromWindow()
{
	CommitInputMethod();
	RemoveCommandShortcuts();
	fCaretRunner.reset();
	StopAutoScroll();
	fCore->Detach();
	BView::DetachedFromWindow();
}


void CodeEditView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	UpdateScrollBars();
}


void CodeEditView::KeyDown(const char *bytes, int32 numBytes)
{
	CommitInputMethod();
	BMessage *message = Window()->CurrentMessage();
	const uint8 firstByte = static_cast<uint8>(bytes[0]);
	const uint32 modifiers = message != nullptr ? message->GetInt32("modifiers", 0) : ::modifiers();
	const uint32 rawChar = message != nullptr ? message->GetInt32("raw_char", firstByte) : firstByte;

	if (const std::optional<CodeEdit::KeyBinding> binding = fKeyMap->Find(rawChar, modifiers)) {
		ExecuteCommand(binding->command, binding->extend);
		return;
	}
	if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY)) != 0) {
		BView::KeyDown(bytes, numBytes);
		return;
	}

	const bool keepContext = CaretInContextArea();
	switch (firstByte) {
		case B_ENTER:
			fCore->InsertNewline();
			break;
		case B_TAB:
			if ((modifiers & B_SHIFT_KEY) != 0) {
				BView::KeyDown(bytes, numBytes);
				return;
			}
			fCore->InsertTab();
			break;
		default:
			if (firstByte < 0x20 || firstByte == B_DELETE) {
				BView::KeyDown(bytes, numBytes);
				return;
			}
			fCore->InsertText(std::string_view(bytes, numBytes));
			break;
	}
	ApplyDirty();
	ScrollToCaret(keepContext);
	RestartCaretBlink();
}


size_t CodeEditView::OffsetAtPoint(BPoint where)
{
	const size_t row = static_cast<size_t>(std::max(0.0f, floorf(where.y / fLineHeight)));
	return fCore->OffsetAt(row, where.x / fCellWidth);
}


//#pragma mark - Mouse

BPoint CodeEditView::ClampToBounds(BPoint where) const
{
	const BRect bounds = Bounds();
	return BPoint(
		std::clamp(where.x, bounds.left, bounds.right),
		std::clamp(where.y, bounds.top, bounds.bottom)
	);
}


bool CodeEditView::IsPointInSelection(BPoint where)
{
	const CodeEdit::Selection selection = fCore->GetSelection();
	const size_t start = std::min(selection.anchor, selection.caret);
	const size_t end = std::max(selection.anchor, selection.caret);
	if (start == end || where.y < 0) {
		return false;
	}
	const size_t row = static_cast<size_t>(floorf(where.y / fLineHeight));
	if (row >= fCore->RowCount()) {
		return false;
	}
	// Past the line end the newline is hit, which is selected when it is
	// inside the range.
	const CodeEdit::CellHit hit = fCore->HitTestCell(row, where.x / fCellWidth);
	return hit.offset >= start && hit.offset < end;
}


void CodeEditView::UpdateCursor(BPoint where)
{
	static const BCursor sIBeamCursor(B_CURSOR_ID_I_BEAM);
	static const BCursor sArrowCursor(B_CURSOR_ID_SYSTEM_DEFAULT);

	const bool arrow = IsPointInSelection(where);
	if (fMouse->arrowCursor == arrow) {
		return;
	}
	fMouse->arrowCursor = arrow;
	SetViewCursor(arrow ? &sArrowCursor : &sIBeamCursor);
}


void CodeEditView::MouseDown(BPoint where)
{
	using namespace CodeEdit;

	CommitInputMethod();

	MakeFocus(true);
	BMessage *message = Window()->CurrentMessage();
	const uint32 modifiers = message != nullptr ? message->GetInt32("modifiers", 0) : ::modifiers();
	const uint32 buttons = message != nullptr
		? message->GetInt32("buttons", B_PRIMARY_MOUSE_BUTTON)
		: B_PRIMARY_MOUSE_BUTTON;
	const int32 clicks = message != nullptr ? message->GetInt32("clicks", 1) : 1;
	const bool extend = (modifiers & B_SHIFT_KEY) != 0;
	const bool primary = (buttons & B_PRIMARY_MOUSE_BUTTON) != 0;

	fMouse->buttons = buttons;
	fMouse->downPoint = where;
	fMouse->trackPoint = where - Bounds().LeftTop();
	const size_t offset = OffsetAtPoint(where);

	// Pressing inside the selection may start a drag, with any button (Pe).
	if (IsPointInSelection(where) && !extend && (clicks == 1 || !primary)) {
		fMouse->mode = TrackMode::DragPending;
		fMouse->pendingOffset = offset;
		SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
		return;
	}
	if (!primary) {
		BView::MouseDown(where);
		return;
	}

	TextRange range {offset, offset};
	SelectionUnit unit = SelectionUnit::Character;
	if (extend) {
		const Selection selection = fCore->GetSelection();
		range = {selection.anchor, selection.anchor};
		fCore->ExtendSelection(range, offset, unit);
	} else if (clicks == 2) {
		const size_t row = static_cast<size_t>(std::max(0.0f, floorf(where.y / fLineHeight)));
		const CellHit hit = fCore->HitTestCell(row, where.x / fCellWidth);
		const std::optional<TextRange> balanced = hit.pastLineEnd
			? std::nullopt
			: fCore->BalancedRangeAt(hit.offset);
		if (balanced.has_value()) {
			// Pe: selecting the contents of a bracket pair does not track.
			fCore->SetSelection({balanced->start, balanced->end});
			fMouse->mode = TrackMode::None;
			ApplyDirty();
			RestartCaretBlink();
			return;
		}
		unit = SelectionUnit::Word;
		range = hit.pastLineEnd ? TextRange {hit.offset, hit.offset} : fCore->UnitRangeAt(hit.offset, unit);
		fCore->SetSelection({range.start, range.end});
	} else if (clicks >= 3) {
		unit = SelectionUnit::Line;
		range = fCore->UnitRangeAt(offset, unit);
		fCore->SetSelection({range.start, range.end});
	} else {
		fCore->SetSelection({offset, offset});
	}

	fMouse->mode = TrackMode::Select;
	fMouse->unit = unit;
	fMouse->anchor = range;
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS | B_NO_POINTER_HISTORY);
	ApplyDirty();
	RestartCaretBlink();
}


void CodeEditView::MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage)
{
	using namespace CodeEdit;

	if (dragMessage != nullptr) {
		if (dragMessage->HasData("text/plain", B_MIME_TYPE)) {
			HandleDragOver(where, transit);
		}
		return;
	}

	switch (fMouse->mode) {
		case TrackMode::DragPending: {
			const BPoint delta = where - fMouse->downPoint;
			if (fabsf(delta.x) > kDragThreshold || fabsf(delta.y) > kDragThreshold) {
				StartDrag();
			}
			break;
		}
		case TrackMode::Select:
			fMouse->trackPoint = where - Bounds().LeftTop();
			TrackSelection(where);
			UpdateAutoScroll(where);
			break;
		default:
			fMouse->trackPoint = where - Bounds().LeftTop();
			fMouse->pointerInside = transit != B_EXITED_VIEW && transit != B_OUTSIDE_VIEW;
			if (fMouse->pointerInside) {
				UpdateCursor(where);
			} else {
				fMouse->arrowCursor.reset();
			}
			break;
	}
}


void CodeEditView::MouseUp(BPoint where)
{
	using namespace CodeEdit;

	// A click inside the selection without dragging places the caret.
	if (fMouse->mode == TrackMode::DragPending && (fMouse->buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
		fCore->SetSelection({fMouse->pendingOffset, fMouse->pendingOffset});
		ApplyDirty();
	}
	fMouse->mode = TrackMode::None;
	StopAutoScroll();
	fMouse->trackPoint = where - Bounds().LeftTop();
	fMouse->pointerInside = Bounds().Contains(where);
	fMouse->arrowCursor.reset();
	RefreshCursor();
	RestartCaretBlink();
}


// Re-evaluates the cursor shape for a stationary pointer after the text under
// it or the selection changed.
void CodeEditView::RefreshCursor()
{
	if (fMouse->mode == CodeEdit::TrackMode::None && fMouse->pointerInside) {
		UpdateCursor(Bounds().LeftTop() + fMouse->trackPoint);
	}
}


void CodeEditView::TrackSelection(BPoint where)
{
	const size_t offset = OffsetAtPoint(ClampToBounds(where));
	fCore->ExtendSelection(fMouse->anchor, offset, fMouse->unit);
	ApplyDirty();
	fCaretVisible = true;
}


// How hard the pointer pushes for auto-scrolling: while selecting, its
// distance outside the bounds; while dragging over the view, its depth into
// the edge zone.
BPoint CodeEditView::AutoScrollPressure(BPoint where) const
{
	const BRect bounds = Bounds();
	BRect inner = bounds;
	if (fMouse->dropOffset.has_value()) {
		inner.InsetBy(
			std::min(2 * fCellWidth, bounds.Width() / 4),
			std::min(1.5f * fLineHeight, bounds.Height() / 4)
		);
	}

	BPoint pressure(0, 0);
	if (where.x < inner.left) {
		pressure.x = where.x - inner.left;
	} else if (where.x > inner.right) {
		pressure.x = where.x - inner.right;
	}
	if (where.y < inner.top) {
		pressure.y = where.y - inner.top;
	} else if (where.y > inner.bottom) {
		pressure.y = where.y - inner.bottom;
	}
	return pressure;
}


void CodeEditView::UpdateAutoScroll(BPoint where)
{
	const BPoint pressure = AutoScrollPressure(where);
	if (pressure.x == 0 && pressure.y == 0) {
		StopAutoScroll();
		return;
	}
	if (fMouse->autoScrollRunner != nullptr) {
		return;
	}
	fMouse->lastAutoScroll = system_time();
	fMouse->scrollRemainder = BPoint(0, 0);
	BMessage message(kMsgAutoScroll);
	fMouse->autoScrollRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kAutoScrollInterval);
}


void CodeEditView::StopAutoScroll()
{
	fMouse->autoScrollRunner.reset();
}


// Pixels per second for a pressure in pixels.
static float AutoScrollSpeed(float pressure)
{
	const float distance = fabsf(pressure);
	const float speed = std::min(
		kMaxAutoScrollSpeed,
		kAutoScrollLinearSpeed * distance + kAutoScrollQuadraticSpeed * distance * distance
	);
	return pressure < 0 ? -speed : speed;
}


void CodeEditView::AutoScrollTick()
{
	using namespace CodeEdit;

	const bool selecting = fMouse->mode == TrackMode::Select;
	if (!selecting && !fMouse->dropOffset.has_value()) {
		StopAutoScroll();
		return;
	}

	const bigtime_t now = system_time();
	const float seconds = std::min(now - fMouse->lastAutoScroll, kMaxAutoScrollStep) / 1000000.0f;
	fMouse->lastAutoScroll = now;

	const BRect bounds = Bounds();
	const BPoint pressure = AutoScrollPressure(bounds.LeftTop() + fMouse->trackPoint);
	BPoint &remainder = fMouse->scrollRemainder;
	remainder.x += AutoScrollSpeed(pressure.x) * seconds;
	remainder.y += AutoScrollSpeed(pressure.y) * seconds;
	const float dx = truncf(remainder.x);
	const float dy = truncf(remainder.y);
	remainder.x -= dx;
	remainder.y -= dy;
	// ScrollTo() updates the selection or drop position.
	if (dx != 0 || dy != 0) {
		ScrollToClamped(bounds.left + dx, bounds.top + dy);
	}
}


//#pragma mark - Drag and drop

BRect CodeEditView::SelectionFrame()
{
	const CodeEdit::Selection selection = fCore->GetSelection();
	const size_t startRow = fCore->PositionOfOffset(std::min(selection.anchor, selection.caret)).row;
	const size_t endRow = fCore->PositionOfOffset(std::max(selection.anchor, selection.caret)).row;
	const BRect bounds = Bounds();
	BRect frame(bounds.left, startRow * fLineHeight, bounds.right, (endRow + 1) * fLineHeight - 1);
	frame = frame & bounds;
	if (!frame.IsValid()) {
		frame = BRect(fMouse->downPoint, fMouse->downPoint).InsetByCopy(-4, -4);
	}
	return frame;
}


void CodeEditView::StartDrag()
{
	using namespace CodeEdit;

	fMouse->mode = TrackMode::None;
	StopAutoScroll();
	const std::string text = fCore->SelectedText();
	if (text.empty()) {
		return;
	}

	BMessage drag(B_MIME_DATA);
	drag.AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
	drag.AddPointer("be:originator", this);
	drag.AddString("be:clip_name", "Text snippet");
	fMouse->dragButtons = fMouse->buttons;
	fMouse->arrowCursor.reset();
	SetViewCursor(B_CURSOR_SYSTEM_DEFAULT);
	DragMessage(&drag, SelectionFrame());
}


void CodeEditView::SetDropOffset(std::optional<size_t> offset)
{
	if (offset == fMouse->dropOffset) {
		return;
	}
	auto invalidate = [this](size_t dropOffset) {
		const CodeEdit::CaretPosition position = fCore->PositionOfOffset(dropOffset);
		const float x = position.column * fCellWidth;
		const float top = position.row * fLineHeight;
		Invalidate(BRect(x - 1, top, x + fLineHeight / 4 + 1, top + fLineHeight - 1));
	};
	if (fMouse->dropOffset.has_value()) {
		invalidate(*fMouse->dropOffset);
	}
	fMouse->dropOffset = offset;
	if (offset.has_value()) {
		invalidate(*offset);
	}
}


void CodeEditView::HandleDragOver(BPoint where, uint32 transit)
{
	if (transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW) {
		SetDropOffset(std::nullopt);
		StopAutoScroll();
		return;
	}
	fMouse->trackPoint = where - Bounds().LeftTop();
	SetDropOffset(OffsetAtPoint(where));
	UpdateAutoScroll(where);
}


void CodeEditView::HandleDrop(BMessage *message)
{
	using namespace CodeEdit;

	CommitInputMethod();

	SetDropOffset(std::nullopt);
	StopAutoScroll();

	const void *data = nullptr;
	ssize_t size = 0;
	if (message->FindData("text/plain", B_MIME_TYPE, &data, &size) != B_OK) {
		return;
	}
	const std::string text(static_cast<const char*>(data), size);
	const size_t target = OffsetAtPoint(ConvertFromScreen(message->DropPoint()));

	void *originator = nullptr;
	message->FindPointer("be:originator", &originator);
	const bool internal = originator == this && !message->IsSourceRemote();
	const Selection selection = fCore->GetSelection();
	const TextRange source {
		std::min(selection.anchor, selection.caret),
		std::max(selection.anchor, selection.caret)
	};

	if (internal && target >= source.start && target <= source.end) {
		return;
	}
	if (internal && (fMouse->dragButtons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		// Pe: a drag with the secondary button asks whether to move or copy.
		BPopUpMenu *menu = new BPopUpMenu("drop", false, false);
		for (const uint32 what : {kMsgDropMove, kMsgDropCopy}) {
			BMessage *itemMessage = new BMessage(what);
			itemMessage->AddUInt64("offset", target);
			itemMessage->AddData("text", B_MIME_TYPE, text.data(), text.size());
			menu->AddItem(new BMenuItem(what == kMsgDropMove ? "Move" : "Copy", itemMessage));
		}
		menu->SetTargetForItems(this);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(message->DropPoint(), true, true, true);
		return;
	}

	// Within the view a drag moves the text, the tertiary button copies (Pe).
	if (internal && (fMouse->dragButtons & B_TERTIARY_MOUSE_BUTTON) == 0) {
		fCore->MoveText(source, target);
	} else {
		fCore->InsertTextAt(target, text);
	}
	ApplyDirty();
	RestartCaretBlink();
}


void CodeEditView::MakeFocus(bool focus)
{
	if (!focus) {
		CommitInputMethod();
	}
	BView::MakeFocus(focus);
	if (focus) {
		InstallCommandShortcuts();
	} else {
		RemoveCommandShortcuts();
	}
	RestartCaretBlink();
	Invalidate();
}


void CodeEditView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	RestartCaretBlink();
	Invalidate();
}


void CodeEditView::MessageReceived(BMessage *message)
{
	if (message->WasDropped() && message->HasData("text/plain", B_MIME_TYPE)) {
		HandleDrop(message);
		return;
	}
	switch (message->what) {
		case kMsgAutoScroll:
			AutoScrollTick();
			break;
		case kMsgKeyBinding:
			ExecuteCommand(
				static_cast<CodeEdit::EditorCommand>(message->GetInt32("command", 0)),
				message->GetBool("extend", false)
			);
			break;
		case B_INPUT_METHOD_EVENT:
			HandleInputMethodEvent(message);
			break;
		case kMsgDropMove:
		case kMsgDropCopy: {
			const size_t offset = message->GetUInt64("offset", 0);
			if (message->what == kMsgDropMove) {
				const CodeEdit::Selection selection = fCore->GetSelection();
				fCore->MoveText(
					{std::min(selection.anchor, selection.caret), std::max(selection.anchor, selection.caret)},
					offset
				);
			} else {
				const void *data = nullptr;
				ssize_t size = 0;
				if (message->FindData("text", B_MIME_TYPE, &data, &size) == B_OK) {
					fCore->InsertTextAt(offset, std::string_view(static_cast<const char*>(data), size));
				}
			}
			ApplyDirty();
			RestartCaretBlink();
			break;
		}
		case CodeEdit::kMsgModelChanged:
			fCore->Sync();
			ApplyDirty();
			break;
		case kMsgCaretBlink:
			fCaretVisible = !fCaretVisible;
			InvalidateCaret();
			break;
		case B_UNDO:
			ExecuteCommand(CodeEdit::EditorCommand::Undo, false);
			break;
		case B_REDO:
			ExecuteCommand(CodeEdit::EditorCommand::Redo, false);
			break;
		case B_SELECT_ALL:
			ExecuteCommand(CodeEdit::EditorCommand::SelectAll, false);
			break;
		case B_CUT:
			Cut();
			break;
		case B_COPY:
			Copy();
			break;
		case B_PASTE:
			Paste();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


BSize CodeEditView::MinSize()
{
	return BSize(fCellWidth * 10, fLineHeight * 2);
}


BSize CodeEditView::PreferredSize()
{
	return BSize(fCellWidth * 80, fLineHeight * 25);
}
