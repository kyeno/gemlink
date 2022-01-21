// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2017 The SnowGem developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "base58.h"
#include "consensus/validation.h"
#include "key.h"
#include "main.h"
#include "masternode-budget.h"
#include "net.h"
#include "protocol.h"
#include "sporkdb.h"
#include "sync.h"
#include "util.h"


using namespace std;
using namespace boost;

#define MAKE_SPORK_DEF(name, defaultValue) CSporkDef(name, defaultValue, #name)

std::vector<CSporkDef> sporkDefs = {
    MAKE_SPORK_DEF(SPORK_2_SWIFTTX, 0),                                    // ON
    MAKE_SPORK_DEF(SPORK_3_SWIFTTX_BLOCK_FILTERING, 0),                    // ON
    MAKE_SPORK_DEF(SPORK_5_MAX_VALUE, 1000),                               // 1000
    MAKE_SPORK_DEF(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT, 1523750400ULL), // ON
    MAKE_SPORK_DEF(SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT, 4070908800ULL),  // OFF
    MAKE_SPORK_DEF(SPORK_10_MASTERNODE_PAY_UPDATED_NODES, 0),              // OFF
    MAKE_SPORK_DEF(SPORK_11_LOCK_INVALID_UTXO, 4070908800ULL),             // OFF
    MAKE_SPORK_DEF(SPORK_13_ENABLE_SUPERBLOCKS, 4070908800ULL),            // OFF
    MAKE_SPORK_DEF(SPORK_14_NEW_PROTOCOL_ENFORCEMENT, 4070908800ULL),      // OFF
    MAKE_SPORK_DEF(SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2, 4070908800ULL),    // OFF
    MAKE_SPORK_DEF(SPORK_16_ZEROCOIN_MAINTENANCE_MODE, 4070908800ULL),     // OFF
    MAKE_SPORK_DEF(SPORK_17_COLDSTAKING_ENFORCEMENT, 4070908800ULL),       // OFF
    MAKE_SPORK_DEF(SPORK_18_ZEROCOIN_PUBLICSPEND_V4, 4070908800ULL),       // OFF
};

CSporkManager sporkManager;
std::map<uint256, CSporkMessage> mapSporks;

