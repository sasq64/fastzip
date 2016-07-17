#pragma once

#include <string>

class ZipStream;

class FUnzip {
public:
	void exec();
	void smartDestDir(ZipStream &zs);
//private:
	std::string zipName;
	int threadCount = 8;
	bool verbose = false;
	std::string destinationDir;
};
