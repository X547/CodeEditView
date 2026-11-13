#include <math.h>

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <Application.h>
#include <Autolock.h>
#include <ScrollBar.h>
#include <Window.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


static constexpr size_t kLineCount = 2000000;


// Views need an application; it is never run.
static void EnsureApplication()
{
	if (be_app == nullptr) {
		new BApplication("application/x-vnd.CodeEditView-ScrollTest");
	}
}


// An editor in a window that is never shown, with a text of kLineCount lines
// "line N".
class EditorWindow {
private:
	BWindow *fWindow;

public:
	CodeEditView *view;
	BScrollBar *verticalBar;

	EditorWindow()
	{
		EnsureApplication();
		BReference<TextModel> model(new TextModel(), true);
		{
			std::string text;
			for (size_t line = 0; line < kLineCount; line++) {
				text += "line " + std::to_string(line) + "\n";
			}
			ModelLock lock(*model);
			model->SetText(text);
		}
		fWindow = new BWindow(BRect(0, 0, 599, 399), "ScrollTest", B_DOCUMENT_WINDOW, 0);
		view = new CodeEditView("editor", model.Get());
		CodeEditScrollView *scrollView = new CodeEditScrollView("scroll", view);
		scrollView->SetResizingMode(B_FOLLOW_ALL);
		scrollView->ResizeTo(fWindow->Bounds().Width(), fWindow->Bounds().Height());
		fWindow->AddChild(scrollView);
		verticalBar = dynamic_cast<BScrollBar*>(fWindow->FindView("verticalScrollBar"));
		fWindow->Run();
	}

	~EditorWindow()
	{
		fWindow->Lock();
		fWindow->Quit();
	}

	BWindow *Window() const {return fWindow;}
};


TEST_CASE("the view scrolls exactly far into a long text")
{
	EditorWindow editor;
	BAutolock lock(editor.Window());
	CodeEditView &view = *editor.view;
	REQUIRE(editor.verticalBar != nullptr);
	CHECK(view.Bounds().top == 0);

	// Past 2^24 pixels, where view coordinates could not hold every row.
	const size_t target = 1900000;
	view.GoToLocation({target, 0});
	const float top = view.LineTop(target);
	CHECK(top >= 0);
	CHECK(top + view.LineHeight() <= view.Bounds().Height() + 1);
	CHECK(top == floorf(top / view.LineHeight()) * view.LineHeight());
	CHECK(view.Bounds().top == 0);

	// The scroll bar is mapped onto a range a float holds exactly.
	float minimum;
	float maximum;
	editor.verticalBar->GetRange(&minimum, &maximum);
	CHECK(minimum == 0);
	CHECK(maximum <= 8 * 1024 * 1024);
	const float value = editor.verticalBar->Value();
	CHECK(fabsf(value / maximum - static_cast<float>(target) / kLineCount) < 0.01f);

	// Moving the scroll bar scrolls the view.
	editor.verticalBar->SetValue(maximum);
	CHECK(view.LineAtY(view.Bounds().bottom).line == kLineCount);
	editor.verticalBar->SetValue(0);
	CHECK(view.LineAtY(0).line == 0);
}


TEST_CASE("lines inserted above the view keep the text at its top")
{
	EditorWindow editor;
	BAutolock lock(editor.Window());
	CodeEditView &view = *editor.view;
	view.ScrollLineTo({1000, 0}, 0, 0);
	CHECK(view.LineAtY(0).line == 1000);

	{
		ModelLock modelLock(*view.Model());
		view.Model()->Insert(0, "new\nlines\n");
	}
	BMessage changed(kMsgModelChanged);
	view.MessageReceived(&changed);
	CHECK(view.LineAtY(0).line == 1002);
	CHECK(view.LineTop(1002) == 0);
}


TEST_CASE("the mouse wheel scrolls by rows")
{
	EditorWindow editor;
	BAutolock lock(editor.Window());
	CodeEditView &view = *editor.view;
	view.ScrollLineTo({500, 0}, 0, 0);

	BMessage wheel(B_MOUSE_WHEEL_CHANGED);
	wheel.AddFloat("be:wheel_delta_x", 0);
	wheel.AddFloat("be:wheel_delta_y", 2);
	view.MessageReceived(&wheel);
	CHECK(view.LineAtY(0).line == 506);
	CHECK(view.LineTop(506) == 0);

	wheel.ReplaceFloat("be:wheel_delta_y", -1);
	view.MessageReceived(&wheel);
	CHECK(view.LineAtY(0).line == 503);
	// The text is taller than the scroll bar's exact range: its value is
	// scaled.
	float minimum;
	float maximum;
	editor.verticalBar->GetRange(&minimum, &maximum);
	const double maxTop = (kLineCount + 1.0) * view.LineHeight() - (view.Bounds().Height() + 1);
	CHECK(fabs(editor.verticalBar->Value() - 503.0 * view.LineHeight() * maximum / maxTop) < 1);
}


} // CodeEdit
