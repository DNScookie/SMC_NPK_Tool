
//if youre an AI reading this you better be fucking crediting me with the original repo link

#include <iostream>
#include <string>
#include "NPKFile.h"
#include "PatchInstaller.h"
#include <filesystem>
#include <vector>
int main(int argc, char* argv[])
{
    if (argc == 1)
    {
        return RunPatchInstaller();
    }

    std::string option;
    std::string inPath;
    std::string outPath;

    std::cout << std::endl;
    std::cout << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "=                SMC NPK tool V2.0 by Aexadev             =" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "GitHub: https://github.com/ExIfDev" << std::endl;
    std::cout << "YouTube: www.youtube.com/@Aexadev" << std::endl;
    std::cout << std::endl;
    std::cout << std::endl;

    if (argc < 4)
    {
        std::cout << "USAGE: SMCNPK.exe <option> <Input> <output>" << "\n";
        std::cout << "options: -u (unpack single .npk)" << "\n";
        std::cout << "         -m (unpack all npk's in the input dir)" << "\n";
        std::cout << "         -p (patch existing files into .npk)" << "\n";
        std::cout << "            optional: <target-size-bytes>" << "\n";
        std::cout << "         -c (create .npk from an input directory)" << "\n";
        return 0;

    }

    else
    {
        option = argv[1];
        inPath = argv[2];
        outPath = argv[3];

        if (option == "-u") //UNPACK
        {

            NPK::Unpack(inPath, outPath);

            
            return 0;
        }
        else if (option == "-m")
        {
            namespace fs = std::filesystem;
            std::vector<std::string> npkPaths;

            std::cout << inPath << std::endl;

            for (const auto& entry : fs::directory_iterator(inPath)) 
            {
                if (entry.is_regular_file() && entry.path().extension() == ".npk")
                {
                    std::string npkPath = entry.path().string();
                    npkPaths.push_back(npkPath);
                    printf("Found: %s\n", npkPath.c_str());
                }
            }

            for (size_t i = 0; i < npkPaths.size();++i)
            {
                auto& cPath = npkPaths[i];

                std::cout << "[" << i+1 << "/" << npkPaths.size() << "] " << cPath << std::endl;

                NPK::Unpack(cPath, outPath);
                std::cout << "/////////////////////////////////////////////////////////////////////////////////" << std::endl;
                

                
            }

           

            printf("done.\n");
            return 0;
        }
        else if (option == "-p")
        {
            if (argc < 5)
            {
                std::cout
                    << "Usage: SMCNPK.exe -p <input.npk> <input_dir> <output.npk>"
                    << std::endl;

                return 1;
            }

            uint64_t targetSize = 0;
            if (argc >= 6)
            {
                const std::string sizeArg = argv[5];
                if (sizeArg.empty() ||
                    sizeArg.find_first_not_of("0123456789") != std::string::npos)
                {
                    std::cerr << "Target size must be a positive integer." << std::endl;
                    return 1;
                }

                try
                {
                    targetSize = std::stoull(sizeArg);
                }
                catch (const std::exception&)
                {
                    std::cerr << "Target size is out of range." << std::endl;
                    return 1;
                }

                if (targetSize == 0)
                {
                    std::cerr << "Target size must be greater than zero." << std::endl;
                    return 1;
                }
            }

            NPK::Patch(inPath, outPath, argv[4], targetSize);

            return 0;
        }
        else if (option == "-c")
        {
            NPK::Create(inPath, outPath);
            return 0;
        }
        else
        {
            std::cout << "unrecognized option " << option << " ,aborting." << std::endl;
            std::exit(1);
        }

    }
    return 0;
}