CSporkMessage::CSporkMessage() : CSignedMessage(),
                                 nSporkID(0),
                                 nValue(0),
                                 nTimeSigned(0)
{
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

CSporkMessage::CSporkMessage(int nSporkID, int64_t nValue, int64_t nTimeSigned) : CSignedMessage(),
                                                                                  nSporkID(nSporkID),
                                                                                  nValue(nValue),
                                                                                  nTimeSigned(nTimeSigned)
{
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

uint256 CSporkMessage::GetSignatureHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << nMessVersion;
    ss << nSporkID;
    ss << nValue;
    ss << nTimeSigned;
    return ss.GetHash();
}

std::string CSporkMessage::GetStrMessage() const
{
    return std::to_string(nSporkID) +
           std::to_string(nValue) +
           std::to_string(nTimeSigned);
}

bool CSporkMessage::CheckSignature() const
{
    std::string strError = "";
    if (!CSignedMessage::CheckSignature(strError)) {
        LogPrintf("CSporkMessage::CheckSignature Error - %s\n", strError);
        return false;
    }
    return true;
}

// TODO gemlink can remove after morag fork

void CSporkMessage::Relay()
{
    CInv inv(MSG_SPORK, GetHash());
    RelayInv(inv);
}

CSporkManager::CSporkManager()
{
    LogPrintf("init spork mânger\n");
    for (auto& sporkDef : sporkDefs) {
        LogPrintf("id %d, name %s\n", (int)sporkDef.sporkId, sporkDef.name);
        sporkDefsById.emplace((int)sporkDef.sporkId, &sporkDef);
        sporkDefsByName.emplace(sporkDef.name, &sporkDef);
    }
}

void CSporkManager::Clear()
{
    strMasterPrivKey = "";
    mapSporksActive.clear();
}

// SnowGem: on startup load spork values from previous session if they exist in the sporkDB
void CSporkManager::LoadSporksFromDB()
{
    for (const auto& sporkDef : sporkDefs) {
        // attempt to read spork from sporkDB
        CSporkMessage spork;
        if (!pSporkDB->ReadSpork(sporkDef.sporkId, spork)) {
            LogPrintf("%s : no previous value for %s found in database\n", __func__, sporkDef.name);
            continue;
        }

        // add spork to memory
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        std::time_t result = spork.nValue;
        std::string sporkName = sporkManager.GetSporkNameByID(spork.nSporkID);
        // If SPORK Value is greater than 1,000,000 assume it's actually a Date and then convert to a more readable format
        if (spork.nValue > 1000000) {
            char* res = std::ctime(&result);
            LogPrintf("%s : loaded spork %s with value %d : %s\n", __func__, sporkName.c_str(), spork.nValue,
                      ((res) ? res : "no time"));
        } else {
            LogPrintf("%s : loaded spork %s with value %d\n", __func__,
                      sporkName, spork.nValue);
        }
    }
}

void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode)
        return; // disable all obfuscation/masternode related functionality

    int nChainHeight = 0;
    {
        LOCK(cs_main);
        if (chainActive.Tip() == nullptr)
            return;
        nChainHeight = chainActive.Height();
    }

    if (strCommand == "spork") {
        CSporkMessage spork;
        vRecv >> spork;

        if (chainActive.Tip() == NULL)
            return;

        // Ignore spork messages about unknown/deleted sporks
        std::string strSpork = sporkManager.GetSporkNameByID(spork.nSporkID);
        if (strSpork == "Unknown")
            return;

        if (spork.nTimeSigned > GetAdjustedTime() + 2 * 60 * 60) {
            LOCK(cs_main);
            LogPrintf("%s : ERROR: too far into the future\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        // reject old signatures 600 blocks after hard-fork
        if (spork.nMessVersion != MessageVersion::MESS_VER_HASH) {
            if (NetworkUpgradeActive(nChainHeight - 600, Params().GetConsensus(), Consensus::UPGRADE_MORAG)) {
                LogPrintf("%s : nMessVersion=%d not accepted anymore at block %d\n", __func__, spork.nMessVersion, nChainHeight);
                return;
            }
        }

        uint256 hash = spork.GetHash();
        {
            LOCK(cs);
            if (mapSporksActive.count(spork.nSporkID)) {
                if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
                    if (fDebug)
                        LogPrintf("spork - seen %s block %d \n", hash.ToString(), chainActive.Tip()->nHeight);
                    return;
                } else {
                    if (fDebug)
                        LogPrintf("spork - got updated spork %s block %d \n", hash.ToString(), chainActive.Tip()->nHeight);
                }
            } else {
                // spork is not active
                if (fDebug)
                    LogPrintf("%s : got new spork %s block %d \n", __func__, hash.ToString(), chainActive.Tip()->nHeight);
            }
        }

        LogPrintf("spork - new %s ID %d Time %d bestHeight %d\n", hash.ToString(), spork.nSporkID, spork.nValue, chainActive.Tip()->nHeight);

        bool fValidSig = spork.CheckSignature();

        if (!fValidSig) {
            LOCK(cs_main);
            LogPrintf("%s : Invalid Signature\n", __func__);
            Misbehaving(pfrom->GetId(), 100);
            return;
        }

        {
            LOCK(cs);
            mapSporks[hash] = spork;
            mapSporksActive[spork.nSporkID] = spork;
        }

        spork.Relay();

        // SnowGem: add to spork database.
        pSporkDB->WriteSpork(spork.nSporkID, spork);
    }
    if (strCommand == "getsporks") {
        std::map<int, CSporkMessage>::iterator it = mapSporksActive.begin();

        while (it != mapSporksActive.end()) {
            pfrom->PushMessage("spork", it->second);
            it++;
        }
    }
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(int nSporkID)
{
    LOCK(cs);

    if (mapSporksActive.count(nSporkID)) {
        return mapSporksActive[nSporkID].nValue;

    } else {
        auto it = sporkDefsById.find(nSporkID);
        if (it != sporkDefsById.end()) {
            return it->second->defaultValue;
        } else {
            LogPrintf("%s : Unknown Spork %d\n", __func__, nSporkID);
        }
    }

    return -1;
}

// grab the spork value, and see if it's off
bool CSporkManager::IsSporkActive(int nSporkID)
{
    return GetSporkValue(nSporkID) < GetAdjustedTime();
}


void ReprocessBlocks(int nBlocks)
{
    std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
    while (it != mapRejectedBlocks.end()) {
        // use a window twice as large as is usual for the nBlocks we want to reset
        if ((*it).second > GetTime() - (nBlocks * 60 * 5)) {
            BlockMap::iterator mi = mapBlockIndex.find((*it).first);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                LOCK(cs_main);

                CBlockIndex* pindex = (*mi).second;
                LogPrintf("ReprocessBlocks - %s\n", (*it).first.ToString());

                CValidationState state;
                ReconsiderBlock(state, pindex);
            }
        }
        ++it;
    }

    CValidationState state;
    {
        LOCK(cs_main);
        DisconnectBlocksAndReprocess(nBlocks);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }
}

bool CSporkManager::UpdateSpork(int nSporkID, int64_t nValue)
{
    bool fNewSigs = false;
    {
        fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    }
    CSporkMessage spork = CSporkMessage(nSporkID, nValue, GetTime());

    if (spork.SignMessage(strMasterPrivKey, fNewSigs)) {
        spork.Relay();
        LOCK(cs);
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[nSporkID] = spork;
        return true;
    } else {
        LogPrintf("CSporkManager::UpdateSpork - Sign message failed");
    }

    return false;
}


bool CSporkManager::SetPrivKey(std::string strPrivKey)
{
    bool fNewSigs = false;
    {
        fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    }
    CSporkMessage spork;

    // Test signing successful, proceed
    spork.SignMessage(strPrivKey, fNewSigs);

    bool fValidSig = spork.CheckSignature();

    if (fValidSig) {
        LOCK(cs);
        // Test signing successful, proceed
        LogPrintf("%s : Successfully initialized as spork signer\n", __func__);
        strMasterPrivKey = strPrivKey;
        return true;
    } else {
        LogPrintf("%s : Set privkey failedr\n", __func__);
        return false;
    }
}

SporkId CSporkManager::GetSporkIDByName(std::string strName)
{
    auto it = sporkDefsByName.find(strName);
    if (it == sporkDefsByName.end()) {
        LogPrintf("%s : Unknown Spork name '%s'\n", __func__, strName);
        return SPORK_INVALID;
    }
    return (SporkId)it->second->sporkId;
}

std::string CSporkManager::GetSporkNameByID(int nSporkID)
{
    auto it = sporkDefsById.find(nSporkID);
    if (it == sporkDefsById.end()) {
        LogPrint("%s : Unknown Spork ID %d\n", __func__, nSporkID);
        return "Unknown";
    }
    return it->second->name;
}

std::string CSporkManager::ToString() const
{
    LOCK(cs);
    return strprintf("Sporks: %llu", mapSporksActive.size());
}