
#include "fastzip.h"
#include "funzip.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <vector>

#include <benchmark/benchmark.h>

int64_t iz_deflate(int level, char* tgt, char* src, unsigned long tgtsize,
                   unsigned long srcsize, int earlyOut);

static void BM_Inflate(benchmark::State& state)
{
	std::vector<char> testdata(256 * 1024, 0);
	std::vector<char> output(512 * 1024);

	if (fileExists(".benchdata")) {
		printf("Reading\n");
		FILE* fp = fopen(".benchdata", "rb");
		fread(&testdata[0], 1, testdata.size(), fp);
		fclose(fp);
	} else {
		for (auto& c : testdata) {
			c = rand() % 256;
			if (c < 128)
				c = 0;
		}
		FILE* fp = fopen(".benchdata", "wb");
		fwrite(&testdata[0], 1, testdata.size(), fp);
		fclose(fp);
	}
	while (state.KeepRunning()) {
		iz_deflate(4, &output[0], &testdata[0], output.size(), testdata.size(), 0); }
}

BENCHMARK(BM_Inflate);

BENCHMARK_MAIN();

