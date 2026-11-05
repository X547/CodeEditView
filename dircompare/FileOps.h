#pragma once

#include <string>
#include <vector>


namespace DirCompare {


// Copies an entry (file, symlink or directory with its contents) to target.
// Files keep their data, permissions, modification time and attributes; a
// file is replaced through a temporary file. A target of another type is
// deleted first; a directory is merged into an existing one (entries are
// replaced, other entries of the target are kept). Missing parent
// directories of target are created. Errors are added to errors; returns
// whether there were none.
bool CopyEntry(const std::string &source, const std::string &target, std::vector<std::string> &errors);

// Deletes an entry; directories with their contents. Links are not
// followed.
bool DeleteEntry(const std::string &path, std::vector<std::string> &errors);


} // DirCompare
