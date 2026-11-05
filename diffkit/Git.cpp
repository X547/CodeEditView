#include "Git.h"


namespace DiffKit {


void Check(int result, const char *what)
{
	if (result >= 0) {
		return;
	}
	const git_error *error = git_error_last();
	std::string message = what;
	if (error != nullptr && error->message != nullptr) {
		message += ": ";
		message += error->message;
	}
	throw GitError(message);
}


} // DiffKit
