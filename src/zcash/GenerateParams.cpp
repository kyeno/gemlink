#include "zcash/JoinSplit.hpp"

#include "crypto/common.h"
#include <iostream>

int main(int argc, char** argv)
{
    if (sodium_init() == -1) {
        return 1;
    }

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " provingKeyFileName verificationKeyFileName r1csFileName" << std::endl;
        return 1;
    }

    std::string pkFile = argv[1];
    std::string vkFile = argv[2];
    std::string r1csFile = argv[3];

    ZCJoinSplit::Generate(r1csFile, vkFile, pkFile);

    return 0;
}
