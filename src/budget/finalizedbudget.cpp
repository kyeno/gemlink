// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "budget/finalizedbudget.h"

#include "masternodeman.h"

CFinalizedBudget::CFinalizedBudget() : fAutoChecked(false),
                                       fValid(true),
                                       strBudgetName(""),
                                       strInvalid(),
                                       nBlockStart(0),
                                       vecBudgetPayments(),
                                       mapVotes(),
                                       nTime(0),
                                       nFeeTXHash(uint256())
{
}
CFinalizedBudget::CFinalizedBudget(const std::string& name,
                                   int blockstart,
                                   const std::vector<CTxBudgetPayment>& vecBudgetPaymentsIn,
                                   const uint256& nfeetxhash) : fAutoChecked(false),
                                                                fValid(true),
                                                                strInvalid(),
                                                                mapVotes(),
                                                                strBudgetName(name),
                                                                nBlockStart(blockstart),
                                                                vecBudgetPayments(vecBudgetPaymentsIn),
                                                                nFeeTXHash(nfeetxhash),
                                                                strProposals(""),
                                                                nTime(0)
{
}


bool CFinalizedBudget::AddOrUpdateVote(const CFinalizedBudgetVote& vote, std::string& strError)
{
    LOCK(cs);

    const uint256& hash = vote.GetVin().prevout.GetHash();
    const int64_t voteTime = vote.GetTime();
    std::string strAction = "New vote inserted:";

    if (mapVotes.count(hash)) {
        const int64_t oldTime = mapVotes[hash].GetTime();
        if (oldTime > voteTime) {
            strError = strprintf("new vote older than existing vote - %s\n", vote.GetHash().ToString());
            LogPrint("mnbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        if (voteTime - oldTime < BUDGET_VOTE_UPDATE_MIN) {
            strError = strprintf("time between votes is too soon - %s - %lli sec < %lli sec\n", vote.GetHash().ToString(), voteTime - oldTime, BUDGET_VOTE_UPDATE_MIN);
            LogPrint("mnbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
            return false;
        }
        strAction = "Existing vote updated:";
    }

    if (voteTime > GetTime() + (60 * 60)) {
        strError = strprintf("new vote is too far ahead of current time - %s - nTime %lli - Max Time %lli\n", vote.GetHash().ToString(), voteTime, GetTime() + (60 * 60));
        LogPrint("mnbudget", "CFinalizedBudget::AddOrUpdateVote - %s\n", strError);
        return false;
    }

    mapVotes[hash] = vote;
    LogPrint("mnbudget", "CFinalizedBudget::AddOrUpdateVote - %s %s\n", strAction.c_str(), vote.GetHash().ToString().c_str());
    return true;
}

UniValue CFinalizedBudget::GetVotesObject() const
{
    LOCK(cs);
    UniValue ret(UniValue::VOBJ);
    for (const auto& it : mapVotes) {
        const CFinalizedBudgetVote& vote = it.second;
        ret.push_back(std::make_pair(vote.GetVin().prevout.ToStringShort(), vote.ToJSON()));
    }
    return ret;
}

void CFinalizedBudget::SetSynced(bool synced)
{
    LOCK(cs);
    for (auto& it : mapVotes) {
        CFinalizedBudgetVote& vote = it.second;
        if (synced) {
            if (vote.IsValid())
                vote.SetSynced(true);
        } else {
            vote.SetSynced(false);
        }
    }
}


// evaluate if we should vote for this. Masternode only
void CFinalizedBudget::CheckAndVote()
{
    if (!fMasterNode || fAutoChecked)
        return;

    // do this 1 in 4 blocks -- spread out the voting activity on mainnet
    //  -- this function is only called every fourteenth block, so this is really 1 in 56 blocks
    if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN && rand() % 4 != 0) {
        LogPrint("masternode", "CFinalizedBudget::AutoCheck - waiting\n");
        return;
    }

    fAutoChecked = true; // we only need to check this once


    if (strBudgetMode == "auto") // only vote for exact matches
    {
        LOCK(cs);
        std::vector<CBudgetProposal*> vBudgetProposals = budget.GetBudget();


        for (unsigned int i = 0; i < vecBudgetPayments.size(); i++) {
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - nProp %d %s\n", i, vecBudgetPayments[i].nProposalHash.ToString());
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - Payee %d %s\n", i, vecBudgetPayments[i].payee.ToString());
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - nAmount %d %lli\n", i, vecBudgetPayments[i].nAmount);
        }

        for (unsigned int i = 0; i < vBudgetProposals.size(); i++) {
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - nProp %d %s\n", i, vBudgetProposals[i]->GetHash().ToString());
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - Payee %d %s\n", i, vBudgetProposals[i]->GetPayee().ToString());
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - nAmount %d %lli\n", i, vBudgetProposals[i]->GetAmount());
        }

        if (vBudgetProposals.size() == 0) {
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - Can't get Budget, aborting\n");
            return;
        }

        if (vBudgetProposals.size() != vecBudgetPayments.size()) {
            LogPrint("masternode", "CFinalizedBudget::AutoCheck - Budget length doesn't match. vBudgetProposals.size()=%ld != vecBudgetPayments.size()=%ld\n",
                     vBudgetProposals.size(), vecBudgetPayments.size());
            return;
        }


        for (unsigned int i = 0; i < vecBudgetPayments.size(); i++) {
            if (i > vBudgetProposals.size() - 1) {
                LogPrint("masternode", "CFinalizedBudget::AutoCheck - Proposal size mismatch, i=%d > (vBudgetProposals.size() - 1)=%d\n", i, vBudgetProposals.size() - 1);
                return;
            }

            if (vecBudgetPayments[i].nProposalHash != vBudgetProposals[i]->GetHash()) {
                LogPrint("masternode", "CFinalizedBudget::AutoCheck - item #%d doesn't match %s %s\n", i, vecBudgetPayments[i].nProposalHash.ToString(), vBudgetProposals[i]->GetHash().ToString());
                return;
            }

            // if(vecBudgetPayments[i].payee != vBudgetProposals[i]->GetPayee()){ -- triggered with false positive
            if (vecBudgetPayments[i].payee.ToString() != vBudgetProposals[i]->GetPayee().ToString()) {
                LogPrint("masternode", "CFinalizedBudget::AutoCheck - item #%d payee doesn't match %s %s\n", i, vecBudgetPayments[i].payee.ToString(), vBudgetProposals[i]->GetPayee().ToString());
                return;
            }

            if (vecBudgetPayments[i].nAmount != vBudgetProposals[i]->GetAmount()) {
                LogPrint("masternode", "CFinalizedBudget::AutoCheck - item #%d payee doesn't match %lli %lli\n", i, vecBudgetPayments[i].nAmount, vBudgetProposals[i]->GetAmount());
                return;
            }
        }

        LogPrint("masternode", "CFinalizedBudget::AutoCheck - Finalized Budget Matches! Submitting Vote.\n");
        SubmitVote();
    }
}
// If masternode voted for a proposal, but is now invalid -- remove the vote
void CFinalizedBudget::CleanAndRemove()
{
    std::map<uint256, CFinalizedBudgetVote>::iterator it = mapVotes.begin();

    while (it != mapVotes.end()) {
        CMasternode* pmn = mnodeman.Find((*it).second.GetVin());
        (*it).second.SetValid(pmn != nullptr);
        ++it;
    }
}


CAmount CFinalizedBudget::GetTotalPayout() const
{
    CAmount ret = 0;

    for (auto& vecBudgetPayment : vecBudgetPayments) {
        ret += vecBudgetPayment.nAmount;
    }

    return ret;
}

std::string CFinalizedBudget::GetProposals()
{
    LOCK(cs);
    std::string ret = "";

    for (CTxBudgetPayment& budgetPayment : vecBudgetPayments) {
        CBudgetProposal* pbudgetProposal = budget.FindProposal(budgetPayment.nProposalHash);

        std::string token = budgetPayment.nProposalHash.ToString();

        if (pbudgetProposal)
            token = pbudgetProposal->GetName();
        if (ret == "") {
            ret = token;
        } else {
            ret += "," + token;
        }
    }
    return ret;
}

std::string CFinalizedBudget::GetStatus() const
{
    std::string retBadHashes = "";
    std::string retBadPayeeOrAmount = "";

    for (int nBlockHeight = GetBlockStart(); nBlockHeight <= GetBlockEnd(); nBlockHeight++) {
        CTxBudgetPayment budgetPayment;
        if (!GetBudgetPaymentByBlock(nBlockHeight, budgetPayment)) {
            LogPrint("masternode", "CFinalizedBudget::GetStatus - Couldn't find budget payment for block %lld\n", nBlockHeight);
            continue;
        }

        const CBudgetProposal* pbudgetProposal = budget.FindProposal(budgetPayment.nProposalHash);
        if (!pbudgetProposal) {
            if (retBadHashes == "") {
                retBadHashes = "Unknown proposal hash! Check this proposal before voting" + budgetPayment.nProposalHash.ToString();
            } else {
                retBadHashes += "," + budgetPayment.nProposalHash.ToString();
            }
        } else {
            if (pbudgetProposal->GetPayee() != budgetPayment.payee || pbudgetProposal->GetAmount() != budgetPayment.nAmount) {
                if (retBadPayeeOrAmount == "") {
                    retBadPayeeOrAmount = "Budget payee/nAmount doesn't match our proposal! " + budgetPayment.nProposalHash.ToString();
                } else {
                    retBadPayeeOrAmount += "," + budgetPayment.nProposalHash.ToString();
                }
            }
        }
    }

    if (retBadHashes == "" && retBadPayeeOrAmount == "")
        return "OK";

    return retBadHashes + retBadPayeeOrAmount;
}

void CFinalizedBudget::SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const
{
    LOCK(cs);
    for (const auto& it : mapVotes) {
        const CFinalizedBudgetVote& vote = it.second;
        if (vote.IsValid() && (!fPartial || !vote.IsSynced())) {
            pfrom->PushInventory(CInv(MSG_BUDGET_FINALIZED_VOTE, vote.GetHash()));
            nInvCount++;
        }
    }
}

bool CFinalizedBudget::UpdateValid(int nCurrentHeight, bool fCheckCollateral)
{
    fValid = false;
    // Must be the correct block for payment to happen (once a month)
    if (nBlockStart % GetBudgetPaymentCycleBlocks() != 0) {
        strInvalid = "Invalid BlockStart";
        return false;
    }

    // The following 2 checks check the same (basically if vecBudgetPayments.size() > 100)
    if (GetBlockEnd() - nBlockStart > 100) {
        strInvalid = "Invalid BlockEnd";
        return false;
    }
    if ((int)vecBudgetPayments.size() > 100) {
        strInvalid = "Invalid budget payments count (too many)";
        return false;
    }
    if (strBudgetName == "") {
        strInvalid = "Invalid Budget Name";
        return false;
    }
    if (nBlockStart == 0) {
        strInvalid = "Budget " + strBudgetName + " Invalid BlockStart == 0";
        return false;
    }
    if (nFeeTXHash == uint256()) {
        strInvalid = "Budget " + strBudgetName + " Invalid FeeTx == 0";
        return false;
    }

    // Can only pay out 10% of the possible coins (min value of coins)
    if (GetTotalPayout() > budget.GetTotalBudget(nBlockStart)) {
        strInvalid = "Budget " + strBudgetName + " Invalid Payout (more than max)";
        return false;
    }

    std::string strError2 = "";
    if (fCheckCollateral) {
        int nConf = 0;
        if (!IsBudgetCollateralValid(nFeeTXHash, GetHash(), strError2, nTime, nConf)) {
            {
                strInvalid = "Budget " + strBudgetName + " Invalid Collateral : " + strError2;
                return false;
            }
        }
    }

    // TODO: if N cycles old, invalid, invalid

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL)
        return true;

    // TODO: verify if we can safely remove this
    //
    //    if (nBlockStart < pindexPrev->nHeight - 100) {
    //        strError = "Budget " + strBudgetName + " Older than current blockHeight" ;
    //        return false;
    //    }
    fValid = true;
    strInvalid.clear();
    return true;
}

