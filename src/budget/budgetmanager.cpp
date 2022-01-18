// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "budget/budgetmanager.h"

#include "masternode-sync.h"
#include "masternodeman.h"
#include "validation.h" // GetTransaction, cs_main


CBudgetManager g_budgetman;

std::map<uint256, int64_t> askedForSourceProposalOrBudget;

// Used to check both proposals and finalized-budgets collateral txes
bool CheckCollateral(const uint256& nTxCollateralHash, const uint256& nExpectedHash, std::string& strError, int64_t& nTime, int nCurrentHeight, bool fBudgetFinalization);


bool CBudgetManager::IsBudgetPaymentBlock(int nBlockHeight)
{
    int nHighestCount = -1;
    int nFivePercent = mnodeman.CountEnabled(ActiveProtocol()) / 20;

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (pfinalizedBudget->GetVoteCount() > nHighestCount &&
            nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
            nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    LogPrint("masternode", "CBudgetManager::IsBudgetPaymentBlock() - nHighestCount: %lli, 5%% of Masternodes: %lli. Number of budgets: %lli\n",
             nHighestCount, nFivePercent, mapFinalizedBudgets.size());

    // If budget doesn't have 5% of the network votes, then we should pay a masternode instead
    if (nHighestCount > nFivePercent)
        return true;

    return false;
}

TrxValidationStatus CBudgetManager::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs);

    int nHighestCount = 0;
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;
    int nFivePercent = mnodeman.CountEnabled(ActiveProtocol()) / 20;
    std::vector<CFinalizedBudget*> ret;

    // ------- Grab The Highest Count

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (pfinalizedBudget->GetVoteCount() > nHighestCount &&
            nBlockHeight >= pfinalizedBudget->GetBlockStart() &&
            nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            nHighestCount = pfinalizedBudget->GetVoteCount();
        }

        ++it;
    }

    LogPrint("masternode", "CBudgetManager::IsTransactionValid() - nHighestCount: %lli, 5%% of Masternodes: %lli mapFinalizedBudgets.size(): %ld\n",
             nHighestCount, nFivePercent, mapFinalizedBudgets.size());
    /*
        If budget doesn't have 5% of the network votes, then we should pay a masternode instead
    */
    if (nHighestCount < nFivePercent)
        return TrxValidationStatus::InValid;

    // check the highest finalized budgets (+/- 10% to assist in consensus)

    it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        if (pfinalizedBudget->GetVoteCount() > nHighestCount - mnodeman.CountEnabled(ActiveProtocol()) / 10) {
            if (nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
                transactionStatus = pfinalizedBudget->IsTransactionValid(txNew, nBlockHeight);
                if (transactionStatus == TrxValidationStatus::Valid) {
                    LogPrint("mnbudget", "%s: pfinalizedBudget->IsTransactionValid() passed\n", __func__);
                    return TrxValidationStatus::Valid;
                }
            }
        }

        ++it;
    }

    // we looked through all of the known budgets
    return transactionStatus;
}

std::vector<CBudgetProposal*> CBudgetManager::GetAllProposals()
{
    LOCK(cs);

    std::vector<CBudgetProposal*> vBudgetProposalRet;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove();

        CBudgetProposal* pbudgetProposal = &((*it).second);
        vBudgetProposalRet.push_back(pbudgetProposal);

        ++it;
    }

    return vBudgetProposalRet;
}

//
// Sort by votes, if there's a tie sort by their feeHash TX
//
struct sortProposalsByVotes {
    bool operator()(const std::pair<CBudgetProposal*, int>& left, const std::pair<CBudgetProposal*, int>& right)
    {
        if (left.second != right.second)
            return (left.second > right.second);
        return (UintToArith256(left.first->GetFeeTXHash()) > UintToArith256(right.first->GetFeeTXHash()));
    }
};

// Need to review this function
std::vector<CBudgetProposal*> CBudgetManager::GetBudget()
{
    LOCK(cs);

    // ------- Sort budgets by Yes Count

    std::vector<std::pair<CBudgetProposal*, int>> vBudgetPorposalsSort;

    std::map<uint256, CBudgetProposal>::iterator it = mapProposals.begin();
    while (it != mapProposals.end()) {
        (*it).second.CleanAndRemove();
        vBudgetPorposalsSort.push_back(make_pair(&((*it).second), (*it).second.GetYeas() - (*it).second.GetNays()));
        ++it;
    }

    std::sort(vBudgetPorposalsSort.begin(), vBudgetPorposalsSort.end(), sortProposalsByVotes());

    // ------- Grab The Budgets In Order

    std::vector<CBudgetProposal*> vBudgetProposalsRet;

    CAmount nBudgetAllocated = 0;
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL)
        return vBudgetProposalsRet;

    int nBlockStart = pindexPrev->nHeight - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    int nBlockEnd = nBlockStart + GetBudgetPaymentCycleBlocks() - 1;
    CAmount nTotalBudget = GetTotalBudget(nBlockStart);


    std::vector<std::pair<CBudgetProposal*, int>>::iterator it2 = vBudgetPorposalsSort.begin();
    while (it2 != vBudgetPorposalsSort.end()) {
        CBudgetProposal* pbudgetProposal = (*it2).first;

        LogPrint("masternode", "CBudgetManager::GetBudget() - Processing Budget %s\n", pbudgetProposal->GetName().c_str());
        // prop start/end should be inside this period
        if (pbudgetProposal->IsValid() && pbudgetProposal->GetBlockStart() <= nBlockStart &&
            pbudgetProposal->GetBlockEnd() >= nBlockEnd &&
            pbudgetProposal->GetYeas() - pbudgetProposal->GetNays() > mnodeman.CountEnabled(ActiveProtocol()) / 10 &&
            pbudgetProposal->IsEstablished()) {
            LogPrint("masternode", "CBudgetManager::GetBudget() -   Check 1 passed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                     pbudgetProposal->IsValid(), pbudgetProposal->GetBlockStart(), nBlockStart, pbudgetProposal->GetBlockEnd(),
                     nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnodeman.CountEnabled(ActiveProtocol()) / 10,
                     pbudgetProposal->IsEstablished());

            if (pbudgetProposal->GetAmount() + nBudgetAllocated <= nTotalBudget) {
                pbudgetProposal->SetAllotted(pbudgetProposal->GetAmount());
                nBudgetAllocated += pbudgetProposal->GetAmount();
                vBudgetProposalsRet.push_back(pbudgetProposal);
                LogPrint("masternode", "CBudgetManager::GetBudget() -     Check 2 passed: Budget added\n");
            } else {
                pbudgetProposal->SetAllotted(0);
                LogPrint("masternode", "CBudgetManager::GetBudget() -     Check 2 failed: no amount allotted\n");
            }
        } else {
            LogPrint("masternode", "CBudgetManager::GetBudget() -   Check 1 failed: valid=%d | %ld <= %ld | %ld >= %ld | Yeas=%d Nays=%d Count=%d | established=%d\n",
                     pbudgetProposal->IsValid(), pbudgetProposal->GetBlockStart(), nBlockStart, pbudgetProposal->GetBlockEnd(),
                     nBlockEnd, pbudgetProposal->GetYeas(), pbudgetProposal->GetNays(), mnodeman.CountEnabled(ActiveProtocol()) / 10,
                     pbudgetProposal->IsEstablished());
        }

        ++it2;
    }

    return vBudgetProposalsRet;
}

