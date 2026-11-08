#pragma once

#include <stddef.h>

#include <vector>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/ViewTypes.h>


namespace CodeEdit {


// Clamps decorations to a text of the length and sorts them by start.
void NormalizeDecorations(std::vector<Decoration> &decorations, size_t length);

// Ranges shrink when text is inserted at their edges; gap lines stay before
// text inserted at them.
void MapDecoration(Decoration &decoration, const EditRecord &edit);


} // CodeEdit