bool CFinalizedBudget::IsPaidAlready(uint256 nProposalHash, int nBlockHeight) const
{
    // Remove budget-payments from former/future payment cycles
    std::map<uint256, int>::iterator it = mapPayment_History.begin();
    int nPaidBlockHeight = 0;
    uint256 nOldProposalHash;

    for (it = mapPayment_History.begin(); it != mapPayment_History.end(); /* No incrementation needed */) {
        nPaidBlockHeight = (*it).second;
        if ((nPaidBlockHeight < GetBlockStart()) || (nPaidBlockHeight > GetBlockEnd())) {
            nOldProposalHash = (*it).first;
            LogPrint("mnbudget", "%s: Budget Proposal %s, Block %d from old cycle deleted\n",
                     __func__, nOldProposalHash.ToString().c_str(), nPaidBlockHeight);
            mapPayment_History.erase(it++);
        } else {
            ++it;
        }
    }

    // Now that we only have payments from the current payment cycle check if this budget was paid already
    if (mapPayment_History.count(nProposalHash) == 0) {
        // New proposal payment, insert into map for checks with later blocks from this cycle
        mapPayment_History.insert(std::pair<uint256, int>(nProposalHash, nBlockHeight));
        LogPrint("mnbudget", "%s: Budget Proposal %s, Block %d added to payment history\n",
                 __func__, nProposalHash.ToString().c_str(), nBlockHeight);
        return false;
    }
    // This budget was paid already -> reject transaction so it gets paid to a masternode instead
    return true;
}


