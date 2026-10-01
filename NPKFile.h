#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace NPK
{
	void Unpack(std::string& npkPth, std::string& OutPth);
	uint64_t HashId(const std::string& path);
	void BuildDict(std::string& npkPth, std::string& OutPth);
	uint32_t GetFileCount(std::string& npkPth, std::string& OutPth);
	void Patch(const std::string& npkPath,
           const std::string& inputDir,
           const std::string& outputPath);
}

void WriteFile(const std::string& outputDir,
	const std::string& filename,
	const std::vector<uint8_t>& data);
