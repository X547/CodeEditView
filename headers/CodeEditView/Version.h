#pragma once

#include <stdint.h>


namespace CodeEdit {


struct Version {
	int32_t major;
	int32_t minor;
	int32_t patch;
};


Version LibraryVersion();
uint32_t TreeSitterLanguageVersion();


} // CodeEdit