TrxValidationStatus CFinalizedBudget::IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const
{
    TrxValidationStatus transactionStatus = TrxValidationStatus::InValid;
    int nCurrentBudgetPayment = nBlockHeight - GetBlockStart();
    if (nCurrentBudgetPayment < 0) {
        LogPrint("masternode", "CFinalizedBudget::IsTransactionValid - Invalid block - height: %d start: %d\n", nBlockHeight, GetBlockStart());
        return TrxValidationStatus::InValid;
    }

    if (nCurrentBudgetPayment > (int)vecBudgetPayments.size() - 1) {
        LogPrint("masternode", "CFinalizedBudget::IsTransactionValid - Invalid block - current budget payment: %d of %d\n", nCurrentBudgetPayment + 1, (int)vecBudgetPayments.size());
        return TrxValidationStatus::InValid;
    }

    bool paid = false;
    for (const CTxOut& out : txNew.vout) {
        if (vecBudgetPayments[nCurrentBudgetPayment].payee == out.scriptPubKey && vecBudgetPayments[nCurrentBudgetPayment].nAmount == out.nValue) {
            // Check if this proposal was paid already. If so, pay a masternode instead
            paid = IsPaidAlready(vecBudgetPayments[nCurrentBudgetPayment].nProposalHash, nBlockHeight);
            if (paid) {
                LogPrint("mnbudget", "%s: Double Budget Payment of %d for proposal %d detected. Paying a masternode instead.\n",
                         __func__, vecBudgetPayments[nCurrentBudgetPayment].nAmount, vecBudgetPayments[nCurrentBudgetPayment].nProposalHash.GetHex());
                // No matter what we've found before, stop all checks here. In future releases there might be more than one budget payment
                // per block, so even if the first one was not paid yet this one disables all budget payments for this block.
                transactionStatus = TrxValidationStatus::DoublePayment;
                break;
            } else {
                transactionStatus = TrxValidationStatus::Valid;
                LogPrint("mnbudget", "%s: Found valid Budget Payment of %d for proposal %d\n", __func__,
                         vecBudgetPayments[nCurrentBudgetPayment].nAmount, vecBudgetPayments[nCurrentBudgetPayment].nProposalHash.GetHex());
            }
        }
    }

    if (transactionStatus == TrxValidationStatus::InValid) {
        CTxDestination address1;
        ExtractDestination(vecBudgetPayments[nCurrentBudgetPayment].payee, address1);

        LogPrint("masternode", "CFinalizedBudget::IsTransactionValid - Missing required payment - %s: %d c: %d\n",
                 EncodeDestination(address1), vecBudgetPayments[nCurrentBudgetPayment].nAmount, nCurrentBudgetPayment);
    }

    return transactionStatus;
}

