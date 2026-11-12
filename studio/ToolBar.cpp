#include "ToolBar.h"

#include <math.h>

#include <mutex>

#include <Application.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <Resources.h>


namespace Studio {


// Of the icons at the plain font size of 12 pt.
static constexpr int32 kIconSize = 16;
static constexpr float kArrowWidth = 8;
// Opacity of a disabled icon.
static constexpr float kDisabledOpacity = 0.35f;


static float IconSize()
{
	return be_control_look->ComposeIconSize(kIconSize).Width() + 1;
}


static float ArrowWidth()
{
	return ceilf(kArrowWidth * IconSize() / kIconSize);
}


// The windows create their tool bars on their own threads.
static std::vector<uint8> LoadIconData(const char *name)
{
	static std::mutex sLock;
	std::lock_guard<std::mutex> lock(sLock);
	BResources *resources = BApplication::AppResources();
	size_t size = 0;
	const void *data = resources != nullptr ? resources->LoadResource(B_VECTOR_ICON_TYPE, name, &size) : nullptr;
	if (data == nullptr) {
		return {};
	}
	const uint8 *bytes = static_cast<const uint8 *>(data);
	return std::vector<uint8>(bytes, bytes + size);
}


//#pragma mark - IconMenuItem

IconMenuItem::IconMenuItem(const char *iconName, const char *toolTip, BMessage *message):
	BMenuItem("", message),
	fIconData(LoadIconData(iconName)),
	fToolTip(toolTip)
{
}


IconMenuItem::IconMenuItem(const char *iconName, const char *toolTip, BMenu *menu):
	BMenuItem(menu),
	fIconData(LoadIconData(iconName)),
	fToolTip(toolTip)
{
}


IconMenuItem::~IconMenuItem()
{
}


// The icon and the icon faded, unless they have the size.
void IconMenuItem::RenderIcons(float size)
{
	if (fIconData.empty() || (fIcon != nullptr && fIcon->Bounds().Width() + 1 == size)) {
		return;
	}
	fIcon.reset();
	fDisabledIcon.reset();
	const BRect bounds(0, 0, size - 1, size - 1);
	auto icon = std::make_unique<BBitmap>(bounds, B_RGBA32);
	if (icon->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon(fIconData.data(), fIconData.size(), icon.get()) != B_OK) {
		return;
	}
	auto disabled = std::make_unique<BBitmap>(icon.get());
	if (disabled->InitCheck() == B_OK) {
		uint8 *row = static_cast<uint8 *>(disabled->Bits());
		const int32 width = bounds.IntegerWidth() + 1;
		const int32 height = bounds.IntegerHeight() + 1;
		for (int32 y = 0; y < height; y++, row += disabled->BytesPerRow()) {
			for (int32 x = 0; x < width; x++) {
				uint8 &alpha = row[x * 4 + 3];
				alpha = static_cast<uint8>(alpha * kDisabledOpacity);
			}
		}
		fDisabledIcon = std::move(disabled);
	}
	fIcon = std::move(icon);
}


void IconMenuItem::SetPressed(bool pressed)
{
	if (pressed == fPressed) {
		return;
	}
	fPressed = pressed;
	if (Menu() != nullptr) {
		Menu()->Invalidate(Frame());
	}
}


void IconMenuItem::GetContentSize(float *width, float *height)
{
	const float size = IconSize();
	*width = size;
	*height = size;
	if (Submenu() != nullptr) {
		*width += ArrowWidth();
	}
}


void IconMenuItem::DrawContent()
{
	BMenu *menu = Menu();
	const float size = IconSize();
	RenderIcons(size);
	menu->PushState();
	// Of the label: the arrow has it.
	const rgb_color textColor = menu->HighColor();

	if (fPressed) {
		const BRect frame = Frame();
		BRect button = frame.InsetByCopy(1, 1);
		const rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);
		const uint32 flags = BControlLook::B_ACTIVATED | (IsEnabled() ? 0 : BControlLook::B_DISABLED);
		be_control_look->DrawButtonFrame(menu, button, frame, base, base, flags);
		be_control_look->DrawButtonBackground(menu, button, frame, base, flags);
	}

	BRect frame = Frame();
	if (Submenu() != nullptr) {
		frame.right -= ArrowWidth();
	}
	const BPoint center((frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2);
	if (Submenu() != nullptr) {
		// The arrow of the ToolBar demo, scaled with the icon.
		if ((menu->Flags() & B_SUBPIXEL_PRECISE) == 0) {
			menu->SetFlags(menu->Flags() | B_SUBPIXEL_PRECISE);
		}
		const float scale = size / kIconSize;
		const BPoint origin = BPoint(frame.right, center.y) + BPoint(0.5, 0.5);
		const BPoint points[] = {
			origin + BPoint(0.5 * scale, -1 * scale),
			origin + BPoint(6.5 * scale, -1 * scale),
			origin + BPoint(3.5 * scale, 2 * scale),
		};
		menu->SetHighColor(textColor);
		menu->FillPolygon(points, 3);
	}

	const BBitmap *icon = IsEnabled() || fDisabledIcon == nullptr ? fIcon.get() : fDisabledIcon.get();
	if (icon != nullptr) {
		menu->SetDrawingMode(B_OP_ALPHA);
		menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		// Whole pixels: the menu draws with subpixel precision.
		const BPoint where(
			floorf(center.x - icon->Bounds().Width() / 2),
			floorf(center.y - icon->Bounds().Height() / 2)
		);
		menu->DrawBitmap(icon, where);
	}
	menu->PopState();
}


//#pragma mark - ToolBar

ToolBar::ToolBar(const char *name):
	BMenuBar(name, B_ITEMS_IN_ROW)
{
	SetItemMargins(3, 3, 2, 2);
}


ToolBar::~ToolBar()
{
}


BMenuItem *ToolBar::ItemAtPoint(BPoint where) const
{
	for (int32 i = 0; BMenuItem *item = ItemAt(i); i++) {
		if (item->Frame().Contains(where)) {
			return item;
		}
	}
	return nullptr;
}


void ToolBar::MouseDown(BPoint where)
{
	HideToolTip();
	if (fMenusBeginning) {
		fMenusBeginning();
	}
	BMenuBar::MouseDown(where);
}


void ToolBar::MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage)
{
	BMenuItem *item = transit == B_EXITED_VIEW ? nullptr : ItemAtPoint(where);
	if (item != fToolTipItem) {
		HideToolTip();
		fToolTipItem = item;
	}
	BMenuBar::MouseMoved(where, transit, dragMessage);
}


bool ToolBar::GetToolTipAt(BPoint point, BToolTip **_tip)
{
	const IconMenuItem *item = dynamic_cast<IconMenuItem *>(ItemAtPoint(point));
	if (item == nullptr || item->ToolTip().empty()) {
		SetToolTip(static_cast<const char *>(nullptr));
		return false;
	}
	SetToolTip(item->ToolTip().c_str());
	return BMenuBar::GetToolTipAt(point, _tip);
}


} // Studio
