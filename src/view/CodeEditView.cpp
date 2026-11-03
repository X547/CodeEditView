#include <CodeEditView/CodeEditView.h>


CodeEditView::CodeEditView(const char *name):
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE)
{
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetHighUIColor(B_DOCUMENT_TEXT_COLOR);
}


CodeEditView::~CodeEditView()
{
}


void CodeEditView::Draw(BRect updateRect)
{
	(void)updateRect;
}
