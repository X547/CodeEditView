#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Font.h>
#include <Messenger.h>
#include <View.h>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/Marks.h>
#include <CodeEditView/Outline.h>
#include <CodeEditView/TextModel.h>
#include <CodeEditView/Theme.h>
#include <CodeEditView/ViewTypes.h>


class BMessageRunner;
class BScrollBar;
class CodeEditScrollView;


namespace CodeEdit {


// Commands the view accepts as messages, e.g. from menu items.
constexpr uint32 kMsgToggleFold = 'CEtf';
constexpr uint32 kMsgFoldAll = 'CEfa';
constexpr uint32 kMsgUnfoldAll = 'CEua';
constexpr uint32 kMsgIndentLess = 'CEil';
constexpr uint32 kMsgIndentMore = 'CEim';

// Notifications sent to the target (CodeEditView::SetTarget()); "view"
// holds the sending CodeEditView.
// The text or its syntax tree changed, e.g. the outline may be different.
constexpr uint32 kMsgContentsChanged = 'CEcc';
// The caret moved; "caret" (uint64) is its offset.
constexpr uint32 kMsgCaretMoved = 'CEcm';
// The gutter was clicked outside the fold markers: "line" (uint64), and
// "buttons", "modifiers" and "clicks" (int32) of the mouse down.
constexpr uint32 kMsgGutterClicked = 'CEgc';
// The vertical position of lines in the view changed: it scrolled, or rows
// were added or removed (edits, folds, wrapping, resizing). "scrolled" (bool)
// is true if the view scrolled. Bursts are coalesced into one message.
constexpr uint32 kMsgViewportChanged = 'CEvc';
// Posted to the window (not the target) when the caret position shown
// beside the horizontal scroll bar is clicked; the view is focused first.
// Hosts pass it to EditorCommands::Dispatch(), where a go to line command
// (FindKit) handles it.
constexpr uint32 kMsgPositionClicked = 'CEpc';


class EditorCore;
class GutterView;
class KeyMap;
class ScrollProxy;
struct DrawContext;
struct InputMethodState;
struct MouseState;
struct PageInfo;
struct RowData;
enum class EditorCommand;


} // CodeEdit


// Monospace code editor view. Several views may show the same TextModel, also
// from different windows; edits in one view appear in all of them.
class CodeEditView: public BView {
private:
	std::unique_ptr<CodeEdit::EditorCore> fCore;
	std::unique_ptr<CodeEdit::KeyMap> fKeyMap;
	std::unique_ptr<BMessageRunner> fCaretRunner;
	std::unique_ptr<CodeEdit::MouseState> fMouse;
	std::unique_ptr<CodeEdit::InputMethodState> fInputMethod;
	// Per code point: advance and horizontal center of the glyph's ink.
	std::unordered_map<char32_t, std::pair<float, float>> fGlyphCache;
	CodeEdit::Theme fTheme = CodeEdit::Theme::Pe();
	std::map<uint32_t, CodeEdit::MarkStyle> fMarkStyles;
	// Window shortcuts (key, modifiers) installed for Command key bindings.
	std::vector<std::pair<uint32, uint32>> fInstalledShortcuts;
	CodeEditScrollView *fScrollView {};
	BMessenger fTarget;
	size_t fNotifiedCaret = SIZE_MAX;
	CodeEdit::GutterView *fGutter {};
	// Target of the vertical scroll bar, which scrolls it by bounds.
	CodeEdit::ScrollProxy *fScrollProxy {};

	float fCellWidth {};
	float fLineHeight {};
	float fAscent {};
	// The vertical position: the display row at the top of the view and its
	// pixels above the view (less than a line). The bounds stay at y 0, so
	// view coordinates stay small however long the text is. Rows inserted
	// or removed above the view change fTopRow, not what is shown.
	size_t fTopRow {};
	float fTopOffset {};
	bool fSettingScrollBar {};
	bool fCaretVisible {};
	bool fRewrapPosted {};
	bool fViewportNotifyPosted {};
	bool fViewportScrolled {};

	friend class CodeEditScrollView;
	friend class CodeEdit::GutterView;
	friend class CodeEdit::ScrollProxy;

	std::pair<float, float> GlyphMetrics(char32_t codePoint, const char *bytes, int32 length);
	void DrawRow(size_t row, const BRect &updateRect, const CodeEdit::RowData &data, const CodeEdit::DrawContext &context);
	void ApplyDirty();
	void InvalidateRows(size_t first, size_t last);
	void InvalidateCaret();
	void RestartCaretBlink();
	void UpdateScrollBars();
	void UpdateInputMethodFlag();
	void ViewportChanged(bool scrolled);
	float ContentWidth();
	void UpdateWrapColumns();
	void ScheduleRewrap();
	BScrollBar *VerticalScrollBar() const;
	void VerticalScrollBarChanged(float value);
	double ScrollBarScale() const;
	// Document y: pixels from the top of row 0.
	double TopY() const {return static_cast<double>(fTopRow) * fLineHeight + fTopOffset;}
	double MaxTopY() const;
	bool ScrollVertically(double y);
	void ScrollToDocument(float x, double y);
	// y in view coordinates: the text there comes to the top of the view.
	void ScrollToClamped(float x, float y);
	void ScrollToCaret(bool keepContext);
	bool CaretInContextArea();
	CodeEdit::PageInfo CurrentPage() const;
	size_t TopRow() const {return fTopRow;}
	float RowTop(size_t row) const;
	size_t RowAtY(float y) const;
	size_t OffsetAtPoint(BPoint where);
	void ExecuteCommand(CodeEdit::EditorCommand command, bool extend);
	void InstallCommandShortcuts();
	void RemoveCommandShortcuts();
	void Copy();
	void Cut();
	void Paste();

