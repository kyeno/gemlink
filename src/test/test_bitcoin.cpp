// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#define BOOST_TEST_MODULE Bitcoin Test Suite

#include "test_bitcoin.h"

#include "crypto/common.h"
#include "key.h"
#include "main.h"
#include "random.h"
#include "rpc/register.h"
#include "rpc/server.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#endif

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>

#include "librustzcash.h"

// CClientUIInterface uiInterface; // Declared but not defined in ui_interface.h
const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;
uint256 insecure_rand_seed = GetRandHash();
FastRandomContext insecure_rand_ctx(insecure_rand_seed);

extern bool fPrintToConsole;
extern void noui_connect();

JoinSplitTestingSetup::JoinSplitTestingSetup()
{
    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    static_assert(
        sizeof(fs::path::value_type) == sizeof(codeunit),
        "librustzcash not configured correctly");
    auto sapling_spend_str = sapling_spend.native();
    auto sapling_output_str = sapling_output.native();
    auto sprout_groth16_str = sprout_groth16.native();

    librustzcash_init_zksnark_params(
        reinterpret_cast<const codeunit*>(sapling_spend_str.c_str()),
        sapling_spend_str.length(),
        reinterpret_cast<const codeunit*>(sapling_output_str.c_str()),
        sapling_output_str.length(),
        reinterpret_cast<const codeunit*>(sprout_groth16_str.c_str()),
        sprout_groth16_str.length());
}

JoinSplitTestingSetup::~JoinSplitTestingSetup()
{
}

BasicTestingSetup::BasicTestingSetup()
{
    assert(sodium_init() != -1);
    ECC_Start();
    SetupEnvironment();
    fPrintToDebugLog = false; // don't want to write to debug.log file
    fCheckBlockIndex = true;
    SelectParams(CBaseChainParams::MAIN);
}
BasicTestingSetup::~BasicTestingSetup()
{
    ECC_Stop();
}

TestingSetup::TestingSetup()
{
    // Ideally we'd move all the RPC tests to the functional testing framework
    // instead of unit tests, but for now we need these here.
    RegisterAllCoreRPCCommands(tableRPC);
#ifdef ENABLE_WALLET
    bitdb.MakeMock();
    RegisterWalletRPCCommands(tableRPC);
#endif

    // Save current path, in case a test changes it
    orig_current_path = fs::current_path();

    ClearDatadirCache();
    pathTemp = fs::temp_directory_path() / strprintf("test_bitcoin_%lu_%i", (unsigned long)GetTime(), (int)(GetRand(100000)));
    fs::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    pblocktree = new CBlockTreeDB(1 << 20, true);
    pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    InitBlockIndex();
#ifdef ENABLE_WALLET
    bool fFirstRun;
    pwalletMain = new CWallet("wallet.dat");
    pwalletMain->LoadWallet(fFirstRun);
    RegisterValidationInterface(pwalletMain);
#endif
    nScriptCheckThreads = 3;
    for (int i = 0; i < nScriptCheckThreads - 1; i++)
        threadGroup.create_thread(&ThreadScriptCheck);
    RegisterNodeSignals(GetNodeSignals());
}

TestingSetup::~TestingSetup()
{
    UnregisterNodeSignals(GetNodeSignals());
    threadGroup.interrupt_all();
    threadGroup.join_all();
#ifdef ENABLE_WALLET
    UnregisterValidationInterface(pwalletMain);
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    UnloadBlockIndex();
    delete pcoinsTip;
    delete pcoinsdbview;
    delete pblocktree;
#ifdef ENABLE_WALLET
    bitdb.Flush(true);
    bitdb.Reset();
#endif

    // Restore the previous current path so temporary directory can be deleted
    fs::current_path(orig_current_path);

    fs::remove_all(pathTemp);
}


CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(CMutableTransaction& tx, CTxMemPool* pool)
{
    return CTxMemPoolEntry(tx, nFee, nTime, dPriority, nHeight,
                           pool ? pool->HasNoInputsOf(tx) : hadNoDependencies,
                           spendsCoinbase, nBranchId);
}

void Shutdown(void* parg)
{
    exit(0);
}
