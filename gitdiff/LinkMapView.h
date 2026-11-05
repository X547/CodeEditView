#pragma once

#include <stddef.h>

#include <optional>
#include <vector>

#include <View.h>


namespace GitDiff {


class DiffView;


// Connects the chunks of the left and right view of a DiffView with filled
// shapes, each with an arrow button that stages or unstages the chunk.
class LinkMapView: public BView {
private:
	struct Shape {
		size_t chunk {};
		BPoint points[4];
		BRect arrow;
		rgb_color color {};
	};

	DiffView *fDiffView;
	std::optional<size_t> fHovered;

	float LineY(int pane, size_t line);
	std::vector<Shape> VisibleShapes();
	std::optional<size_t> ArrowAt(BPoint where);
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


} // GitDiff
