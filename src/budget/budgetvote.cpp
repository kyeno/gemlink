// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "budget/budgetvote.h"

#include "net.h"
#include "streams.h"

CBudgetVote::CBudgetVote() : CSignedMessage(),
                             fValid(true),
                             fSynced(false),
                             vin(),
                             nProposalHash(uint256()),
                             nVote(VOTE_ABSTAIN),
                             nTime(0)
{
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

CBudgetVote::CBudgetVote(CTxIn vinIn, uint256 nProposalHashIn, VoteDirection nVoteIn) : CSignedMessage(),
                                                                                        fValid(true),
                                                                                        fSynced(false),
                                                                                        vin(vinIn),
                                                                                        nProposalHash(nProposalHashIn),
                                                                                        nVote(nVoteIn),
                                                                                        nTime(GetAdjustedTime())
{
    const bool fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    if (fNewSigs) {
        nMessVersion = MessageVersion::MESS_VER_HASH;
    }
}

void CBudgetVote::Relay() const
{
    CInv inv(MSG_BUDGET_VOTE, GetHash());
    RelayInv(inv);
}

uint256 CBudgetVote::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << vin;
    ss << nProposalHash;
    ss << (int)nVote;
    ss << nTime;
    return ss.GetHash();
}

std::string CBudgetVote::GetStrMessage() const
{
    return vin.prevout.ToStringShort() + nProposalHash.ToString() +
           std::to_string(nVote) + std::to_string(nTime);
}

UniValue CBudgetVote::ToJSON() const
{
    UniValue bObj(UniValue::VOBJ);
    bObj.push_back(Pair("mnId", vin.prevout.hash.ToString()));
    bObj.push_back(Pair("nHash", vin.prevout.GetHash().ToString()));
    bObj.push_back(Pair("Vote", GetVoteString()));
    bObj.push_back(Pair("nTime", nTime));
    bObj.push_back(Pair("fValid", fValid));
    return bObj;
}
