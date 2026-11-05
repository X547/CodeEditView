#pragma once

#include <stddef.h>

#include <optional>
#include <utility>
#include <vector>

#include <View.h>


namespace DiffKit {


class DiffView;


// Connects the chunks of the left and right view of a DiffView with filled
// shapes. Arrow buttons copy a chunk: one on the left edge pointing right
// copies it to the right pane, one on the right edge pointing left copies it
// to the left pane; which ones exist depends on DiffView::CanCopyTo().
class LinkMapView: public BView {
private:
	struct Shape {
		size_t chunk {};
		BPoint points[4];
		// By target pane: the arrow on the source side.
		BRect arrows[2];
		rgb_color color {};
	};

	// A chunk and the pane its arrow copies to.
	using Arrow = std::pair<size_t, int>;

	DiffView *fDiffView;
	std::optional<Arrow> fHovered;

	float LineY(int pane, size_t line);
	std::vector<Shape> VisibleShapes();
	std::optional<Arrow> ArrowAt(BPoint where);
	void DrawArrow(BRect frame, bool pointsRight, rgb_color color, bool hovered);

public:
	explicit LinkMapView(DiffView *diffView);

	void Draw(BRect updateRect) override;
	void MouseDown(BPoint where) override;
	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) override;
	BSize MinSize() override;
	BSize MaxSize() override;
	BSize PreferredSize() override;
};


} // DiffKit
