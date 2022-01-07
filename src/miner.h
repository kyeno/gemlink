// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MINER_H
#define BITCOIN_MINER_H

#include "primitives/block.h"

#include <boost/optional.hpp>
#include <stdint.h>

class CBlockIndex;
class CChainParams;
class CScript;
#ifdef ENABLE_WALLET
class CReserveKey;
class CWallet;
#endif
namespace Consensus { struct Params; };

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn);
#ifdef ENABLE_WALLET
boost::optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey);
CBlockTemplate* CreateNewBlockWithKey(const CChainParams& chainparams, CReserveKey& reservekey);
#else
boost::optional<CScript> GetMinerScriptPubKey();
CBlockTemplate* CreateNewBlockWithKey(const CChainParams& chainparams);
#endif

#ifdef ENABLE_MINING
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce,
    const Consensus::Params& consensusParams);
/** Run the miner threads */
 #ifdef ENABLE_WALLET
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads, const CChainParams& chainparams);
 #else
void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams);
 #endif
#endif

void UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

#endif // BITCOIN_MINER_H
