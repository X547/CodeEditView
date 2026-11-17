#pragma once

#include <string>


namespace DiffKit {


// The order of names in Tracker: case-insensitive, numbers by value
// ("file2" before "file10").
bool NameLess(const std::string &a, const std::string &b);


} // DiffKit