struct sortFinalizedBudgetsByVotes {
    bool operator()(const std::pair<CFinalizedBudget*, int>& left, const std::pair<CFinalizedBudget*, int>& right)
    {
        return left.second > right.second;
    }
};
std::vector<CFinalizedBudget*> CBudgetManager::GetFinalizedBudgets()
{
    LOCK(cs);

    std::vector<CFinalizedBudget*> vFinalizedBudgetsRet;

    // ------- Grab The Budgets In Order
    std::vector<std::pair<CFinalizedBudget*, int>> vFinalizedBudgetsSort;

    // ------- Grab The Budgets In Order

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);

        vFinalizedBudgetsSort.push_back(make_pair(pfinalizedBudget, pfinalizedBudget->GetVoteCount()));
        ++it;
    }
    std::sort(vFinalizedBudgetsSort.begin(), vFinalizedBudgetsSort.end(), sortFinalizedBudgetsByVotes());

    std::vector<std::pair<CFinalizedBudget*, int>>::iterator it2 = vFinalizedBudgetsSort.begin();
    while (it2 != vFinalizedBudgetsSort.end()) {
        vFinalizedBudgetsRet.push_back((*it2).first);
        ++it2;
    }

    return vFinalizedBudgetsRet;
}

std::string CBudgetManager::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs);

    std::string ret = "unknown-budget";

    std::map<uint256, CFinalizedBudget>::iterator it = mapFinalizedBudgets.begin();
    while (it != mapFinalizedBudgets.end()) {
        CFinalizedBudget* pfinalizedBudget = &((*it).second);
        if (nBlockHeight >= pfinalizedBudget->GetBlockStart() && nBlockHeight <= pfinalizedBudget->GetBlockEnd()) {
            CTxBudgetPayment payment;
            if (pfinalizedBudget->GetBudgetPaymentByBlock(nBlockHeight, payment)) {
                if (ret == "unknown-budget") {
                    ret = payment.nProposalHash.ToString();
                } else {
                    ret += ",";
                    ret += payment.nProposalHash.ToString();
                }
            } else {
                LogPrint("masternode", "CBudgetManager::GetRequiredPaymentsString - Couldn't find budget payment for block %d\n", nBlockHeight);
            }
        }

        ++it;
    }

    return ret;
}

CAmount CBudgetManager::GetTotalBudget(int nHeight)
{
    if (NetworkIdFromCommandLine() == CBaseChainParams::TESTNET) {
        CAmount nSubsidy = 500 * COIN;
        return ((nSubsidy / 100) * 10) * 1440;
    }

    CAmount nSubsidy = 500 * COIN;
    return ((nSubsidy / 100) * 10) * 1440;
}

void CBudgetManager::AddSeenProposalVote(const CBudgetVote& vote)
{
    LOCK(cs_votes);
    mapSeenProposalVotes.emplace(vote.GetHash(), vote);
}

void CBudgetManager::AddSeenFinalizedBudgetVote(const CFinalizedBudgetVote& vote)
{
    LOCK(cs_finalizedvotes);
    mapSeenFinalizedBudgetVotes.emplace(vote.GetHash(), vote);
}


CDataStream CBudgetManager::GetProposalVoteSerialized(const uint256& voteHash) const
{
    LOCK(cs_votes);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(1000);
    ss << mapSeenProposalVotes.at(voteHash);
    return ss;
}

CDataStream CBudgetManager::GetProposalSerialized(const uint256& propHash) const
{
    LOCK(cs_proposals);
    return mapProposals.at(propHash).GetBroadcast();
}

CDataStream CBudgetManager::GetFinalizedBudgetVoteSerialized(const uint256& voteHash) const
{
    LOCK(cs_finalizedvotes);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(1000);
    ss << mapSeenFinalizedBudgetVotes.at(voteHash);
    return ss;
}

CDataStream CBudgetManager::GetFinalizedBudgetSerialized(const uint256& budgetHash) const
{
    LOCK(cs_budgets);
    return mapFinalizedBudgets.at(budgetHash).GetBroadcast();
}

bool CBudgetManager::AddAndRelayProposalVote(const CBudgetVote& vote, std::string& strError)
{
    if (UpdateProposal(vote, nullptr, strError)) {
        AddSeenProposalVote(vote);
        vote.Relay();
        return true;
    }
    return false;
}


