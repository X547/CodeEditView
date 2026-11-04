#include "DemoWindow.h"

#include <exception>
#include <string>

#include <Alert.h>
#include <Application.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <SplitView.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/Language.h>

#include "OutlineView.h"


DemoDocument::DemoDocument():
	model(new CodeEdit::TextModel(), true)
{
}


void ShowError(const char *message)
{
	BAlert *alert = new BAlert("CodeEditDemo", message, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(nullptr);
}


DemoWindow::DemoWindow(DemoDocument *document):
	BWindow(
		BRect(100, 100, 899, 699),
		"CodeEditDemo",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fDocument(document)
{
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("New", kMsgNewDocument, 'N')
			.AddItem("Open" B_UTF8_ELLIPSIS, kMsgOpenDocument, 'O')
			.AddItem("New view of this document", kMsgNewView, 'N', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Save", kMsgSave, 'S')
			.AddItem("Save as" B_UTF8_ELLIPSIS, kMsgSaveAs, 'S', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", kMsgQuitApplication, 'Q')
		.End()
		.AddMenu("Edit")
			.AddItem("Undo", B_UNDO, 'Z')
			.AddItem("Redo", B_REDO, 'Z', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Cut", B_CUT, 'X')
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Paste", B_PASTE, 'V')
			.AddSeparator()
			.AddItem("Select all", B_SELECT_ALL, 'A')
			.AddSeparator()
			.AddItem("Shift left", CodeEdit::kMsgIndentLess, '[')
			.AddItem("Shift right", CodeEdit::kMsgIndentMore, ']')
		.End()
		.AddMenu("View")
			.AddItem("Split view", kMsgToggleSplit, 'T', B_SHIFT_KEY)
			.AddItem("Word wrap", kMsgToggleWordWrap)
				.GetItem(fWordWrapItem)
			.AddItem("Outline", kMsgToggleOutline, 'L', B_SHIFT_KEY)
				.GetItem(fOutlineItem)
			.AddSeparator()
			.AddItem("Fold/unfold", CodeEdit::kMsgToggleFold, '.')
			.AddItem("Fold all", CodeEdit::kMsgFoldAll)
			.AddItem("Unfold all", CodeEdit::kMsgUnfoldAll)
		.End();

	CodeEditScrollView *pane = CreatePane();
	fFirstPane = pane;
	fActiveView = pane->EditView();
	fSplitView = new BSplitView(B_VERTICAL, 0);
	fOutline = new OutlineView();
	fMainSplitView = new BSplitView(B_HORIZONTAL, 0);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frame of the edit pane overlaps the menu bar and window borders.
			.SetInsets(-1, -1, -1, -1)
			.Add(fMainSplitView)
		.End();
	fMainSplitView->AddChild(new BScrollView("outlineScroll", fOutline, 0, false, true, B_PLAIN_BORDER), 1);
	fMainSplitView->AddChild(fSplitView, 3);
	fSplitView->AddChild(pane);
	fOutlineItem->SetMarked(true);

	pane->EditView()->MakeFocus(true);
	PostMessage(kMsgRefreshOutline);
	UpdateTitle();
	be_app->PostMessage(kMsgWindowOpened);
}


DemoWindow::~DemoWindow()
{
	be_app->PostMessage(kMsgWindowClosed);
}


CodeEditScrollView *DemoWindow::CreatePane()
{
	CodeEditView *editView = new CodeEditView("editView", fDocument->model.Get());
	editView->SetTarget(BMessenger(this));
	ApplySettings(editView);
	return new CodeEditScrollView("pane", editView);
}


void DemoWindow::ApplySettings(CodeEditView *editView)
{
	CodeEdit::EditorSettings settings = editView->Settings();
	settings.wordWrap = fWordWrap;
	editView->SetSettings(settings);
}


// Edits come in bursts (e.g. undo of a large change); refresh once.
void DemoWindow::ScheduleOutlineRefresh()
{
	if (!fOutlineRefreshPending) {
		fOutlineRefreshPending = true;
		PostMessage(kMsgRefreshOutline);
	}
}


void DemoWindow::RefreshOutline()
{
	if (fActiveView == nullptr || !fOutlineItem->IsMarked()) {
		return;
	}
	fOutline->SetOutline(fActiveView->Outline());
	fOutline->SelectAt(fActiveView->GetSelection().caret);
}


void DemoWindow::JumpToOutlineItem(bool focus)
{
	const OutlineListItem *item = fOutline->SelectedItem();
	if (item == nullptr || fActiveView == nullptr) {
		return;
	}
	fActiveView->SetSelection({item->NameStart(), item->NameEnd()});
	fActiveView->ScrollToSelection();
	if (focus) {
		fActiveView->MakeFocus(true);
	}
}


void DemoWindow::UpdateTitle()
{
	if (fDocument->path.InitCheck() == B_OK) {
		SetTitle(fDocument->path.Leaf());
	} else {
		SetTitle("Untitled");
	}
}


void DemoWindow::Save()
{
	if (fDocument->path.InitCheck() != B_OK) {
		SaveAs();
		return;
	}
	BFile file(fDocument->path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		ShowError((std::string("Cannot write ") + fDocument->path.Path()).c_str());
		return;
	}
	try {
		CodeEdit::ModelLock lock(*fDocument->model);
		fDocument->model->Save(file);
		fDocument->model->SetSavePoint();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	UpdateTitle();
}


void DemoWindow::SaveAs()
{
	if (fSavePanel == nullptr) {
		BMessenger target(this);
		fSavePanel = std::make_unique<BFilePanel>(B_SAVE_PANEL, &target);
	}
	fSavePanel->Show();
}


void DemoWindow::ToggleSplit()
{
	if (fSecondPane != nullptr) {
		if (fActiveView == fSecondPane->EditView()) {
			fActiveView = fFirstPane->EditView();
		}
		fSecondPane->RemoveSelf();
		delete fSecondPane;
		fSecondPane = nullptr;
		return;
	}
	fSecondPane = CreatePane();
	fSplitView->AddChild(fSecondPane);
	fSecondPane->EditView()->MakeFocus(true);
}


void DemoWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgNewDocument:
		case kMsgOpenDocument:
		case kMsgQuitApplication:
			be_app->PostMessage(message->what == kMsgQuitApplication ? static_cast<uint32>(B_QUIT_REQUESTED) : message->what);
			break;
		case kMsgNewView:
			(new DemoWindow(fDocument.Get()))->Show();
			break;
		case kMsgToggleSplit:
			ToggleSplit();
			break;
		case kMsgToggleOutline: {
			const bool visible = !fOutlineItem->IsMarked();
			fOutlineItem->SetMarked(visible);
			fMainSplitView->SetItemCollapsed(0, !visible);
			if (visible) {
				ScheduleOutlineRefresh();
			}
			break;
		}
		case CodeEdit::kMsgContentsChanged: {
			void *view = nullptr;
			if (message->FindPointer("view", &view) == B_OK && view == fActiveView) {
				ScheduleOutlineRefresh();
			}
			break;
		}
		case CodeEdit::kMsgCaretMoved: {
			void *pointer = nullptr;
			message->FindPointer("view", &pointer);
			CodeEditView *view = static_cast<CodeEditView*>(pointer);
			if (view == nullptr || (view != fActiveView && !view->IsFocus())) {
				break;
			}
			if (view != fActiveView) {
				fActiveView = view;
				ScheduleOutlineRefresh();
			}
			fOutline->SelectAt(message->GetUInt64("caret", 0));
			break;
		}
		case kMsgRefreshOutline:
			fOutlineRefreshPending = false;
			RefreshOutline();
			break;
		case kMsgOutlineSelected:
			JumpToOutlineItem(false);
			break;
		case kMsgOutlineInvoked:
			JumpToOutlineItem(true);
			break;
		case kMsgToggleWordWrap: {
			fWordWrap = !fWordWrap;
			fWordWrapItem->SetMarked(fWordWrap);
			for (int32 i = 0; i < fSplitView->CountChildren(); i++) {
				CodeEditScrollView *pane = dynamic_cast<CodeEditScrollView*>(fSplitView->ChildAt(i));
				if (pane != nullptr) {
					ApplySettings(pane->EditView());
				}
			}
			break;
		}
		case kMsgSave:
			Save();
			break;
		case kMsgSaveAs:
			SaveAs();
			break;
		case B_SAVE_REQUESTED: {
			entry_ref directory;
			const char *name = nullptr;
			if (message->FindRef("directory", &directory) != B_OK
				|| message->FindString("name", &name) != B_OK) {
				break;
			}
			BPath path(&directory);
			path.Append(name);
			fDocument->path = path;
			{
				CodeEdit::ModelLock lock(*fDocument->model);
				fDocument->model->SetLanguage(CodeEdit::LanguageForPath(path.Path()));
			}
			Save();
			break;
		}
		case CodeEdit::kMsgToggleFold:
		case CodeEdit::kMsgFoldAll:
		case CodeEdit::kMsgUnfoldAll:
		case CodeEdit::kMsgIndentLess:
		case CodeEdit::kMsgIndentMore:
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL: {
			BView *focus = CurrentFocus();
			if (dynamic_cast<CodeEditView*>(focus) != nullptr) {
				PostMessage(message, focus);
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
