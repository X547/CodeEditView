#include <CodeEditView/CodeEditView.h>

#include <math.h>

#include <algorithm>
#include <string>
#include <vector>

#include <Clipboard.h>
#include <MessageRunner.h>
#include <ScrollBar.h>
#include <Window.h>

#include <CodeEditView/CodeEditScrollView.h>

#include "EditorCore.h"
#include "GutterView.h"
#include "KeyMap.h"
#include "unicode/CharWidth.h"
#include "unicode/Utf8.h"


static constexpr uint32 kMsgCaretBlink = 'CEcb';
static constexpr bigtime_t kCaretBlinkInterval = 500000;
static constexpr float kHorizontalScrollMargin = 4;


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
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fCore(std::make_unique<CodeEdit::EditorCore>(CodeEdit::ModelReference(model))),
	fKeyMap(std::make_unique<CodeEdit::KeyMap>(CodeEdit::KeyMap::Default()))
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

	const size_t firstRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.top / fLineHeight)));
	const size_t lastRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.bottom / fLineHeight)));
	const std::vector<RowData> rows = fCore->Rows(firstRow, lastRow - firstRow + 1);

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

	SetHighColor(context.text);
	for (const Cluster &cluster : data.layout.Clusters()) {
		if (cluster.EndColumn() < firstColumn) {
			continue;
		}
		if (cluster.column > lastColumn) {
			break;
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
				DrawString(PlaceholderText(cluster).c_str(), BPoint(x, baseline));
				SetHighColor(context.text);
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

	if (isCaretRow && context.drawCaret) {
		const float caretWidth = std::max(1.0f, roundf(fLineHeight / 16));
		const float x = context.caretColumn * fCellWidth;
		SetHighColor(context.text);
		FillRect(BRect(x, top, x + caretWidth - 1, bottom));
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
	}
	if (dirty.marksChanged && fGutter != nullptr) {
		fGutter->Invalidate();
	}
}


//#pragma mark - Commands

void CodeEditView::ExecuteCommand(CodeEdit::EditorCommand command, bool extend)
{
	using namespace CodeEdit;

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
		default:
			break;
	}

	ApplyDirty();
	if (scrollToCaret) {
		ScrollToCaret(keepContext);
	}
	RestartCaretBlink();
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
	Copy();
	fCore->DeleteSelection();
	ApplyDirty();
	ScrollToCaret(false);
	RestartCaretBlink();
}


void CodeEditView::Paste()
{
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
	fCaretRunner.reset();
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


void CodeEditView::MouseDown(BPoint where)
{
	MakeFocus(true);
	BMessage *message = Window()->CurrentMessage();
	const uint32 modifiers = message != nullptr ? message->GetInt32("modifiers", 0) : ::modifiers();
	const size_t offset = OffsetAtPoint(where);
	const CodeEdit::Selection selection = fCore->GetSelection();
	const bool extend = (modifiers & B_SHIFT_KEY) != 0;
	fCore->SetSelection({extend ? selection.anchor : offset, offset});
	ApplyDirty();
	RestartCaretBlink();
}


void CodeEditView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
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
	switch (message->what) {
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
