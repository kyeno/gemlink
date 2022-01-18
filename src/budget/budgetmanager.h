// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BUDGET_MANAGER_H
#define BUDGET_MANAGER_H

#include "budget/budgetproposal.h"
#include "budget/finalizedbudget.h"

// Define amount of blocks in budget payment cycle
int GetBudgetPaymentCycleBlocks();
// Check the collateral transaction for the budget proposal/finalized budget
bool IsBudgetCollateralValid(const uint256& nTxCollateralHash, const uint256& nExpectedHash, std::string& strError, int64_t& nTime, int& nConf, bool fBudgetFinalization = false);

//
// Budget Manager : Contains all proposals for the budget
//
class CBudgetManager
{
private:
    // hold txes until they mature enough to use
    // hold unconfirmed finalized-budgets collateral txes until they mature enough to use
    std::map<uint256, uint256> mapUnconfirmedFeeTx; // guarded by cs_budgets

    std::map<uint256, uint256> mapFeeTxToProposal; // guarded by cs_proposals
    std::map<uint256, uint256> mapFeeTxToBudget;   // guarded by cs_budgets

    // keep track of the scanning errors I've seen
    std::map<uint256, CBudgetProposal> mapProposals;
    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets;

    std::map<uint256, CBudgetVote> mapSeenProposalVotes;   // guarded by cs_votes
    std::map<uint256, CBudgetVote> mapOrphanProposalVotes; // guarded by cs_votes
    std::map<uint256, CFinalizedBudgetVote> mapSeenFinalizedBudgetVotes;
    std::map<uint256, CFinalizedBudgetVote> mapOrphanFinalizedBudgetVotes;

    void SetSynced(bool synced);

    // Memory Only. Updated in NewBlock (blocks arrive in order)
    std::atomic<int> nBestHeight;

public:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    // critical sections to protect the inner data structures (must be locked in this order)
    mutable CCriticalSection cs_budgets;
    mutable CCriticalSection cs_proposals;
    mutable CCriticalSection cs_finalizedvotes;
    mutable CCriticalSection cs_votes;
    CBudgetManager()
    {
        mapProposals.clear();
        mapFinalizedBudgets.clear();
    }

    void ClearSeen()
    {
        WITH_LOCK(cs_votes, mapSeenProposalVotes.clear(););
        WITH_LOCK(cs_finalizedvotes, mapSeenFinalizedBudgetVotes.clear(););
    }

    int sizeFinalized() { return (int)mapFinalizedBudgets.size(); }
    int sizeProposals() { return (int)mapProposals.size(); }

    bool HaveProposal(const uint256& propHash) const
    {
        LOCK(cs_proposals);
        return mapProposals.count(propHash);
    }
    bool HaveSeenProposalVote(const uint256& voteHash) const
    {
        LOCK(cs_votes);
        return mapSeenProposalVotes.count(voteHash);
    }
    bool HaveFinalizedBudget(const uint256& budgetHash) const
    {
        LOCK(cs_budgets);
        return mapFinalizedBudgets.count(budgetHash);
    }
    bool HaveSeenFinalizedBudgetVote(const uint256& voteHash) const
    {
        LOCK(cs_finalizedvotes);
        return mapSeenFinalizedBudgetVotes.count(voteHash);
    }

    void AddSeenProposalVote(const CBudgetVote& vote);
    void AddSeenFinalizedBudgetVote(const CFinalizedBudgetVote& vote);

    // Use const operator std::map::at(), thus existence must be checked before calling.
    CDataStream GetProposalVoteSerialized(const uint256& voteHash) const;
    CDataStream GetProposalSerialized(const uint256& propHash) const;
    CDataStream GetFinalizedBudgetVoteSerialized(const uint256& voteHash) const;
    CDataStream GetFinalizedBudgetSerialized(const uint256& budgetHash) const;

    bool AddAndRelayProposalVote(const CBudgetVote& vote, std::string& strError);

    void ResetSync() { SetSynced(false); }
    void MarkSynced() { SetSynced(true); }
    void Sync(CNode* node, const uint256& nProp, bool fPartial = false);
    void SetBestHeight(int height) { nBestHeight.store(height, std::memory_order_release); };
    int GetBestHeight() const { return nBestHeight.load(std::memory_order_acquire); }

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock(int height);
    CBudgetProposal* FindProposal(const uint256& nHash);
    // finds the proposal with the given name, with highest net yes count.
    const CBudgetProposal* FindProposalByName(const std::string& strProposalName) const;
    CFinalizedBudget* FindFinalizedBudget(const uint256& nHash);

    static CAmount GetTotalBudget(int nHeight);
    std::vector<CBudgetProposal*> GetBudget();
    std::vector<CBudgetProposal*> GetAllProposals();
    std::vector<CFinalizedBudget*> GetFinalizedBudgets();
    bool IsBudgetPaymentBlock(int nBlockHeight);
    bool AddProposal(CBudgetProposal& budgetProposal);
    bool AddFinalizedBudget(CFinalizedBudget& finalizedBudget);
    void SubmitFinalBudget();

    bool UpdateProposal(const CBudgetVote& vote, CNode* pfrom, std::string& strError);
    bool UpdateFinalizedBudget(CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError);
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew);

    void CheckOrphanVotes();
    void Clear()
    {
        {
            LOCK(cs_proposals);
            mapProposals.clear();
            mapFeeTxToProposal.clear();
        }
        {
            LOCK(cs_budgets);
            mapFinalizedBudgets.clear();
            mapFeeTxToBudget.clear();
            mapUnconfirmedFeeTx.clear();
        }
        {
            LOCK(cs_votes);
            mapSeenProposalVotes.clear();
            mapOrphanProposalVotes.clear();
        }
        {
            LOCK(cs_finalizedvotes);
            mapSeenFinalizedBudgetVotes.clear();
            mapOrphanFinalizedBudgetVotes.clear();
        }
    }
    void CheckAndRemove();
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        {
            LOCK(cs_proposals);
            READWRITE(mapProposals);
            READWRITE(mapFeeTxToProposal);
        }
        {
            LOCK(cs_votes);
            READWRITE(mapSeenProposalVotes);
            READWRITE(mapOrphanProposalVotes);
        }
        {
            LOCK(cs_budgets);
            READWRITE(mapFinalizedBudgets);
            READWRITE(mapFeeTxToBudget);
            READWRITE(mapUnconfirmedFeeTx);
        }
        {
            LOCK(cs_finalizedvotes);
            READWRITE(mapSeenFinalizedBudgetVotes);
            READWRITE(mapOrphanFinalizedBudgetVotes);
        }
    }
};

extern CBudgetManager g_budgetman;

#endif // BUDGET_MANAGER_H