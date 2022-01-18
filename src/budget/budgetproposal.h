// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BUDGET_PROPOSAL_H
#define BUDGET_PROPOSAL_H

#include "budget/budgetvote.h"
#include "net.h"
#include "streams.h"

static const CAmount PROPOSAL_FEE_TX = (50 * COIN);
static const CAmount BUDGET_FEE_TX_OLD = (50 * COIN);
static const CAmount BUDGET_FEE_TX = (5 * COIN);
static const int64_t BUDGET_VOTE_UPDATE_MIN = 60 * 60;

//
// Budget Proposal : Contains the masternode votes for each budget
//

class CBudgetProposal
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    CAmount nAlloted;

    bool fValid;
    std::string strInvalid;

    bool IsHeavilyDownvoted(bool fNewRules);
    bool IsExpired(int nCurrentHeight);
    bool CheckStartEnd();
    bool CheckAmount(const CAmount& nTotalBudget);
    bool CheckAddress();

protected:
    std::map<uint256, CBudgetVote> mapVotes;
    std::string strProposalName;

    /*
        json object with name, short-description, long-description, pdf-url and any other info
        This allows the proposal website to stay 100% decentralized
    */
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;
    CAmount nAmount;
    CScript address;
    uint256 nFeeTXHash;

public:
    int64_t nTime;

    CBudgetProposal();
    CBudgetProposal(const std::string& name, const std::string& url, int paycount, const CScript& payee, const CAmount& amount, int blockstart, const uint256& nfeetxhash);

    bool AddOrUpdateVote(const CBudgetVote& vote, std::string& strError);
    UniValue GetVotesArray() const;
    void SetSynced(bool synced); // sets fSynced on votes (true only if valid)

    // sync proposal votes with a node
    void SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const;

    // sets fValid and strInvalid, returns fValid
    bool UpdateValid(int nHeight, bool fCheckCollateral = true);
    // Static checks that should be done only once - sets strInvalid
    bool IsWellFormed(const CAmount& nTotalBudget);
    bool IsValid() const { return fValid; }
    void SetStrInvalid(const std::string& _strInvalid) { strInvalid = _strInvalid; }
    std::string IsInvalidReason() const { return strInvalid; }
    std::string IsInvalidLogStr() const { return strprintf("[%s]: %s", GetName(), IsInvalidReason()); }
    bool IsEstablished() const;
    bool IsPassing(int nBlockStartBudget, int nBlockEndBudget, int mnCount) const;

    std::string GetName() const { return strProposalName; }
    std::string GetURL() const { return strURL; }
    int GetBlockStart() const { return nBlockStart; }
    int GetBlockEnd() const { return nBlockEnd; }
    CScript GetPayee() const { return address; }
    int GetTotalPaymentCount() const;
    int GetRemainingPaymentCount(int nCurrentHeight) const;
    int GetBlockStartCycle() const;
    static int GetBlockCycle(int nCurrentHeight);
    int GetBlockEndCycle() const;
    const uint256& GetFeeTXHash() const { return nFeeTXHash; }
    double GetRatio() const;
    int GetVoteCount(CBudgetVote::VoteDirection vd) const;
    int GetYeas() const { return GetVoteCount(CBudgetVote::VOTE_YES); }
    int GetNays() const { return GetVoteCount(CBudgetVote::VOTE_NO); }
    int GetAbstains() const { return GetVoteCount(CBudgetVote::VOTE_ABSTAIN); };
    CAmount GetAmount() const { return nAmount; }
    void SetAllotted(CAmount nAllotedIn) { nAlloted = nAllotedIn; }
    CAmount GetAllotted() const { return nAlloted; }

    void CleanAndRemove();

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strProposalName;
        ss << strURL;
        ss << nBlockStart;
        ss << nBlockEnd;
        ss << nAmount;
        ss << std::vector<unsigned char>(address.begin(), address.end());
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        // for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(*(CScriptBase*)(&address));
        READWRITE(nTime);
        READWRITE(nFeeTXHash);

        // for saving to the serialized db
        READWRITE(mapVotes);
    }

    // Serialization for network messages.
    bool ParseBroadcast(CDataStream& broadcast);
    CDataStream GetBroadcast() const;
    void Relay();
};

#endif // BUDGET_PROPOSAL_H
