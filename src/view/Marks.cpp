#include <CodeEditView/Marks.h>

#include <CodeEditView/TextModel.h>


namespace CodeEdit {


MarkStyle DefaultMarkStyle(uint32_t kind)
{
	switch (kind) {
		case kMarkBookmark:
			return {MarkShape::Bookmark, {60, 120, 220, 255}, {}, 10};
		case kMarkBreakpoint:
			return {MarkShape::Circle, {215, 40, 40, 255}, {215, 40, 40, 40}, 20};
		case kMarkBreakpointDisabled:
			return {MarkShape::CircleOutline, {215, 40, 40, 255}, {}, 15};
		case kMarkExecLine:
			return {MarkShape::Arrow, {245, 190, 0, 255}, {255, 210, 0, 96}, 30};
		default:
			break;
	}
	return {};
}


} // CodeEdit
