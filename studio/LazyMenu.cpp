#include "LazyMenu.h"

#include <math.h>

#include <algorithm>

#include <ControlLook.h>
#include <InterfaceDefs.h>


namespace Studio {


static constexpr size_t kBatchSize = 64;


//#pragma mark - LazyMenu

LazyMenu::LazyMenu(const char *name):
	BMenu(name)
{
}


void LazyMenu::RemoveAll()
{
	RemoveItems(0, CountItems(), true);
}


bool LazyMenu::AddDynamicItem(add_state state)
{
	switch (state) {
		case B_INITIAL_ADD:
			RemoveAll();
			fCount = Prepare();
			fNext = 0;
			return true;
		case B_PROCESSING: {
			const size_t end = std::min(fCount, fNext + kBatchSize);
			for (; fNext < end; fNext++) {
				if (BMenuItem *item = MakeItem(fNext)) {
					AddItem(item);
				}
			}
			if (fNext < fCount) {
				return true;
			}
			Release();
			return false;
		}
		case B_ABORT:
			Release();
			RemoveAll();
			return false;
		default:
			return false;
	}
}


//#pragma mark - SectionItem

SectionItem::SectionItem(const char *label):
	BMenuItem(label, nullptr)
{
	SetEnabled(false);
}


void SectionItem::Draw()
{
	BMenu *menu = Menu();
	const BRect frame = Frame();
	const float spacing = be_control_look->DefaultLabelSpacing();
	const float labelWidth = menu->StringWidth(Label());
	const float labelLeft = frame.right - 3 * spacing - labelWidth;
	const float lineY = floorf((frame.top + frame.bottom) / 2);

	const rgb_color background = ui_color(B_MENU_BACKGROUND_COLOR);
	const rgb_color oldHigh = menu->HighColor();
	menu->BeginLineArray(4);
	const auto addLines = [&](float y, rgb_color color) {
		menu->AddLine(BPoint(frame.left, y), BPoint(labelLeft - spacing, y), color);
		menu->AddLine(BPoint(labelLeft + labelWidth + spacing, y), BPoint(frame.right, y), color);
	};
	addLines(lineY, tint_color(background, B_DARKEN_2_TINT));
	addLines(lineY + 1, tint_color(background, B_LIGHTEN_2_TINT));
	menu->EndLineArray();

	font_height fontHeight;
	menu->GetFontHeight(&fontHeight);
	menu->SetHighColor(tint_color(ui_color(B_MENU_ITEM_TEXT_COLOR), B_DISABLED_LABEL_TINT));
	menu->DrawString(Label(), BPoint(labelLeft, ContentLocation().y + ceilf(fontHeight.ascent)));
	menu->SetHighColor(oldHigh);
}


} // Studio
