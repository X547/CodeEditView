#pragma once

#include <stdio.h>
#include <stdlib.h>


namespace CodeEdit {


inline void Assert(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "[CodeEditView] assertion failed: %s\n", message);
		abort();
	}
}


} // CodeEdit
