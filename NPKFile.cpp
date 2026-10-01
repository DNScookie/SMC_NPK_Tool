#include "NPKFile.h"
#include "BinaryReader.h"

#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <unordered_map>
#include <stdexcept>

#include <lz4.h>

#include "RC4.h"
#include "Hash.h"
#include "MurmurHash3.h"



namespace NPK
{


	uint64_t HashId(const std::string& path)//sub_14082F890
	{
		uint32_t hashV1 = NHashV1(path);
		uint32_t MMH3;
		MurmurHash3_x86_32(path.data(), (int)path.size(), 0x9747B28C, &MMH3);

		return ((uint64_t)hashV1 << 32) | MMH3;//hi V1 lo MMH
	}

	void Unpack(std::string& npkPth, std::string& OutPth)
	{
		// TODO:use internal flist
		std::ifstream fFileDict("org_flist.lst");
		if (!fFileDict.is_open())
		{
			std::cout << "Failed to open org_flist.lst\n";
			return;
		}

		std::string fDictEntry;

		//

		unsigned char RC4key[] = "61ea476e-8201-11e5-864b-fcaa147137b7"; //sub_14196D3B0

		std::cout << "MemAlloc..." << std::endl;
		auto Buf = ReadFile(npkPth);

		RC4 arc4(RC4key, sizeof(RC4key) - 1);//no nulltrm

		BinaryReader bs(Buf);

		if (bs.ReadFixedString(4) != "NXPK")
		{
			std::cout << "Not an npk file." << std::endl;
			std::exit(1);
		}
		uint32_t FILE_COUNT = bs.Read<uint32_t>();

		bs.Seek(20);

		uint32_t DICT_OFFSET = bs.Read<uint32_t>();
		bs.Seek(DICT_OFFSET);

		uint32_t DICT_SIZE = FILE_COUNT * 32;

		auto dictBuf = bs.ReadBytes(DICT_SIZE);
		arc4.process(dictBuf.data(), dictBuf.size());
		BinaryReader dr(dictBuf);

		std::cout << "Files: " << FILE_COUNT << " Dict_off: " << DICT_OFFSET << " Dict_sz: " << DICT_SIZE << std::endl;
		uint32_t extCnt = 0;
		while (std::getline(fFileDict, fDictEntry))
		{
			///TEMP WORKAROUND
			auto sep = fDictEntry.find_first_of(" \t");
			std::string filePath = sep == std::string::npos
				? fDictEntry
				: fDictEntry.substr(0, sep);

			if (filePath.empty())
				continue;


			auto slash = filePath.find('/');
			if (slash != std::string::npos)
				filePath = filePath.substr(slash + 1);


			std::replace(filePath.begin(), filePath.end(), '/', '\\');
			///

			uint64_t targetFileHash = HashId(filePath);

			dr.Seek(0);

			bool isFound = false;
			
			//could be faster with a lut but i cant be arsed...
			for (uint32_t i = 0; i < FILE_COUNT; ++i)
			{
				uint64_t HASH_ID = dr.Read<uint64_t>();//8
				uint32_t OFFSET = dr.Read<uint32_t>();//4
				uint32_t C_SIZE = dr.Read<uint32_t>();//4
				uint32_t D_SIZE = dr.Read<uint32_t>();//4
				uint32_t C_CRC = dr.Read<uint32_t>();//4
				uint32_t D_CRC = dr.Read<uint32_t>();//4
				uint16_t COMP_TYP = dr.Read<uint16_t>();//2
				uint16_t ENCP_TYP = dr.Read<uint16_t>();//2
				
				//only the files from fFileDict will be extracted, if found
				if (targetFileHash == HASH_ID)
				{
					extCnt++;
					isFound = true;
					printf("Ext file [%u/%u]: %s\n",extCnt,FILE_COUNT, filePath.c_str());
					if (ENCP_TYP != 0)
					{
						std::cout << "[ERR] Unsupported Etyp!, aborted." << std::endl;
						std::exit(6);
					}

					//alloc file buf
					bs.Seek(OFFSET);
					std::vector<uint8_t> dta = bs.ReadBytes(C_SIZE);
					//out file
					std::vector<uint8_t> out;

					switch (COMP_TYP)
					{
						case 5://LZ4
						{
							out.resize(D_SIZE);

							int decomp = LZ4_decompress_safe(
								reinterpret_cast<const char*>(dta.data()),
								reinterpret_cast<char*>(out.data()),
								static_cast<int>(C_SIZE),
								static_cast<int>(D_SIZE)
							);

							if (decomp < 0)
							{
								std::cout << "[ERR] LZ4 decompression failed on file index " << i << std::endl;
								std::exit(7);

							}

							out.resize(decomp);

							break;
						}
						case 2://
						{
							out = dta;

							break;
						}

						case 3://UNCOMPRESSED
						{
							if (C_SIZE != D_SIZE)
							{
								std::cout << "[ERR] Size ambiguity on file index " << i << " " << C_SIZE << "!=" << D_SIZE << std::endl;
								std::exit(7);

							}
							out = dta;
							break;
						}
						case 0://NXS
						{
							out = dta;

							{
								RC4 npkRc4(RC4key, sizeof(RC4key) - 1);
								npkRc4.process(out.data(), out.size());
							}

							BinaryReader nxr(out);

							if (nxr.ReadFixedString(4) != "NXS\x03")
							{
								std::cout << "[ERR] Not NXS  " << i << std::endl;
								std::exit(7);
							}

							nxr.Seek(8);

							uint32_t nxsDecompSize = nxr.Read<uint32_t>();

							std::vector<uint8_t> nxsdata = nxr.ReadBytes(out.size() - 12);
						
							{
								RC4 nxsRc4(RC4key, sizeof(RC4key) - 1);
								nxsRc4.process(nxsdata.data(), nxsdata.size());
							}

							std::vector<uint8_t> pycdata;
							pycdata.resize(nxsDecompSize);

							int decomp = LZ4_decompress_safe(
								reinterpret_cast<const char*>(nxsdata.data()),
								reinterpret_cast<char*>(pycdata.data()),
								static_cast<int>(nxsdata.size()),
								static_cast<int>(nxsDecompSize)
							);

							if (decomp < 0)
							{
								std::cout << "[ERR] LZ4 decompression failed on file index " << i << std::endl;
								std::exit(7);

							}

							pycdata.resize(decomp);

							out = std::move(pycdata);

							break;
						}

						default:
							std::cout << "[ERR] Unsupported CT " << std::to_string(COMP_TYP) <<"RS: " <<D_SIZE <<" DS: " << C_SIZE<<" !, aborted." << std::endl;
							WriteFile(OutPth, "Failed/" + std::to_string(i) + ".bin", dta);
							std::exit(5);
							break;
					}

					WriteFile(OutPth, filePath, out);
					break;

				}

			}
			if (fDictEntry.starts_with(std::filesystem::path(npkPth).stem().string()+ "/")&&!isFound)
			{
				extCnt++;
				printf("NOT FOUND [%u/%u]: %u %s\n", extCnt, FILE_COUNT, targetFileHash,filePath.c_str());//fDictEntry.c_str()

			}
		}
		fFileDict.close();
	}




