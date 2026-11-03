#include <CodeEditView/Version.h>

#include <tree_sitter/api.h>


namespace CodeEdit {


Version LibraryVersion()
{
	return {0, 1, 0};
}


uint32_t TreeSitterLanguageVersion()
{
	return TREE_SITTER_LANGUAGE_VERSION;
}


} // CodeEdit
