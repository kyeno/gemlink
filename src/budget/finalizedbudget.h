// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FINALIZED_BUDGET_H
#define FINALIZED_BUDGET_H

#include "budget/budgetproposal.h"
#include "budget/finalizedbudgetvote.h"
#include "net.h"
#include "streams.h"


class CTxBudgetPayment;

static std::map<uint256, std::pair<uint256, int>> mapPayment_History; // proposal hash --> (block hash, block height)

enum class TrxValidationStatus {
    InValid,       /** Transaction verification failed */
    Valid,         /** Transaction successfully verified */
    DoublePayment, /** Transaction successfully verified, but includes a double-budget-payment */
    VoteThreshold  /** If not enough masternodes have voted on a finalized budget */
};

//
// Finalized Budget : Contains the suggested proposals to pay on a given block
//

class CFinalizedBudget
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    bool fAutoChecked; // If it matches what we see, we'll auto vote for it (masternode only)

    bool fValid;
    std::string strInvalid;

protected:
    std::string strBudgetName;
    int nBlockStart;
    std::vector<CTxBudgetPayment> vecBudgetPayments;
    std::map<uint256, CFinalizedBudgetVote> mapVotes;
    uint256 nFeeTXHash;

public:
    int64_t nTime;

    CFinalizedBudget();
    CFinalizedBudget(const std::string& name, int blockstart, const std::vector<CTxBudgetPayment>& vecBudgetPaymentsIn, const uint256& nfeetxhash);

    void CleanAndRemove();
    bool AddOrUpdateVote(const CFinalizedBudgetVote& vote, std::string& strError);
    UniValue GetVotesObject() const;
    void SetSynced(bool synced); // sets fSynced on votes (true only if valid)

    // sync budget votes with a node
    void SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const;

    // sets fValid and strInvalid, returns fValid
    bool UpdateValid(int nHeight, bool fCheckCollateral = true);
    bool IsValid() const { return fValid; }
    std::string IsInvalidReason() const { return strInvalid; }

    std::string GetName() const { return strBudgetName; }
    std::string GetProposals();
    int GetBlockStart() const { return nBlockStart; }
    int GetBlockEnd() const { return nBlockStart + (int)(vecBudgetPayments.size() - 1); }
    const uint256& GetFeeTXHash() const { return nFeeTXHash; }
    int GetVoteCount() const { return (int)mapVotes.size(); }
    bool IsPaidAlready(uint256 nProposalHash, int nBlockHeight) const;
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    bool GetBudgetPaymentByBlock(int64_t nBlockHeight, CTxBudgetPayment& payment) const;
    bool GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount) const;

    // Verify and vote on finalized budget
    void CheckAndVote();
    // total pivx paid out by this budget
    CAmount GetTotalPayout() const;
    // vote on this finalized budget as a masternode
    void SubmitVote();

    // checks the hashes to make sure we know about them
    std::string GetStatus() const;

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strBudgetName;
        ss << nBlockStart;
        ss << vecBudgetPayments;
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;

    // for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(LIMITED_STRING(strBudgetName, 20));
        READWRITE(nFeeTXHash);
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(vecBudgetPayments);
        READWRITE(fAutoChecked);

        READWRITE(mapVotes);
    }
};

class CTxBudgetPayment
{
public:
    uint256 nProposalHash;
    CScript payee;
    CAmount nAmount;

    CTxBudgetPayment()
    {
        payee = CScript();
        nAmount = 0;
        nProposalHash = uint256();
    }

    ADD_SERIALIZE_METHODS;

    // for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(nAmount);
        READWRITE(nProposalHash);
    }
};


#endif // FINALIZED_BUDGET_H
