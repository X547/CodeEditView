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


class BMessageRunner;
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


class EditorCore;
class GutterView;
class KeyMap;
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

	float fCellWidth {};
	float fLineHeight {};
	float fAscent {};
	// Y of the top of row 0. Rows inserted or removed above the visible text
	// move it instead of scrolling, so the text on screen stays in place.
	float fOriginY {};
	bool fCaretVisible {};
	bool fRewrapPosted {};

	friend class CodeEditScrollView;
	friend class CodeEdit::GutterView;

	std::pair<float, float> GlyphMetrics(char32_t codePoint, const char *bytes, int32 length);
	void DrawRow(size_t row, const BRect &updateRect, const CodeEdit::RowData &data, const CodeEdit::DrawContext &context);
	void ApplyDirty();
	void InvalidateRows(size_t first, size_t last);
	void InvalidateCaret();
	void RestartCaretBlink();
	void UpdateScrollBars();
	float ContentWidth();
	void UpdateWrapColumns();
	void ScheduleRewrap();
	BPoint ClampScrollPosition(float x, float y);
	void ScrollToClamped(float x, float y);
	void ScrollToCaret(bool keepContext);
	bool CaretInContextArea();
	CodeEdit::PageInfo CurrentPage() const;
	size_t TopRow() const;
	float RowTop(size_t row) const {return fOriginY + row * fLineHeight;}
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

	// Receives the notifications above.
	void SetTarget(const BMessenger &target) {fTarget = target;}
	// Definitions in the text, from the syntax tree; empty without one.
	std::vector<CodeEdit::OutlineItem> Outline();

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
