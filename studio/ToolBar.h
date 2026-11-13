#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Bitmap.h>
#include <MenuBar.h>
#include <MenuItem.h>


namespace Studio {


// A menu bar item showing an icon instead of a label, as a button or, with
// a submenu, a drop-down button with an arrow beside the icon. A pressed
// item shows a state that is on.
class IconMenuItem: public BMenuItem {
private:
	// The vector icon; empty if the resources have none.
	std::vector<uint8> fIconData;
	// Rendered at the size of the plain font.
	std::unique_ptr<BBitmap> fIcon;
	std::unique_ptr<BBitmap> fDisabledIcon;
	std::string fToolTip;
	bool fPressed {};

	void RenderIcons(float size);

protected:
	void GetContentSize(float *width, float *height) override;
	void DrawContent() override;

public:
	// iconName: a vector icon resource of the application.
	IconMenuItem(const char *iconName, const char *toolTip, BMessage *message);
	IconMenuItem(const char *iconName, const char *toolTip, BMenu *menu);
	~IconMenuItem() override;

	const std::string &ToolTip() const {return fToolTip;}
	bool IsPressed() const {return fPressed;}
	void SetPressed(bool pressed);
};


// A row of icon menu items below the menu bar of a window. Hovering over an
// item shows its tool tip.
class ToolBar: public BMenuBar {
private:
	BMenuItem *fToolTipItem {};

	BMenuItem *ItemAtPoint(BPoint where) const;

public:
	explicit ToolBar(const char *name);
	~ToolBar() override;

	void MouseDown(BPoint where) override;
	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) override;
	bool GetToolTipAt(BPoint point, BToolTip **_tip) override;
};


} // Studio
