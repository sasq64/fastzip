#pragma once

#include <string>

class FUnzip {
public:
	void exec();
//private:
	std::string zipName;
	int threadCount = 8;
	bool verbose = false;
	std::string destinationDir;
};
