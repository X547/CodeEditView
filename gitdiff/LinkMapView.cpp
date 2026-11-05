#include "LinkMapView.h"

#include <math.h>

#include <algorithm>

#include <Window.h>

#include <CodeEditView/CodeEditView.h>

#include "DiffView.h"
#include "Messages.h"


namespace GitDiff {


static constexpr float kWidth = 36;
static constexpr float kArrowSize = 14;

static constexpr rgb_color kInsertColor = {40, 170, 60, 255};
static constexpr rgb_color kDeleteColor = {210, 50, 50, 255};
static constexpr rgb_color kChangeColor = {60, 110, 220, 255};


LinkMapView::LinkMapView(DiffView *diffView):
	BView("linkMap", B_WILL_DRAW | B_FRAME_EVENTS),
	fDiffView(diffView)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


// Y of the top of a line of a view, in this view's coordinates.
float LinkMapView::LineY(int pane, size_t line)
{
	CodeEditView *view = fDiffView->View(pane);
	const BPoint screen = view->ConvertToScreen(BPoint(0, view->LineTop(line)));
	return ConvertFromScreen(screen).y;
}


std::vector<LinkMapView::Shape> LinkMapView::VisibleShapes()
{
	std::vector<Shape> shapes;
	const BRect bounds = Bounds();
	const std::vector<Chunk> &chunks = fDiffView->Chunks();
	const bool arrowOnLeft = fDiffView->Kind() == DiffKind::Staged;
	for (size_t i = 0; i < chunks.size(); i++) {
		const Chunk &chunk = chunks[i];
		const float leftTop = LineY(0, chunk.aFirst);
		const float rightTop = LineY(1, chunk.bFirst);
		if (leftTop > bounds.bottom && rightTop > bounds.bottom) {
			// Chunks are sorted.
			break;
		}
		// Chunks without lines on a side are a line at their position.
		const float leftBottom = std::max(LineY(0, chunk.aFirst + chunk.aCount), leftTop + 1);
		const float rightBottom = std::max(LineY(1, chunk.bFirst + chunk.bCount), rightTop + 1);
		if (leftBottom < bounds.top && rightBottom < bounds.top) {
			continue;
		}

		Shape shape;
		shape.chunk = i;
		shape.points[0] = BPoint(bounds.left, leftTop);
		shape.points[1] = BPoint(bounds.right, rightTop);
		shape.points[2] = BPoint(bounds.right, rightBottom - 1);
		shape.points[3] = BPoint(bounds.left, leftBottom - 1);
		if (chunk.aCount == 0) {
			shape.color = kInsertColor;
		} else if (chunk.bCount == 0) {
			shape.color = kDeleteColor;
		} else {
			shape.color = kChangeColor;
		}

		// The arrow sits on the source side and points to the index.
		const float sourceTop = arrowOnLeft ? leftTop : rightTop;
		const float sourceBottom = arrowOnLeft ? leftBottom : rightBottom;
		const float centerY = floorf((sourceTop + sourceBottom) / 2);
		const float left = arrowOnLeft ? bounds.left + 2 : bounds.right - 2 - kArrowSize;
		shape.arrow = BRect(left, centerY - kArrowSize / 2, left + kArrowSize, centerY + kArrowSize / 2);
		shapes.push_back(shape);
	}
	return shapes;
}


void LinkMapView::DrawArrow(BRect frame, bool pointsRight, rgb_color color, bool hovered)
{
	const float centerY = (frame.top + frame.bottom) / 2;
	const float half = frame.Height() / 2;
	const float shaft = roundf(half * 0.35f);
	const float tipX = pointsRight ? frame.right : frame.left;
	const float baseX = pointsRight ? frame.right - half : frame.left + half;
	const float tailX = pointsRight ? frame.left : frame.right;
	const BPoint points[] = {
		BPoint(tailX, centerY - shaft), BPoint(baseX, centerY - shaft),
		BPoint(baseX, centerY - half), BPoint(tipX, centerY),
		BPoint(baseX, centerY + half), BPoint(baseX, centerY + shaft),
		BPoint(tailX, centerY + shaft)
	};
	const rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(hovered ? color : mix_color(background, color, 200));
	FillPolygon(points, 7);
	SetHighColor(tint_color(color, B_DARKEN_2_TINT));
	StrokePolygon(points, 7);
}


void LinkMapView::Draw(BRect updateRect)
{
	const rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(background);
	FillRect(updateRect);
	if (fDiffView->Document() == nullptr) {
		return;
	}

	SetDrawingMode(B_OP_OVER);
	const bool pointsRight = fDiffView->Kind() == DiffKind::Staged;
	const bool editable = fDiffView->Document()->ReadOnlyReason().empty();
	for (const Shape &shape : VisibleShapes()) {
		SetHighColor(mix_color(background, shape.color, 80));
		FillPolygon(shape.points, 4);
		SetHighColor(mix_color(background, shape.color, 180));
		StrokeLine(shape.points[0], shape.points[1]);
		StrokeLine(shape.points[3], shape.points[2]);
		if (editable) {
			DrawArrow(shape.arrow, pointsRight, shape.color, fHovered == shape.chunk);
		}
	}
}


std::optional<size_t> LinkMapView::ArrowAt(BPoint where)
{
	if (fDiffView->Document() == nullptr || !fDiffView->Document()->ReadOnlyReason().empty()) {
		return std::nullopt;
	}
	for (const Shape &shape : VisibleShapes()) {
		if (shape.arrow.InsetByCopy(-2, -2).Contains(where)) {
			return shape.chunk;
		}
	}
	return std::nullopt;
}


void LinkMapView::MouseDown(BPoint where)
{
	const std::optional<size_t> chunk = ArrowAt(where);
	if (!chunk.has_value()) {
		return;
	}
	BMessage message(kMsgTransferChunk);
	message.AddInt32("chunk", static_cast<int32>(*chunk));
	Window()->PostMessage(&message, fDiffView);
}


void LinkMapView::MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage)
{
	(void)dragMessage;
	const std::optional<size_t> hovered = transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW
		? std::nullopt
		: ArrowAt(where);
	if (hovered != fHovered) {
		fHovered = hovered;
		Invalidate();
	}
}


BSize LinkMapView::MinSize()
{
	return BSize(kWidth, 0);
}


BSize LinkMapView::MaxSize()
{
	return BSize(kWidth, B_SIZE_UNLIMITED);
}


BSize LinkMapView::PreferredSize()
{
	return BSize(kWidth, 100);
}


} // GitDiff