void CBudgetManager::NewBlock(int height)
{
    TRY_LOCK(cs, fBudgetNewBlock);
    if (!fBudgetNewBlock)
        return;
    SetBestHeight(height);

    if (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_BUDGET)
        return;

    if (strBudgetMode == "suggest") { // suggest the budget we see
        SubmitFinalBudget();
    }

    int nCurrentHeight = GetBestHeight();
    // this function should be called 1/14 blocks, allowing up to 100 votes per day on all proposals
    if (nCurrentHeight % 14 != 0)
        return;

    // incremental sync with our peers
    if (masternodeSync.IsSynced()) {
        LogPrint("masternode", "CBudgetManager::NewBlock - incremental sync started\n");
        if (0 == rand() % 1440) {
            ClearSeen();
            ResetSync();
        }

        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes)
            if (pnode->nVersion >= ActiveProtocol())
                Sync(pnode, uint256(), true);

        MarkSynced();
    }


    CheckAndRemove();

    // remove invalid votes once in a while (we have to check the signatures and validity of every vote, somewhat CPU intensive)

    LogPrint("masternode", "CBudgetManager::NewBlock - askedForSourceProposalOrBudget cleanup - size: %d\n", askedForSourceProposalOrBudget.size());
    std::map<uint256, int64_t>::iterator it = askedForSourceProposalOrBudget.begin();
    while (it != askedForSourceProposalOrBudget.end()) {
        if ((*it).second > GetTime() - (60 * 60 * 24)) {
            ++it;
        } else {
            askedForSourceProposalOrBudget.erase(it++);
        }
    }

    LogPrint("masternode", "CBudgetManager::NewBlock - mapProposals cleanup - size: %d\n", mapProposals.size());
    std::map<uint256, CBudgetProposal>::iterator it2 = mapProposals.begin();
    while (it2 != mapProposals.end()) {
        (*it2).second.CleanAndRemove();
        ++it2;
    }

    LogPrint("masternode", "CBudgetManager::NewBlock - mapFinalizedBudgets cleanup - size: %d\n", mapFinalizedBudgets.size());
    std::map<uint256, CFinalizedBudget>::iterator it3 = mapFinalizedBudgets.begin();
    while (it3 != mapFinalizedBudgets.end()) {
        (*it3).second.CleanAndRemove();
        ++it3;
    }

    LogPrint("masternode", "CBudgetManager::NewBlock - vecImmatureBudgetProposals cleanup - size: %d\n", vecImmatureBudgetProposals.size());
    std::vector<CBudgetProposalBroadcast>::iterator it4 = vecImmatureBudgetProposals.begin();
    while (it4 != vecImmatureBudgetProposals.end()) {
        std::string strError = "";
        int nConf = 0;
        const uint256& nHash = it4->GetHash();
        if (!IsBudgetCollateralValid(it4->GetFeeTXHash(), nHash, strError, (*it4).nTime, nConf)) {
            ++it4;
            continue;
        }

        if (!(*it4).UpdateValid(nCurrentHeight)) {
            LogPrint("masternode", "mprop (immature) - invalid budget proposal - %s\n", strError);
            it4 = vecImmatureBudgetProposals.erase(it4);
            continue;
        }

        CBudgetProposal budgetProposal((*it4));
        if (AddProposal(budgetProposal)) {
            (*it4).Relay();
        }

        LogPrint("masternode", "mprop (immature) - new budget - %s\n", (*it4).GetHash().ToString());
        it4 = vecImmatureBudgetProposals.erase(it4);
    }

    LogPrint("masternode", "CBudgetManager::NewBlock - vecImmatureFinalizedBudgets cleanup - size: %d\n", vecImmatureFinalizedBudgets.size());
    std::vector<CFinalizedBudgetBroadcast>::iterator it5 = vecImmatureFinalizedBudgets.begin();
    while (it5 != vecImmatureFinalizedBudgets.end()) {
        std::string strError = "";
        int nConf = 0;
        const uint256& nHash = it5->GetHash();
        if (!IsBudgetCollateralValid(it5->GetFeeTXHash(), nHash, strError, (*it5).nTime, nConf, true)) {
            ++it5;
            continue;
        }

        if (!(*it5).UpdateValid(nCurrentHeight)) {
            LogPrint("masternode", "fbs (immature) - invalid finalized budget - %s\n", it5->IsInvalidReason());
            it5 = vecImmatureFinalizedBudgets.erase(it5);
            continue;
        }

        LogPrint("masternode", "fbs (immature) - new finalized budget - %s\n", (*it5).GetHash().ToString());

        CFinalizedBudget finalizedBudget((*it5));
        if (AddFinalizedBudget(finalizedBudget)) {
            (*it5).Relay();
        }

        it5 = vecImmatureFinalizedBudgets.erase(it5);
    }
    LogPrint("masternode", "CBudgetManager::NewBlock - PASSED\n");
}

