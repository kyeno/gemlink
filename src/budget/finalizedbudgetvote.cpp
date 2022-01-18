// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "budget/finalizedbudgetvote.h"

#include "net.h"


CFinalizedBudgetVote::CFinalizedBudgetVote() : CSignedMessage(),
                                               fValid(true),
                                               fSynced(false),
                                               vin(),
                                               nTime(0)
{
    nBudgetHash = uint256();
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

CFinalizedBudgetVote::CFinalizedBudgetVote(CTxIn vinIn, uint256 nBudgetHashIn) : CSignedMessage(),
                                                                                 fValid(true),
                                                                                 fSynced(false),
                                                                                 vin(vinIn),
                                                                                 nBudgetHash(nBudgetHashIn)
{
    nTime = GetAdjustedTime();
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

UniValue CFinalizedBudgetVote::ToJSON() const
{
    UniValue bObj(UniValue::VOBJ);
    bObj.push_back(Pair("nHash", vin.prevout.GetHash().ToString()));
    bObj.push_back(Pair("nTime", (int64_t)nTime));
    bObj.push_back(Pair("fValid", fValid));
    return bObj;
}

void CFinalizedBudgetVote::Relay() const
{
    CInv inv(MSG_BUDGET_FINALIZED_VOTE, GetHash());
    RelayInv(inv);
}

uint256 CFinalizedBudgetVote::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << nBudgetHash;
    ss << nTime;
    return ss.GetHash();
}

std::string CFinalizedBudgetVote::GetStrMessage() const
{
    return vin.prevout.ToStringShort() + nBudgetHash.ToString() + std::to_string(nTime);
}