	BPoint ClampToBounds(BPoint where) const;
	bool IsPointInSelection(BPoint where);
	std::optional<size_t> FoldPlaceholderAt(BPoint where);
	void UpdateCursor(BPoint where);
	void RefreshCursor();
	void TrackSelection(BPoint where);
	void UpdateAutoScroll(BPoint where);
	void StopAutoScroll();
	BPoint AutoScrollPressure(BPoint where) const;
	void AutoScrollTick();
	void StartDrag();
	BRect SelectionFrame();
	void SetDropOffset(std::optional<size_t> offset);
	void HandleDragOver(BPoint where, uint32 transit);
	void HandleDrop(BMessage *message);

	bool SpliceInputMethod(CodeEdit::RowData &data, size_t caret);
	void HandleInputMethodEvent(BMessage *message);
	void HandleInputMethodChanged(BMessage *message);
	void SendInputMethodLocation();
	void InvalidateInputMethodRow();
	void CommitInputMethod();

public:
	explicit CodeEditView(const char *name, CodeEdit::TextModel *model = nullptr);
	~CodeEditView() override;

	CodeEdit::TextModel *Model() const;
	// Owner for edit groups made on this view's behalf
	// (TextModel::BeginGroup()): undoing them restores this view's selection.
	CodeEdit::ObserverId EditOwner() const;
	// Shows another model (nullptr: a new empty one). Settings, theme and
	// mark styles stay; selection, folds, decorations, mark and decoration
	// sets and the scroll position start anew.
	void SetModel(CodeEdit::TextModel *model);

	const CodeEdit::EditorSettings &Settings() const;
	void SetSettings(const CodeEdit::EditorSettings &settings);
	void SetEditorFont(const BFont &font);
	const CodeEdit::Theme &GetTheme() const {return fTheme;}
	void SetTheme(const CodeEdit::Theme &theme);
	// Styles per mark kind; the built-in kinds have defaults
	// (DefaultMarkStyle()), other kinds are not shown until set.
	CodeEdit::MarkStyle GetMarkStyle(uint32_t kind) const;
	void SetMarkStyle(uint32_t kind, const CodeEdit::MarkStyle &style);

	CodeEdit::Selection GetSelection();
	// Model line and display column (tabs expanded) of the caret, 0-based.
	CodeEdit::TextPoint CaretLocation();
	void SetSelection(const CodeEdit::Selection &selection);
	void ScrollToSelection();
	// Unfolds and scrolls so that the line is visible with context rows,
	// without moving the caret (e.g. for a debugger's execution line).
	void RevealLine(size_t line);
	// Moves the caret to a line and display column (0-based, clamped to the
	// text), unfolding and scrolling to it.
	void GoToLocation(const CodeEdit::TextPoint &location);

	// Receives the notifications above.
	void SetTarget(const BMessenger &target) {fTarget = target;}
	// Definitions in the text, from the syntax tree; empty without one.
	std::vector<CodeEdit::OutlineItem> Outline();

	// Folds of this view, sorted by header line.
	std::vector<CodeEdit::FoldRange> Folds();
	// Replaces the fold regions of the host. Each region gets a fold marker
	// in the gutter, like the folds of the syntax tree, and those marked
	// folded are folded (FoldSource::Host); user folds stay. The user can
	// unfold and fold them again like any other. Folds starting on the
	// header line of a user fold are left out, and so are folds that would
	// hide the caret or the selection anchor unless foldAtCaret is set
	// (then the caret moves to the header line).
	void SetHostFolds(const std::vector<CodeEdit::HostFold> &folds, bool foldAtCaret = false);

	// Replaces the host decorations. They follow edits until replaced.
	void SetDecorations(std::vector<CodeEdit::Decoration> decorations);

	// Sets of the model shown by this view, besides the model's shared marks
	// (e.g. the execution line of the debug session the view follows). Sets
	// of another model throw std::invalid_argument. Decoration sets are drawn
	// in order, below the host decorations.
	void SetMarkSets(std::vector<BReference<CodeEdit::MarkSet>> sets);
	std::vector<BReference<CodeEdit::MarkSet>> MarkSets() const;
	void SetDecorationSets(std::vector<BReference<CodeEdit::DecorationSet>> sets);
	std::vector<BReference<CodeEdit::DecorationSet>> DecorationSets() const;

	// Vertical geometry of lines in view coordinates. LineTop(line + 1) is
	// the bottom of the line; hidden (folded) lines have no height, and
	// LineTop(LineCount()) is the bottom of the text.
	size_t LineCount();
	float LineTop(size_t line);
	CodeEdit::LinePosition LineAtY(float y);
	// Scrolls so that the position is at viewY from the top of the visible
	// area, and the left edge of the visible area is at left (clamped).
	void ScrollLineTo(const CodeEdit::LinePosition &position, float viewY, float left);

	float LineHeight() const {return fLineHeight;}
	float CellWidth() const {return fCellWidth;}

	void AttachedToWindow() override;
	void DetachedFromWindow() override;
	void Draw(BRect updateRect) override;
	void FrameResized(float width, float height) override;
	void KeyDown(const char *bytes, int32 numBytes) override;
	void MouseDown(BPoint where) override;
	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) override;
	void MouseUp(BPoint where) override;
	void MakeFocus(bool focus = true) override;
	void WindowActivated(bool active) override;
	void MessageReceived(BMessage *message) override;
	void ScrollTo(BPoint where) override;
	BSize MinSize() override;
	BSize PreferredSize() override;
};