void CBudgetManager::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    // lite mode is not supported
    if (fLiteMode)
        return;
    if (!masternodeSync.IsBlockchainSynced())
        return;

    int nCurrentHeight = GetBestHeight();
    LOCK(cs_budget);

    if (strCommand == "mnvs") { // Masternode vote sync
        uint256 nProp;
        vRecv >> nProp;

        if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
            if (nProp == uint256()) {
                if (pfrom->HasFulfilledRequest("mnvs")) {
                    LogPrint("masternode", "mnvs - peer already asked me for the list\n");
                    Misbehaving(pfrom->GetId(), 20);
                    return;
                }
                pfrom->FulfilledRequest("mnvs");
            }
        }

        Sync(pfrom, nProp);
        LogPrint("mnbudget", "mnvs - Sent Masternode votes to peer %i\n", pfrom->GetId());
    }

    if (strCommand == "mprop") { // Masternode Proposal
        CBudgetProposalBroadcast budgetProposalBroadcast;
        vRecv >> budgetProposalBroadcast;

        if (HaveProposal(budgetProposalBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(budgetProposalBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        const uint256& nHash = budgetProposalBroadcast.GetHash();
        const uint256& nFeeTXHash = budgetProposalBroadcast.GetFeeTXHash();
        if (!IsBudgetCollateralValid(nFeeTXHash, nHash, strError, budgetProposalBroadcast.nTime, nConf)) {
            LogPrint("mnbudget", "Proposal FeeTX is not valid - %s - %s\n", nFeeTXHash.ToString(), strError);
            if (nConf >= 1)
                vecImmatureBudgetProposals.push_back(budgetProposalBroadcast);
            return;
        }

        AddSeenProposal(budgetProposalBroadcast);

        if (!budgetProposalBroadcast.UpdateValid(nCurrentHeight)) {
            LogPrint("masternode", "mprop - invalid budget proposal - %s\n", budgetProposalBroadcast.IsInvalidReason());
            return;
        }

        CBudgetProposal budgetProposal(budgetProposalBroadcast);
        if (AddProposal(budgetProposal)) {
            budgetProposalBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(nHash);

        LogPrint("masternode", "mprop - new budget - %s\n", nHash.ToString());

        // We might have active votes for this proposal that are valid now
        CheckOrphanVotes();
    }

    if (strCommand == "mvote") { // Masternode Vote
        CBudgetVote vote;
        vRecv >> vote;
        vote.SetValid(true);

        if (HaveSeenProposalVote(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        const CTxIn& voteVin = vote.GetVin();
        CMasternode* pmn = mnodeman.Find(voteVin);
        if (pmn == NULL) {
            LogPrint("masternode", "mvote - unknown masternode - vin: %s\n", voteVin.ToString());
            mnodeman.AskForMN(pfrom, voteVin);
            return;
        }


        AddSeenProposalVote(vote);
        string strError = "";
        if (!vote.CheckSignature(strError)) {
            LogPrint("masternode", "mvote - signature invalid\n");
            if (masternodeSync.IsSynced()) {
                LogPrintf("mvote - signature invalid\n");
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, voteVin);
            return;
        }

        if (UpdateProposal(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());
        }

        LogPrint("masternode", "mvote - new budget vote for budget %s - %s\n", vote.GetProposalHash().ToString(), vote.GetHash().ToString());
    }

    if (strCommand == "fbs") { // Finalized Budget Suggestion
        CFinalizedBudgetBroadcast finalizedBudgetBroadcast;
        vRecv >> finalizedBudgetBroadcast;

        if (HaveSeenFinalizedBudget(finalizedBudgetBroadcast.GetHash())) {
            masternodeSync.AddedBudgetItem(finalizedBudgetBroadcast.GetHash());
            return;
        }

        std::string strError = "";
        int nConf = 0;
        const uint256& nHash = finalizedBudgetBroadcast.GetHash();
        const uint256& nFeeTXHash = finalizedBudgetBroadcast.GetFeeTXHash();
        if (!IsBudgetCollateralValid(nFeeTXHash, nHash, strError, finalizedBudgetBroadcast.nTime, nConf, true)) {
            LogPrint("masternode", "Finalized Budget FeeTX is not valid - %s - %s\n", finalizedBudgetBroadcast.GetFeeTXHash().ToString(), strError);

            if (nConf >= 1)
                vecImmatureFinalizedBudgets.push_back(finalizedBudgetBroadcast);
            return;
        }

        AddSeenFinalizedBudget(finalizedBudgetBroadcast);

        if (!finalizedBudgetBroadcast.UpdateValid(nCurrentHeight)) {
            LogPrint("mnbudget", "fbs - invalid finalized budget - %s\n", finalizedBudgetBroadcast.IsInvalidReason());
            finalizedBudget.SetStrInvalid(strError);
            return;
        }

        LogPrint("masternode", "fbs - new finalized budget - %s\n", finalizedBudgetBroadcast.GetHash().ToString());

        CFinalizedBudget finalizedBudget(finalizedBudgetBroadcast);
        if (AddFinalizedBudget(finalizedBudget)) {
            finalizedBudgetBroadcast.Relay();
        }
        masternodeSync.AddedBudgetItem(nHash);

        // we might have active votes for this budget that are now valid
        CheckOrphanVotes();
    }

    if (strCommand == "fbvote") { // Finalized Budget Vote
        CFinalizedBudgetVote vote;
        vRecv >> vote;
        vote.SetValid(true);

        if (HaveSeenFinalizedBudgetVote(vote.GetHash())) {
            masternodeSync.AddedBudgetItem(vote.GetHash());
            return;
        }

        const CTxIn& voteVin = vote.GetVin();
        CMasternode* pmn = mnodeman.Find(voteVin);
        if (pmn == NULL) {
            LogPrint("mnbudget", "fbvote - unknown masternode - vin: %s\n", voteVin.prevout.hash.ToString());
            mnodeman.AskForMN(pfrom, voteVin);
            return;
        }

        std::string strError = "";
        mapSeenFinalizedBudgetVotes.insert(make_pair(vote.GetHash(), vote));
        if (!vote.CheckSignature(strError)) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("CBudgetManager::ProcessMessage() : fbvote - signature invalid\n");
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, voteVin);
            return;
        }

        if (UpdateFinalizedBudget(vote, pfrom, strError)) {
            vote.Relay();
            masternodeSync.AddedBudgetItem(vote.GetHash());

            LogPrint("masternode", "fbvote - new finalized budget vote - %s\n", vote.GetHash().ToString());
        } else {
            LogPrint("masternode", "fbvote - rejected finalized budget vote - %s - %s\n", vote.GetHash().ToString(), strError);
        }
    }
}

// mark that a full sync is needed
void CBudgetManager::SetSynced(bool synced)
{
    LOCK(cs);

    for (const auto& it : mapSeenMasternodeBudgetProposals) {
        CBudgetProposal* pbudgetProposal = FindProposal(it.first);
        if (pbudgetProposal && pbudgetProposal->IsValid()) {
            // mark votes
            pbudgetProposal->SetSynced(synced);
        }
    }

    for (const auto& it : mapSeenFinalizedBudgets) {
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget(it.first);
        if (pfinalizedBudget && pfinalizedBudget->IsValid()) {
            // mark votes
            pfinalizedBudget->SetSynced(synced);
        }
    }
}

void CBudgetManager::Sync(CNode* pfrom, const uint256& nProp, bool fPartial)
{
    LOCK(cs);

    /*
        Sync with a client on the network

        --

        This code checks each of the hash maps for all known budget proposals and finalized budget proposals, then checks them against the
        budget object to see if they're OK. If all checks pass, we'll send it to the peer.

    */
    int nInvCount = 0;

    std::map<uint256, CBudgetProposalBroadcast>::iterator it1 = mapSeenMasternodeBudgetProposals.begin();
    for (auto& it : mapSeenMasternodeBudgetProposals) {
        CBudgetProposal* pbudgetProposal = FindProposal(it.first);
        if (pbudgetProposal && pbudgetProposal->IsValid() && (nProp.IsNull() || it.first == nProp)) {
            pfrom->PushInventory(CInv(MSG_BUDGET_PROPOSAL, it.second.GetHash()));
            nInvCount++;
            pbudgetProposal->SyncVotes(pfrom, fPartial, nInvCount);
        }
    }

    pfrom->PushMessage("ssc", MASTERNODE_SYNC_BUDGET_PROP, nInvCount);

    LogPrint("mnbudget", "CBudgetManager::Sync - sent %d items\n", nInvCount);

    nInvCount = 0;
    for (auto& it : mapSeenFinalizedBudgets) {
        CFinalizedBudget* pfinalizedBudget = FindFinalizedBudget(it.first);
        if (pfinalizedBudget && pfinalizedBudget->IsValid() && (nProp.IsNull() || it.first == nProp)) {
            pfrom->PushInventory(CInv(MSG_BUDGET_FINALIZED, it.second.GetHash()));
            nInvCount++;
            pfinalizedBudget->SyncVotes(pfrom, fPartial, nInvCount);
        }
    }

    pfrom->PushMessage("ssc", MASTERNODE_SYNC_BUDGET_FIN, nInvCount);
    LogPrint("mnbudget", "CBudgetManager::Sync - sent %d items\n", nInvCount);
}

bool CBudgetManager::UpdateProposal(const CBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    const uint256& nProposalHash = vote.GetProposalHash();
    if (!mapProposals.count(nProposalHash)) {
        if (pfrom) {
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if (!masternodeSync.IsSynced())
                return false;

            LogPrint("masternode", "CBudgetManager::UpdateProposal - Unknown proposal %d, asking for source proposal\n", nProposalHash.ToString());
            mapOrphanMasternodeBudgetVotes[nProposalHash] = vote;

            if (!askedForSourceProposalOrBudget.count(nProposalHash)) {
                pfrom->PushMessage("mnvs", nProposalHash);
                askedForSourceProposalOrBudget[nProposalHash] = GetTime();
            }
        }

        strError = "Proposal not found!";
        return false;
    }


    return mapProposals[nProposalHash].AddOrUpdateVote(vote, strError);
}

bool CBudgetManager::UpdateFinalizedBudget(CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError)
{
    LOCK(cs);

    const uint256& nBudgetHash = vote.GetBudgetHash();
    if (!mapFinalizedBudgets.count(nBudgetHash)) {
        if (pfrom) {
            // only ask for missing items after our syncing process is complete --
            //   otherwise we'll think a full sync succeeded when they return a result
            if (!masternodeSync.IsSynced())
                return false;

            LogPrint("masternode", "CBudgetManager::UpdateFinalizedBudget - Unknown Finalized Proposal %s, asking for source budget\n", nBudgetHash.ToString());
            mapOrphanFinalizedBudgetVotes[nBudgetHash] = vote;

            if (!askedForSourceProposalOrBudget.count(nBudgetHash)) {
                pfrom->PushMessage("mnvs", nBudgetHash);
                askedForSourceProposalOrBudget[nBudgetHash] = GetTime();
            }
        }

        strError = "Finalized Budget " + nBudgetHash.ToString() + " not found!";
        return false;
    }
    LogPrint("masternode", "CBudgetManager::UpdateFinalizedBudget - Finalized Proposal %s added\n", nBudgetHash.ToString());
    return mapFinalizedBudgets[nBudgetHash].AddOrUpdateVote(vote, strError);
}

void CBudgetManager::CheckOrphanVotes()
{
    LOCK(cs);


    std::string strError = "";
    auto it1 = mapOrphanMasternodeBudgetVotes.begin();
    while (it1 != mapOrphanMasternodeBudgetVotes.end()) {
        if (budget.UpdateProposal(((*it1).second), NULL, strError)) {
            LogPrint("masternode", "%s: Proposal/Budget is known, activating and removing orphan vote\n", __func__);
            mapOrphanMasternodeBudgetVotes.erase(it1);
        }
        ++it1;
    }
    auto it2 = mapOrphanFinalizedBudgetVotes.begin();
    while (it2 != mapOrphanFinalizedBudgetVotes.end()) {
        if (budget.UpdateFinalizedBudget(((*it2).second), NULL, strError)) {
            LogPrint("masternode", "%s: Proposal/Budget is known, activating and removing orphan vote\n", __func__);
            mapOrphanFinalizedBudgetVotes.erase(it2);
        }
        ++it2;
    }
    LogPrint("masternode", "CBudgetManager::CheckOrphanVotes - Done\n");
}

void CBudgetManager::SubmitFinalBudget()
{
    static int nSubmittedHeight = 0; // height at which final budget was submitted last time
    int nCurrentHeight;

    {
        TRY_LOCK(cs_main, locked);
        if (!locked)
            return;
        if (!chainActive.Tip())
            return;
        nCurrentHeight = chainActive.Height();
    }

    int nBlockStart = nCurrentHeight - nCurrentHeight % GetBudgetPaymentCycleBlocks() + GetBudgetPaymentCycleBlocks();
    if (nSubmittedHeight >= nBlockStart) {
        LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - nSubmittedHeight(=%ld) < nBlockStart(=%ld) condition not fulfilled.\n", nSubmittedHeight, nBlockStart);
        return;
    }
    // Submit final budget during the last 2 days before payment for Mainnet, about 9 minutes for Testnet
    int nFinalizationStart = nBlockStart - ((GetBudgetPaymentCycleBlocks() / 30) * 2);
    int nOffsetToStart = nFinalizationStart - nCurrentHeight;

    if (nBlockStart - nCurrentHeight > ((GetBudgetPaymentCycleBlocks() / 30) * 2)) {
        LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - Too early for finalization. Current block is %ld, next Superblock is %ld.\n", nCurrentHeight, nBlockStart);
        LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - First possible block for finalization: %ld. Last possible block for finalization: %ld. You have to wait for %ld block(s) until Budget finalization will be possible\n", nFinalizationStart, nBlockStart, nOffsetToStart);

        return;
    }

    std::vector<CBudgetProposal*> vBudgetProposals = budget.GetBudget();
    std::string strBudgetName = "main";
    std::vector<CTxBudgetPayment> vecTxBudgetPayments;

    for (auto& vBudgetProposal : vBudgetProposals) {
        CTxBudgetPayment txBudgetPayment;
        txBudgetPayment.nProposalHash = vBudgetProposal->GetHash();
        txBudgetPayment.payee = vBudgetProposal->GetPayee();
        txBudgetPayment.nAmount = vBudgetProposal->GetAllotted();
        vecTxBudgetPayments.push_back(txBudgetPayment);
    }

    if (vecTxBudgetPayments.size() < 1) {
        LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - Found No Proposals For Period\n");
        return;
    }

    CFinalizedBudget tempBudget(strBudgetName, nBlockStart, vecTxBudgetPayments, uint256());
    const uint256& budgetHash = tempBudget.GetHash();
    if (HaveFinalizedBudget(budgetHash)) {
        LogPrint("mnbudget", "%s: Budget already exists - %s\n", __func__, budgetHash.ToString());
        nSubmittedHeight = nCurrentHeight;
        return; // already exists
    }

    // create fee tx
    // CTransaction tx;
    uint256 txidCollateral;

    // See if collateral tx exists
    if (!mapUnconfirmedFeeTx.count(budgetHash)) {
        // create the collateral tx, send it to the network and return
        CWalletTx wtx;
        // Get our change address
        CReserveKey keyChange(pwalletMain);
        if (!pwalletMain->GetBudgetSystemCollateralTX(wtx, tempBudget.GetHash(), false)) {
            LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - Can't make collateral transaction\n");
            return;
        }

        // Get our change address
        CReserveKey reservekey(pwalletMain);
        // Send the tx to the network. Do NOT use SwiftTx, locking might need too much time to propagate, especially for testnet
        bool result = pwalletMain->CommitTransaction(wtx, reservekey, "NO-ix");
        if (result) {
            const uint256& collateraltxid = wtx.GetHash();
            mapUnconfirmedFeeTx.emplace(budgetHash, collateraltxid);
            LogPrint("mnbudget", "%s: Collateral sent. txid: %s\n", __func__, collateraltxid.ToString());
            return;
        }
    }

    // create the proposal incase we're the first to make it
    CFinalizedBudget fb(strBudgetName, nBlockStart, vecTxBudgetPayments, mapUnconfirmedFeeTx.at(budgetHash));

    // check
    int nConf = 0;
    int64_t nTime = 0;
    std::string strError = "";
    if (!AddFinalizedBudget(fb)) {
        LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - Invalid finalized budget - %s \n", strError);
        return;
    }

    if (!finalizedBudgetBroadcast.UpdateValid(nCurrentHeight)) {
        LogPrint("masternode", "%s: Invalid finalized budget - %s \n", __func__, finalizedBudgetBroadcast.IsInvalidReason());
        return;
    }

    LOCK(cs);
    AddSeenFinalizedBudget(finalizedBudgetBroadcast);
    finalizedBudgetBroadcast.Relay();
    budget.AddFinalizedBudget(finalizedBudgetBroadcast);
    nSubmittedHeight = nCurrentHeight;
    LogPrint("masternode", "CBudgetManager::SubmitFinalBudget - Done! %s\n", finalizedBudgetBroadcast.GetHash().ToString());
}

bool CBudgetManager::AddFinalizedBudget(CFinalizedBudget& finalizedBudget)
{
    AssertLockNotHeld(cs_budgets); // need to lock cs_main here (CheckCollateral)
    const uint256& nHash = finalizedBudget.GetHash();

    if (WITH_LOCK(cs_budgets, return mapFinalizedBudgets.count(nHash))) {
        LogPrint("mnbudget", "%s: finalized budget %s already added\n", __func__, nHash.ToString());
        return false;
    }

    if (!finalizedBudget.IsWellFormed(GetTotalBudget(finalizedBudget.GetBlockStart()))) {
        LogPrint("mnbudget", "%s: invalid finalized budget: %s %s\n", __func__, nHash.ToString(), finalizedBudget.IsInvalidLogStr());
        return false;
    }

    std::string strError;
    int nCurrentHeight = GetBestHeight();
    const uint256& feeTxId = finalizedBudget.GetFeeTXHash();
    if (!CheckCollateral(feeTxId, nHash, strError, finalizedBudget.nTime, nCurrentHeight, true)) {
        LogPrint("mnbudget", "%s: invalid finalized budget (%s) collateral id=%s - %s\n",
                 __func__, nHash.ToString(), feeTxId.ToString(), strError);
        finalizedBudget.SetStrInvalid(strError);
        return false;
    }

    // update expiration
    if (!finalizedBudget.UpdateValid(nCurrentHeight)) {
        LogPrint("mnbudget", "%s: invalid finalized budget: %s %s\n", __func__, nHash.ToString(), finalizedBudget.IsInvalidLogStr());
        return false;
    }

    SetBudgetProposalsStr(finalizedBudget);
    {
        LOCK(cs_budgets);
        mapFinalizedBudgets.emplace(nHash, finalizedBudget);
        // Add to feeTx index
        mapFeeTxToBudget.emplace(feeTxId, nHash);
        // Remove the budget from the unconfirmed map, if it was there
        if (mapUnconfirmedFeeTx.count(nHash))
            mapUnconfirmedFeeTx.erase(nHash);
    }
    LogPrint("mnbudget", "%s: finalized budget %s [%s (%s)] added\n",
             __func__, nHash.ToString(), finalizedBudget.GetName(), finalizedBudget.GetProposalsStr());
    return true;
}

bool CBudgetManager::AddProposal(CBudgetProposal& budgetProposal)
{
    LOCK(cs);
    if (!budgetProposal.UpdateValid(GetBestHeight())) {
        LogPrint("masternode", "CBudgetManager::AddProposal - invalid budget proposal - %s\n", budgetProposal.IsInvalidReason());
        return false;
    }

    if (mapProposals.count(budgetProposal.GetHash())) {
        return false;
    }

    mapProposals.insert(make_pair(budgetProposal.GetHash(), budgetProposal));
    LogPrint("masternode", "CBudgetManager::AddProposal - proposal %s added\n", budgetProposal.GetName().c_str());
    return true;
}

void CBudgetManager::CheckAndRemove()
{
    int nCurrentHeight = GetBestHeight();
    std::map<uint256, CFinalizedBudget> tmpMapFinalizedBudgets;
    std::map<uint256, CBudgetProposal> tmpMapProposals;

    LogPrint("mnbudget", "%s: mapFinalizedBudgets cleanup - size before: %d\n", __func__, mapFinalizedBudgets.size());
    for (auto& it : mapFinalizedBudgets) {
        CFinalizedBudget* pfinalizedBudget = &(it.second);
        if (!pfinalizedBudget->UpdateValid(nCurrentHeight)) {
            LogPrint("mnbudget", "%s: Invalid finalized budget: %s\n", __func__, pfinalizedBudget->IsInvalidReason());
        } else {
            LogPrint("mnbudget", "%s: Found valid finalized budget: %s %s\n", __func__,
                     pfinalizedBudget->GetName(), pfinalizedBudget->GetFeeTXHash().ToString());
            pfinalizedBudget->CheckAndVote();
            tmpMapFinalizedBudgets.insert(std::make_pair(pfinalizedBudget->GetHash(), *pfinalizedBudget));
        }
    }

    LogPrint("mnbudget", "%s: mapProposals cleanup - size before: %d\n", __func__, mapProposals.size());
    for (auto& it : mapProposals) {
        CBudgetProposal* pbudgetProposal = &(it.second);
        if (!pbudgetProposal->UpdateValid(nCurrentHeight)) {
            LogPrint("mnbudget", "%s: Invalid budget proposal - %s\n", __func__, pbudgetProposal->IsInvalidReason());
        } else {
            LogPrint("mnbudget", "%s: Found valid budget proposal: %s %s\n", __func__,
                     pbudgetProposal->GetName(), pbudgetProposal->GetFeeTXHash().ToString());
            tmpMapProposals.insert(std::make_pair(pbudgetProposal->GetHash(), *pbudgetProposal));
        }
    }

    // Remove invalid entries by overwriting complete map
    mapFinalizedBudgets.swap(tmpMapFinalizedBudgets);
    mapProposals.swap(tmpMapProposals);

    LogPrint("mnbudget", "%s: mapFinalizedBudgets cleanup - size after: %d\n", __func__, mapFinalizedBudgets.size());
    LogPrint("mnbudget", "%s: mapProposals cleanup - size after: %d\n", __func__, mapProposals.size());
    LogPrint("mnbudget", "%s: PASSED\n", __func__);
}


const CFinalizedBudget* CBudgetManager::GetBudgetWithHighestVoteCount(int chainHeight) const
{
    LOCK(cs_budgets);
    int highestVoteCount = 0;
    const CFinalizedBudget* pHighestBudget = nullptr;
    for (const auto& it : mapFinalizedBudgets) {
        const CFinalizedBudget* pfinalizedBudget = &(it.second);
        int voteCount = pfinalizedBudget->GetVoteCount();
        if (voteCount > highestVoteCount &&
            chainHeight >= pfinalizedBudget->GetBlockStart() &&
            chainHeight <= pfinalizedBudget->GetBlockEnd()) {
            pHighestBudget = pfinalizedBudget;
            highestVoteCount = voteCount;
        }
    }
    return pHighestBudget;
}

int CBudgetManager::GetHighestVoteCount(int chainHeight) const
{
    const CFinalizedBudget* pbudget = GetBudgetWithHighestVoteCount(chainHeight);
    return (pbudget ? pbudget->GetVoteCount() : -1);
}

bool CBudgetManager::GetPayeeAndAmount(int chainHeight, CScript& payeeRet, CAmount& nAmountRet) const
{
    int nCountThreshold;
    if (!IsBudgetPaymentBlock(chainHeight, nCountThreshold))
        return false;

    const CFinalizedBudget* pfb = GetBudgetWithHighestVoteCount(chainHeight);
    return pfb && pfb->GetPayeeAndAmount(chainHeight, payeeRet, nAmountRet) && pfb->GetVoteCount() > nCountThreshold;
}


void CBudgetManager::FillBlockPayee(CMutableTransaction& txNew)
{
    LOCK(cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev)
        return;

    int nHighestCount = 0;

    CScript payee;
    CAmount nAmount = 0;
    const int height = pindexPrev->nHeight;

    if (!GetPayeeAndAmount(nHeight, payee, nAmount))
        return;

    CAmount blockValue = GetBlockSubsidy(height + 1, Params().GetConsensus());

    // miners get the full amount on these blocks
    txNew.vout[0].nValue = blockValue;

    if ((height + 1 > 0) && (height + 1 <= Params().GetConsensus().GetLastFoundersRewardBlockHeight())) {
        CAmount vFoundersReward = 0;

        if (height + 1 < Params().GetConsensus().vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight) {
            vFoundersReward = txNew.vout[0].nValue / 20;
        } else if (height + 1 < Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KNOWHERE].nActivationHeight) {
            vFoundersReward = txNew.vout[0].nValue * 7.5 / 100;
        } else {
            vFoundersReward = txNew.vout[0].nValue * 15 / 100;
        }

        // And give it to the founders
        txNew.vout.push_back(CTxOut(vFoundersReward, Params().GetFoundersRewardScriptAtHeight(height + 1)));

        // Take some reward away from us
        txNew.vout[0].nValue -= vFoundersReward;
    }

    if ((height + 1 > 0) && (height + 1 <= Params().GetConsensus().GetLastTreasuryRewardBlockHeight())) {
        CAmount vTreasuryReward = 0;
        if (height + 1 >= Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KNOWHERE].nActivationHeight &&
            !NetworkUpgradeActive(height + 1, Params().GetConsensus(), Consensus::UPGRADE_ATLANTIS)) {
            vTreasuryReward = txNew.vout[0].nValue * 5 / 100;
        } else {
            vTreasuryReward = txNew.vout[0].nValue * 10 / 100;
        }

        // Take some reward away from us
        txNew.vout[0].nValue -= vTreasuryReward;

        // And give it to the founders
        if (height + 1 >= Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KNOWHERE].nActivationHeight) {
            txNew.vout.push_back(CTxOut(vTreasuryReward, Params().GetTreasuryRewardScriptAtHeight(height + 1)));
        }
    }

    if (Params().GetConsensus().NetworkUpgradeActive(height + 1, Consensus::UPGRADE_MORAG) &&
        height + 1 <= Params().GetConsensus().GetLastDevelopersRewardBlockHeight()) {
        const CAmount vDevelopersReward = GetDevelopersPayment(height + 1, blockValue);

        // And give it to the founders
        txNew.vout.push_back(CTxOut(vDevelopersReward, Params().GetDevelopersRewardScriptAtHeight(height + 1)));

        // Take some reward away from us
        txNew.vout[0].nValue -= vDevelopersReward;
    }

    if (nHighestCount > 0) {
        txNew.vout.push_back(CTxOut(nAmount, payee));

        CTxDestination address1;
        ExtractDestination(payee, address1);

        LogPrintf("Masternode payment to %s\n", EncodeDestination(address1));
    }
}