	uint32_t GetFileCount(std::string& npkPth, std::string& OutPth)
	{
		unsigned char RC4key[] = "61ea476e-8201-11e5-864b-fcaa147137b7"; //sub_14196D3B0

		std::cout << "MemAlloc..." << std::endl;
		auto Buf = ReadFile(npkPth);

		RC4 arc4(RC4key, sizeof(RC4key) - 1);//no nulltrm

		BinaryReader bs(Buf);

		if (bs.ReadFixedString(4) != "NXPK")
		{
			std::cout << "Not an npk file." << std::endl;
			std::exit(1);
		}
		uint32_t FILE_COUNT = bs.Read<uint32_t>();

		return FILE_COUNT;
	}

	void Patch(const std::string& npkPath,
				const std::string& inputDir,
				const std::string& outputPath)
		{
			unsigned char RC4key[] =
				"61ea476e-8201-11e5-864b-fcaa147137b7";

			namespace fs = std::filesystem;

			struct Entry
			{
				uint64_t hashId;
				uint32_t offset;
				uint32_t cSize;
				uint32_t dSize;
				uint32_t cCrc;
				uint32_t dCrc;
				uint16_t compType;
				uint16_t encType;

				std::vector<uint8_t> data;
			};

			std::cout << "Reading NPK..." << std::endl;

			auto buf = ReadFile(npkPath);
			BinaryReader bs(buf);

			if (bs.ReadFixedString(4) != "NXPK")
			{
				throw std::runtime_error("Not an NPK file.");
			}

			uint32_t fileCount = bs.Read<uint32_t>();

			// Header layout used by the original unpacker.
			bs.Seek(20);
			uint32_t dictOffset = bs.Read<uint32_t>();

			uint32_t dictSize = fileCount * 32;

			std::cout
				<< "Files: " << fileCount
				<< " Dict_off: " << dictOffset
				<< " Dict_sz: " << dictSize
				<< std::endl;

			/*
			* Read and decrypt dictionary.
			*/
			bs.Seek(dictOffset);

			auto dictBuf = bs.ReadBytes(dictSize);

			RC4 arc4(RC4key, sizeof(RC4key) - 1);
			arc4.process(dictBuf.data(), dictBuf.size());

			BinaryReader dr(dictBuf);

			std::vector<Entry> entries;
			entries.resize(fileCount);

			/*
			* Read dictionary and corresponding payloads.
			*/
			for (uint32_t i = 0; i < fileCount; ++i)
			{
				Entry& entry = entries[i];

				entry.hashId = dr.Read<uint64_t>();
				entry.offset = dr.Read<uint32_t>();
				entry.cSize = dr.Read<uint32_t>();
				entry.dSize = dr.Read<uint32_t>();
				entry.cCrc = dr.Read<uint32_t>();
				entry.dCrc = dr.Read<uint32_t>();
				entry.compType = dr.Read<uint16_t>();
				entry.encType = dr.Read<uint16_t>();

				bs.Seek(entry.offset);
				entry.data = bs.ReadBytes(entry.cSize);
			}

			/*
			* Build:
			*
			*   hash -> replacement file
			*
			* from the extracted directory.
			*/
			std::unordered_map<uint64_t, fs::path> replacements;

			if (!fs::exists(inputDir))
			{
				throw std::runtime_error(
					"Input directory does not exist: " + inputDir);
			}

			for (const auto& file : fs::recursive_directory_iterator(inputDir))
			{
				if (!file.is_regular_file())
					continue;

				fs::path relative = fs::relative(file.path(), inputDir);

				std::string relativePath = relative.generic_string();

				/*
				* HashId expects Windows-style paths.
				*/
				std::replace(
					relativePath.begin(),
					relativePath.end(),
					'/',
					'\\');

				uint64_t hash = HashId(relativePath);

				replacements[hash] = file.path();

				std::cout
					<< "Replacement candidate: "
					<< relativePath
					<< std::endl;
			}

			uint32_t patched = 0;
			uint32_t skipped = 0;

			/*
			* Replace existing entries.
			*/
			for (uint32_t i = 0; i < fileCount; ++i)
			{
				Entry& entry = entries[i];

				auto it = replacements.find(entry.hashId);

				if (it == replacements.end())
					continue;

				const fs::path& replacementPath = it->second;

				/*
				* For now we deliberately don't touch encrypted
				* entries. NXS (COMP_TYP == 0) will also be left
				* untouched until we implement its repacking.
				*/
				if (entry.encType != 0)
				{
					std::cout
						<< "[SKIP] Encrypted entry #"
						<< i
						<< std::endl;

					skipped++;
					continue;
				}

				auto replacement =
					ReadFile(replacementPath.string());

				switch (entry.compType)
				{
					case 2:
					{
						// Raw data.

						entry.data = std::move(replacement);

						entry.cSize =
							static_cast<uint32_t>(entry.data.size());

						entry.dSize =
							static_cast<uint32_t>(entry.data.size());

						patched++;

						std::cout
							<< "[PATCH] entry #"
							<< i
							<< " raw, "
							<< entry.dSize
							<< " bytes"
							<< std::endl;

						break;
					}

					case 3:
					{
						// Uncompressed.

						entry.data = std::move(replacement);

						entry.cSize =
							static_cast<uint32_t>(entry.data.size());

						entry.dSize =
							static_cast<uint32_t>(entry.data.size());

						patched++;

						std::cout
							<< "[PATCH] entry #"
							<< i
							<< " uncompressed, "
							<< entry.dSize
							<< " bytes"
							<< std::endl;

						break;
					}

					case 5:
					{
						// LZ4.

						int srcSize =
							static_cast<int>(replacement.size());

						int bound =
							LZ4_compressBound(srcSize);

						std::vector<uint8_t> compressed(bound);

						int compressedSize =
							LZ4_compress_default(
								reinterpret_cast<const char*>(
									replacement.data()),
								reinterpret_cast<char*>(
									compressed.data()),
								srcSize,
								bound);

						if (compressedSize <= 0)
						{
							throw std::runtime_error(
								"LZ4 compression failed for: " +
								replacementPath.string());
						}

						compressed.resize(compressedSize);

						entry.data = std::move(compressed);

						entry.cSize =
							static_cast<uint32_t>(
								entry.data.size());

						entry.dSize =
							static_cast<uint32_t>(
								replacement.size());

						patched++;

						std::cout
							<< "[PATCH] entry #"
							<< i
							<< " LZ4, "
							<< entry.dSize
							<< " -> "
							<< entry.cSize
							<< " bytes"
							<< std::endl;

						break;
					}

					case 0:
					{
						/*
						* NXS requires:
						*
						* RC4
						*   NXS header
						*   RC4
						*     LZ4
						*
						* We don't repack it yet.
						*/
						std::cout
							<< "[SKIP] NXS entry #"
							<< i
							<< std::endl;

						skipped++;

						break;
					}

					default:
					{
						std::cout
							<< "[SKIP] Unsupported compression type "
							<< entry.compType
							<< " entry #"
							<< i
							<< std::endl;

						skipped++;

						break;
					}
				}
			}

			/*
			* Rebuild the archive.
			*
			* We preserve everything before the payload region,
			* then write all payloads sequentially and finally
			* write the encrypted dictionary.
			*/

			uint32_t firstPayloadOffset = UINT32_MAX;

			for (const auto& entry : entries)
			{
				if (entry.offset < firstPayloadOffset)
					firstPayloadOffset = entry.offset;
			}

			if (firstPayloadOffset == UINT32_MAX)
			{
				throw std::runtime_error(
					"Could not determine payload offset.");
			}

			std::vector<uint8_t> output;

			/*
			* Preserve bytes before the first payload.
			*/
			output.insert(
				output.end(),
				buf.begin(),
				buf.begin() + firstPayloadOffset);

			/*
			* Write payloads sequentially.
			*/
			for (auto& entry : entries)
			{
				entry.offset =
					static_cast<uint32_t>(output.size());

				output.insert(
					output.end(),
					entry.data.begin(),
					entry.data.end());
			}

			/*
			* Dictionary starts here.
			*/
			uint32_t newDictOffset =
				static_cast<uint32_t>(output.size());

			std::vector<uint8_t> newDict;
			newDict.reserve(dictSize);

			auto writeU16 =
				[&newDict](uint16_t value)
			{
				newDict.push_back(
					static_cast<uint8_t>(value & 0xff));

				newDict.push_back(
					static_cast<uint8_t>((value >> 8) & 0xff));
			};

			auto writeU32 =
				[&newDict](uint32_t value)
			{
				for (int i = 0; i < 4; ++i)
				{
					newDict.push_back(
						static_cast<uint8_t>(
							(value >> (i * 8)) & 0xff));
				}
			};

			auto writeU64 =
				[&newDict](uint64_t value)
			{
				for (int i = 0; i < 8; ++i)
				{
					newDict.push_back(
						static_cast<uint8_t>(
							(value >> (i * 8)) & 0xff));
				}
			};

			for (const auto& entry : entries)
			{
				writeU64(entry.hashId);
				writeU32(entry.offset);
				writeU32(entry.cSize);
				writeU32(entry.dSize);
				writeU32(entry.cCrc);
				writeU32(entry.dCrc);
				writeU16(entry.compType);
				writeU16(entry.encType);
			}

			/*
			* Encrypt dictionary exactly like the original
			* unpacker expects.
			*/
			RC4 dictRc4(RC4key, sizeof(RC4key) - 1);
			dictRc4.process(newDict.data(), newDict.size());

			output.insert(
				output.end(),
				newDict.begin(),
				newDict.end());

			/*
			* Update dictionary offset in the header.
			*
			* The original format stores DICT_OFFSET at +20.
			*/
			output[20] =
				static_cast<uint8_t>(newDictOffset & 0xff);

			output[21] =
				static_cast<uint8_t>(
					(newDictOffset >> 8) & 0xff);

			output[22] =
				static_cast<uint8_t>(
					(newDictOffset >> 16) & 0xff);

			output[23] =
				static_cast<uint8_t>(
					(newDictOffset >> 24) & 0xff);

			std::ofstream out(
				outputPath,
				std::ios::binary);

			if (!out)
			{
				throw std::runtime_error(
					"Failed to open output file: " +
					outputPath);
			}

			out.write(
				reinterpret_cast<const char*>(output.data()),
				static_cast<std::streamsize>(output.size()));

			if (!out)
			{
				throw std::runtime_error(
					"Failed to write output file: " +
					outputPath);
			}

			std::cout
				<< "Patch complete."
				<< std::endl;

			std::cout
				<< "Patched: "
				<< patched
				<< ", skipped: "
				<< skipped
				<< std::endl;

			std::cout
				<< "Output size: "
				<< output.size()
				<< " bytes"
				<< std::endl;
		}
}

void WriteFile(const std::string& outputDir,const std::string& fName,const std::vector<uint8_t>& data)
{
	namespace fs = std::filesystem;

	fs::path fPath = fs::path(outputDir) / fName;

	fs::create_directories(fPath.parent_path());

	std::ofstream out(fPath, std::ios::binary);
	if (!out)
		throw std::runtime_error("Failed to open output file: " + fPath.string());

	out.write(reinterpret_cast<const char*>(data.data()),
		static_cast<std::streamsize>(data.size()));

	if (!out)
		throw std::runtime_error("Failed to write output file: " + fPath.string());
}