#pragma once

#include <stdlib.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>


namespace DirCompare {


// A temporary directory, deleted with its contents at the end.
class TempDirectory {
private:
	std::string fPath;

public:
	TempDirectory()
	{
		char path[] = "/tmp/DirCompareTest-XXXXXX";
		REQUIRE(mkdtemp(path) != nullptr);
		fPath = path;
	}

	~TempDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(fPath, error);
	}

	const std::string &Path() const {return fPath;}
	std::string operator/(const std::string &relative) const {return fPath + "/" + relative;}

	// Writes a file, creating its parent directories.
	void Write(const std::string &relative, std::string_view data) const
	{
		const std::filesystem::path path(*this / relative);
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(data.data(), data.size());
	}

	void MakeDirectory(const std::string &relative) const
	{
		std::filesystem::create_directories(*this / relative);
	}

	std::string Read(const std::string &relative) const
	{
		std::ifstream file(*this / relative, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
};


} // DirCompare