void CBudgetManager::VoteOnFinalizedBudgets()
{
    // function called only from initialized masternodes
    if (!fMasterNode) {
        LogPrint("mnbudget", "%s: Not a masternode\n", __func__);
        return;
    }
    if (activeMasternode.vin == nullopt) {
        LogPrint("mnbudget", "%s: Active Masternode not initialized\n", __func__);
        return;
    }

    // Do this 1 in 4 blocks -- spread out the voting activity
    // -- this function is only called every fourteenth block, so this is really 1 in 56 blocks
    if (rand() % 4 != 0) {
        LogPrint("mnbudget", "%s: waiting\n", __func__);
        return;
    }

    std::vector<CBudgetProposal> vBudget = GetBudget();
    if (vBudget.empty()) {
        LogPrint("mnbudget", "%s: No proposal can be finalized\n", __func__);
        return;
    }

    std::map<uint256, CBudgetProposal> mapWinningProposals;
    for (const CBudgetProposal& p : vBudget) {
        mapWinningProposals.emplace(p.GetHash(), p);
    }
    // Vector containing the hash of finalized budgets to sign
    std::vector<uint256> vBudgetHashes;
    {
        LOCK(cs_budgets);
        for (auto& it : mapFinalizedBudgets) {
            CFinalizedBudget* pfb = &(it.second);
            // we only need to check this once
            if (pfb->IsAutoChecked())
                continue;
            pfb->SetAutoChecked(true);
            // only vote for exact matches
            if (strBudgetMode == "auto") {
                // compare budget payements with winning proposals
                if (!pfb->CheckProposals(mapWinningProposals)) {
                    continue;
                }
            }
            // exact match found. add budget hash to sign it later.
            vBudgetHashes.emplace_back(pfb->GetHash());
        }
    }

    // Get masternode keys
    CPubKey pubKeyMasternode;
    CKey keyMasternode;
    activeMasternode.GetKeys(keyMasternode, pubKeyMasternode);

    // Sign finalized budgets
    for (const uint256& budgetHash : vBudgetHashes) {
        CFinalizedBudgetVote vote(*(activeMasternode.vin), budgetHash);
        if (!vote.Sign(keyMasternode, pubKeyMasternode)) {
            LogPrintf("%s: Failure to sign budget %s", __func__, budgetHash.ToString());
            continue;
        }
        std::string strError = "";
        if (!UpdateFinalizedBudget(vote, NULL, strError)) {
            LogPrintf("%s: Error submitting vote - %s\n", __func__, strError);
            continue;
        }
        LogPrint("mnbudget", "%s: new finalized budget vote signed: %s\n", __func__, vote.GetHash().ToString());
        AddSeenFinalizedBudgetVote(vote);
        vote.Relay();
    }
}