bool CFinalizedBudget::GetBudgetPaymentByBlock(int64_t nBlockHeight, CTxBudgetPayment& payment) const
{
    LOCK(cs);

    int i = nBlockHeight - GetBlockStart();
    if (i < 0)
        return false;
    if (i > (int)vecBudgetPayments.size() - 1)
        return false;
    payment = vecBudgetPayments[i];
    return true;
}

bool CFinalizedBudget::GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount) const
{
    LOCK(cs);

    int i = nBlockHeight - GetBlockStart();
    if (i < 0)
        return false;
    if (i > (int)vecBudgetPayments.size() - 1)
        return false;
    payee = vecBudgetPayments[i].payee;
    nAmount = vecBudgetPayments[i].nAmount;
    return true;
}

void CFinalizedBudget::SubmitVote()
{
    CPubKey pubKeyMasternode;
    CKey keyMasternode;
    bool fNewSigs = false;
    {
        LOCK(cs_main);
        fNewSigs = NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_MORAG);
    }

    if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode, fNewSigs)) {
        LogPrint("mnbudget", "CFinalizedBudget::SubmitVote - Error upon calling GetKeysFromSecret\n");
        return;
    }

    CFinalizedBudgetVote vote(activeMasternode.vin, GetHash());
    if (!vote.SignMessage(keyMasternode, pubKeyMasternode, fNewSigs)) {
        LogPrint("mnbudget", "CFinalizedBudget::SubmitVote - Failure to sign.");
        return;
    }

    std::string strError = "";
    if (budget.UpdateFinalizedBudget(vote, NULL, strError)) {
        LogPrint("masternode", "CFinalizedBudget::SubmitVote  - new finalized budget vote - %s\n", vote.GetHash().ToString());

        budget.AddSeenFinalizedBudgetVote(vote);
        vote.Relay();
    } else {
        LogPrint("masternode", "CFinalizedBudget::SubmitVote : Error submitting vote - %s\n", strError);
    }
}
