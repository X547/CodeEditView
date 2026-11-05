#pragma once

#include <GraphicsDefs.h>


namespace DiffKit {


// Chunk colours: lines only on the right, only on the left, or changed.
constexpr rgb_color kInsertColor = {40, 170, 60, 255};
constexpr rgb_color kDeleteColor = {210, 50, 50, 255};
constexpr rgb_color kChangeColor = {60, 110, 220, 255};


} // DiffKit