CFinalizedBudget* CBudgetManager::FindFinalizedBudget(const uint256& nHash)
{
    AssertLockHeld(cs_budgets);

    if (mapFinalizedBudgets.count(nHash))
        return &mapFinalizedBudgets[nHash];

    return NULL;
}


const CBudgetProposal* CBudgetManager::FindProposalByName(const std::string& strProposalName) const
{
    int64_t nYesCountMax = std::numeric_limits<int64_t>::min();
    const CBudgetProposal* pbudgetProposal = nullptr;

    for (const auto& it : mapProposals) {
        const CBudgetProposal& proposal = it.second;
        int64_t nYesCount = proposal.GetYeas() - proposal.GetNays();
        if (proposal.GetName() == strProposalName && nYesCount > nYesCountMax) {
            pbudgetProposal = &proposal;
            nYesCountMax = nYesCount;
        }
    }

    return pbudgetProposal;
}

CBudgetProposal* CBudgetManager::FindProposal(const uint256& nHash)
{
    LOCK(cs);

    if (mapProposals.count(nHash))
        return &mapProposals[nHash];

    return NULL;
}


std::string CBudgetManager::ToString() const
{
    std::ostringstream info;

    info << "Proposals: " << (int)mapProposals.size() << ", Budgets: " << (int)mapFinalizedBudgets.size() << ", Seen Budgets: " << (int)mapSeenMasternodeBudgetProposals.size() << ", Seen Budget Votes: " << (int)mapSeenMasternodeBudgetVotes.size() << ", Seen Final Budgets: " << (int)mapSeenFinalizedBudgets.size() << ", Seen Final Budget Votes: " << (int)mapSeenFinalizedBudgetVotes.size();

    return info.str();
}


