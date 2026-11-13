#pragma once

#include <stddef.h>

#include <Menu.h>
#include <MenuItem.h>


namespace Studio {


// A menu whose items are made each time it opens and added a batch at a
// time, as Tracker fills its folder menus: nothing is made until the menu
// is shown, and moving away from a long menu stops the filling.
class LazyMenu: public BMenu {
private:
	size_t fCount {};
	size_t fNext {};

	void RemoveAll();

protected:
	// Prepares the items of this opening; returns their number.
	virtual size_t Prepare() = 0;
	// The item at index (of those prepared); nullptr for none.
	virtual BMenuItem *MakeItem(size_t index) = 0;
	// Drops what Prepare() kept.
	virtual void Release() {}

public:
	explicit LazyMenu(const char *name);

	bool AddDynamicItem(add_state state) final;
};


// A separator line with the name of a section at its right end, as Pe shows
// "#pragma mark" in its function menu.
class SectionItem: public BMenuItem {
protected:
	void Draw() final;

public:
	explicit SectionItem(const char *label);
};


} // Studio
