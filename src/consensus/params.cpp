// Copyright (c) 2019 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "params.h"

#include "upgrades.h"
#include "util.h"
#include <amount.h>
#include <key_io.h>
#include <script/standard.h>

namespace Consensus
{
bool Params::NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const
{
    return NetworkUpgradeState(nHeight, *this, idx) == UPGRADE_ACTIVE;
}

int Params::validEHparameterList(EHparameters* ehparams, unsigned int blocktime) const
{
    // if in overlap period, there will be two valid solutions, else 1.
    // The upcoming version of EH is preferred so will always be first element
    // returns number of elements in list
    if (blocktime >= eh_epoch_2_start() && blocktime > eh_epoch_1_end()) {
        ehparams[0] = eh_epoch_2_params();
        return 1;
    }
    if (blocktime < eh_epoch_2_start()) {
        ehparams[0] = eh_epoch_1_params();
        return 1;
    }
    ehparams[0] = eh_epoch_2_params();
    ehparams[1] = eh_epoch_1_params();
    return 2;
}
} // namespace Consensus