CFinalizedBudget* CBudgetManager::FindFinalizedBudget(const uint256& nHash)
{
    if (mapFinalizedBudgets.count(nHash))
        return &mapFinalizedBudgets[nHash];

    return NULL;
}


/*
 * Check Collateral
 */
bool CheckCollateralConfs(const uint256& nTxCollateralHash, int nCurrentHeight, int nProposalHeight, std::string& strError)
{
    const int nRequiredConfs = Params().GetConsensus().nBudgetFeeConfirmations;
    const int nConf = nCurrentHeight - nProposalHeight + 1;

    if (nConf < nRequiredConfs) {
        strError = strprintf("Collateral requires at least %d confirmations - %d confirmations (current height: %d, fee tx height: %d)",
                             nRequiredConfs, nConf, nCurrentHeight, nProposalHeight);
        LogPrint("mnbudget", "%s: %s\n", __func__, strError);
        return false;
    }
    return true;
}

bool CheckCollateral(const uint256& nTxCollateralHash, const uint256& nExpectedHash, std::string& strError, int64_t& nTime, int nCurrentHeight, bool fBudgetFinalization)
{
    CTransaction txCollateral;
    uint256 nBlockHash;
    if (!GetTransaction(nTxCollateralHash, txCollateral, Params().GetConsensus(), nBlockHash, true)) {
        strError = strprintf("Can't find collateral tx %s", txCollateral.ToString());
        LogPrint("masternode", "CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }

    if (txCollateral.vout.size() < 1)
        return false;
    if (txCollateral.nLockTime > (unsigned int)chainActive.Height())
        return false;

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    bool foundOpReturn = false;
    for (const CTxOut o : txCollateral.vout) {
        if (!o.scriptPubKey.IsNormalPaymentScript() && !o.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral.ToString());
            LogPrint("masternode", "CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
            return false;
        }
        if (fBudgetFinalization) {
            // Collateral for budget finalization
            // Note: there are still old valid budgets out there, but the check for the new 5 PIV finalization collateral
            //       will also cover the old 50 PIV finalization collateral.
            LogPrint("mnbudget", "Final Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", HexStr(o.scriptPubKey), HexStr(findScript));
            if (o.scriptPubKey == findScript) {
                LogPrint("mnbudget", "Final Budget: o.nValue(%ld) >= BUDGET_FEE_TX(%ld) ?\n", o.nValue, BUDGET_FEE_TX);
                if (o.nValue >= BUDGET_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        } else {
            // Collateral for normal budget proposal
            LogPrint("mnbudget", "Normal Budget: o.scriptPubKey(%s) == findScript(%s) ?\n", HexStr(o.scriptPubKey), HexStr(findScript));
            if (o.scriptPubKey == findScript) {
                LogPrint("mnbudget", "Normal Budget: o.nValue(%ld) >= PROPOSAL_FEE_TX(%ld) ?\n", o.nValue, PROPOSAL_FEE_TX);
                if (o.nValue >= PROPOSAL_FEE_TX) {
                    foundOpReturn = true;
                }
            }
        }
    }

    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral.ToString());
        LogPrint("masternode", "CBudgetProposalBroadcast::IsBudgetCollateralValid - %s\n", strError);
        return false;
    }
    // Retrieve block height (checking that it's in the active chain) and time
    // both get set in CBudgetProposal/CFinalizedBudget by the caller (AddProposal/AddFinalizedBudget)
    if (nBlockHash.IsNull()) {
        strError = strprintf("Collateral transaction %s is unconfirmed", nTxCollateralHash.ToString());
        return false;
    }
    nTime = 0;
    int nProposalHeight = 0;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nProposalHeight = pindex->nHeight;
                nTime = pindex->nTime;
            }
        }
    }

    if (!nProposalHeight) {
        strError = strprintf("Collateral transaction %s not in Active chain", nTxCollateralHash.ToString());
        return false;
    }

    return CheckCollateralConfs(nTxCollateralHash, nCurrentHeight, nProposalHeight, strError);
}
