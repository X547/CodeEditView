#pragma once

#include <string>
#include <vector>

#include "Coverage.h"


namespace Coverage {


// Runs a program (searched in PATH) and returns its standard output. Throws
// std::runtime_error with its error output if it cannot run or fails.
std::string RunProgram(const std::vector<std::string> &argv);


// Coverage of an instrumented binary from a merged profile (.profdata).
class LlvmCov {
private:
	std::string fBinary;
	std::string fProfile;

public:
	LlvmCov(std::string binary, std::string profile);

	const std::string &Binary() const {return fBinary;}
	const std::string &Profile() const {return fProfile;}

	// Throw std::runtime_error on failure.
	Summary LoadSummary() const;
	FileCoverage LoadFile(const std::string &path) const;
};


} // Coverage
