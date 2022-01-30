// Copyright (c) 2021 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "primitives/transaction.h"

/**
 * Returns the most recent supported transaction version and version group id,
 * as of the specified activation height and active features.
 */
TxVersionInfo CurrentTxVersionInfo(
    const Consensus::Params& consensus,
    int nHeight,
    bool requireSprout)
{
    if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_DIFA) ||
    consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ALFHEIMR) ||
    consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_KNOWHERE) ||
    consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_WAKANDA) ||
    consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ATLANTIS) ||
    consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MORAG)
    ) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = SAPLING_VERSION_GROUP_ID,
            .nVersion =        SAPLING_TX_VERSION
        };
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_SAPLING)) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = SAPLING_VERSION_GROUP_ID,
            .nVersion =        SAPLING_TX_VERSION
        };
    } else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_OVERWINTER)) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = OVERWINTER_VERSION_GROUP_ID,
            .nVersion =        OVERWINTER_TX_VERSION
        };
    } else {
        return {
            .fOverwintered =   false,
            .nVersionGroupId = 0,
            .nVersion =        CTransaction::SPROUT_MIN_CURRENT_VERSION
        };
    }
}
