#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Font.h>
#include <View.h>

#include <CodeEditView/EditorSettings.h>
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
	// Window shortcuts (key, modifiers) installed for Command key bindings.
	std::vector<std::pair<uint32, uint32>> fInstalledShortcuts;
	CodeEditScrollView *fScrollView {};
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

	CodeEdit::Selection GetSelection();
	// Model line and display column (tabs expanded) of the caret, 0-based.
	CodeEdit::TextPoint CaretLocation();
	void SetSelection(const CodeEdit::Selection &selection);
	void ScrollToSelection();

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
