#include <CodeEditView/EditRecord.h>


namespace CodeEdit {


size_t EditRecord::MapOffset(size_t position, Gravity gravity) const
{
	if (position < offset) {
		return position;
	}
	if (position > OldEnd()) {
		return position - oldLength + newLength;
	}
	if (oldLength > 0) {
		// Boundaries of a replaced range stay on their side.
		if (position == offset) {
			return offset;
		}
		if (position == OldEnd()) {
			return NewEnd();
		}
	}
	return gravity == Gravity::Left ? offset : NewEnd();
}


} // CodeEdit
