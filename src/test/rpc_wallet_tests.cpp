// Copyright (c) 2013-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/client.h"
#include "rpc/server.h"

#include "key_io.h"
#include "main.h"
#include "wallet/wallet.h"

#include "test/test_bitcoin.h"

#include "zcash/Address.hpp"

#include "asyncrpcoperation.h"
#include "asyncrpcqueue.h"
#include "wallet/asyncrpcoperation_mergetoaddress.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"

#include "init.h"

#include <array>
#include <chrono>
#include <thread>

#include <fstream>
#include <unordered_set>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

using namespace std;

extern UniValue createArgs(int nRequired, const char* address1 = NULL, const char* address2 = NULL);
extern UniValue CallRPC(string args);

extern CWallet* pwalletMain;

bool find_error(const UniValue& objError, const std::string& expected)
{
    return find_value(objError, "message").get_str().find(expected) != string::npos;
}

static UniValue ValueFromString(const std::string& str)
{
    UniValue value;
    BOOST_CHECK(value.setNumStr(str));
    return value;
}

BOOST_FIXTURE_TEST_SUITE(rpc_wallet_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(rpc_addmultisig)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    rpcfn_type addmultisig = tableRPC["addmultisigaddress"]->actor;

    // old, 65-byte-long:
    const char address1Hex[] = "0434e3e09f49ea168c5bbf53f877ff4206923858aab7c7e1df25bc263978107c95e35065a27ef6f1b27222db0ec97e0e895eaca603d3ee0d4c060ce3d8a00286c8";
    // new, compressed:
    const char address2Hex[] = "0388c2037017c62240b6b72ac1a2a5f94da790596ebd06177c8572752922165cb4";

    KeyIO keyIO(Params());
    UniValue v;
    CTxDestination address;
    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex, address2Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(2, address1Hex, address2Hex), false));
    address = keyIO.DecodeDestination(v.get_str());
    BOOST_CHECK(IsValidDestination(address) && IsScriptDestination(address));

    BOOST_CHECK_THROW(addmultisig(createArgs(0), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(2, address1Hex), false), runtime_error);

    BOOST_CHECK_THROW(addmultisig(createArgs(1, ""), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1, "NotAValidPubkey"), false), runtime_error);

    string short1(address1Hex, address1Hex + sizeof(address1Hex) - 2); // last byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short1.c_str()), false), runtime_error);

    string short2(address1Hex + 1, address1Hex + sizeof(address1Hex)); // first byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short2.c_str()), false), runtime_error);
}


BOOST_AUTO_TEST_CASE(rpc_wallet)
{
    // Test RPC calls for various wallet statistics
    UniValue r;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    KeyIO keyIO(Params());
    CPubKey demoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination demoAddress(CTxDestination(demoPubkey.GetID()));
    UniValue retValue;
    string strAccount = "";
    string strPurpose = "receive";
    BOOST_CHECK_NO_THROW({ /*Initialize Wallet with an account */
                           CWalletDB walletdb(pwalletMain->strWalletFile);
                           CAccount account;
                           account.vchPubKey = demoPubkey;
                           pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, strPurpose);
                           walletdb.WriteAccount(strAccount, account);
    });

    CPubKey setaccountDemoPubkey = pwalletMain->GenerateNewKey();
    CTxDestination setaccountDemoAddress(CTxDestination(setaccountDemoPubkey.GetID()));

    /*********************************
     * 			setaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("setaccount " + keyIO.EncodeDestination(setaccountDemoAddress) + " \"\""));
    /* Accounts are disabled */
    BOOST_CHECK_THROW(CallRPC("setaccount " + keyIO.EncodeDestination(setaccountDemoAddress) + " nullaccount"), runtime_error);
    /* t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1GpgV is not owned by the test wallet. */
    BOOST_CHECK_THROW(CallRPC("setaccount t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1GpgV nullaccount"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("setaccount"), runtime_error);
    /* t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg (34 chars) is an illegal address (should be 35 chars) */
    BOOST_CHECK_THROW(CallRPC("setaccount t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg nullaccount"), runtime_error);


    /*********************************
     *                  getbalance
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getbalance"));
    BOOST_CHECK_THROW(CallRPC("getbalance " + keyIO.EncodeDestination(demoAddress)), runtime_error);

    /*********************************
     * 			listunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listunspent"));
    BOOST_CHECK_THROW(CallRPC("listunspent string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 not_array"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 [] extra"), runtime_error);
    BOOST_CHECK_NO_THROW(r = CallRPC("listunspent 0 1 []"));
    BOOST_CHECK(r.get_array().empty());

    /*********************************
     * 		listreceivedbyaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 true extra"), runtime_error);

    /*********************************
     * 		listreceivedbyaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 true extra"), runtime_error);

    /*********************************
     *          listsinceblock
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listsinceblock"));

    /*********************************
     *          listtransactions
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions"));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress)));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " 20"));
    BOOST_CHECK_NO_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " 20 0"));
    BOOST_CHECK_THROW(CallRPC("listtransactions " + keyIO.EncodeDestination(demoAddress) + " not_int"), runtime_error);

    /*********************************
     *          listlockunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listlockunspent"));

    /*********************************
     *          listaccounts
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listaccounts"));

    /*********************************
     *          listaddressgroupings
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listaddressgroupings"));

    /*********************************
     * 		getrawchangeaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getrawchangeaddress"));

    /*********************************
     * 		getnewaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress \"\""));
    /* Accounts are deprecated */
    BOOST_CHECK_THROW(CallRPC("getnewaddress getnewaddress_demoaccount"), runtime_error);

    /*********************************
     * 		getaccountaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress \"\""));
    /* Accounts are deprecated */
    BOOST_CHECK_THROW(CallRPC("getaccountaddress accountThatDoesntExists"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaccountaddress " + strAccount));
    BOOST_CHECK(keyIO.DecodeDestination(retValue.get_str()) == demoAddress);

    /*********************************
     * 			getaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getaccount " + keyIO.EncodeDestination(demoAddress)));

    /*********************************
     * 	signmessage + verifymessage
     *********************************/
    BOOST_CHECK_NO_THROW(retValue = CallRPC("signmessage " + keyIO.EncodeDestination(demoAddress) + " mymessage"));
    BOOST_CHECK_THROW(CallRPC("signmessage"), runtime_error);
    /* Should throw error because this address is not loaded in the wallet */
    BOOST_CHECK_THROW(CallRPC("signmessage t1h8SqgtM3QM5e2M8EzhhT1yL2PXXtA6oqe mymessage"), runtime_error);

    /* missing arguments */
    BOOST_CHECK_THROW(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress)), runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str()), runtime_error);
    /* Illegal address */
    BOOST_CHECK_THROW(CallRPC("verifymessage t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1Gpg " + retValue.get_str() + " mymessage"), runtime_error);
    /* wrong address */
    BOOST_CHECK(CallRPC("verifymessage t1VtArtnn1dGPiD2WFfMXYXW5mHM3q1GpgV " + retValue.get_str() + " mymessage").get_bool() == false);
    /* Correct address and signature but wrong message */
    BOOST_CHECK(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str() + " wrongmessage").get_bool() == false);
    /* Correct address, message and signature*/
    BOOST_CHECK(CallRPC("verifymessage " + keyIO.EncodeDestination(demoAddress) + " " + retValue.get_str() + " mymessage").get_bool() == true);

    /*********************************
     * 		getaddressesbyaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaddressesbyaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaddressesbyaccount " + strAccount));
    UniValue arr = retValue.get_array();
    BOOST_CHECK_EQUAL(4, arr.size());
    bool notFound = true;
    for (auto a : arr.getValues()) {
        notFound &= keyIO.DecodeDestination(a.get_str()) != demoAddress;
    }
    BOOST_CHECK(!notFound);

    /*********************************
     * 	     fundrawtransaction
     *********************************/
    BOOST_CHECK_THROW(CallRPC("fundrawtransaction 28z"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("fundrawtransaction 01000000000180969800000000001976a91450ce0a4b0ee0ddeb633da85199728b940ac3fe9488ac00000000"), runtime_error);

    /*
     * getblocksubsidy
     */
    BOOST_CHECK_THROW(CallRPC("getblocksubsidy too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblocksubsidy -1"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 50000"));
    UniValue obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), 10.0);
    BOOST_CHECK_EQUAL(find_value(obj, "founders").get_real(), 2.5);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 1000000"));
    obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), 6.25);
    BOOST_CHECK_EQUAL(find_value(obj, "founders").get_real(), 0.0);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getblocksubsidy 2000000"));
    obj = retValue.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "miner").get_real(), 3.125);
    BOOST_CHECK_EQUAL(find_value(obj, "founders").get_real(), 0.0);

    /*
     * getblock
     */
    BOOST_CHECK_THROW(CallRPC("getblock too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblock -1"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("getblock 2147483647"), runtime_error); // allowed, but > height of active chain tip
    BOOST_CHECK_THROW(CallRPC("getblock 2147483648"), runtime_error); // not allowed, > int32 used for nHeight
    BOOST_CHECK_THROW(CallRPC("getblock 100badchars"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 0"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 1"));
    BOOST_CHECK_NO_THROW(CallRPC("getblock 0 2"));
    BOOST_CHECK_THROW(CallRPC("getblock 0 -1"), runtime_error); // bad verbosity
    BOOST_CHECK_THROW(CallRPC("getblock 0 3"), runtime_error);  // bad verbosity
}

BOOST_AUTO_TEST_CASE(rpc_wallet_getbalance)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);


    BOOST_CHECK_THROW(CallRPC("z_getbalance too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getbalance invalidaddress"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_getbalance tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab"));
    BOOST_CHECK_THROW(CallRPC("z_getbalance tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab -1"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_getbalance tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab 0"));
    BOOST_CHECK_THROW(CallRPC("z_getbalance tnRZ8bPq2pff3xBWhTJhNkVUkm2uhzksDeW5PvEa7aFKGT9Qi3YgTALZfjaY4jU3HLVKBtHdSXxoPoLA3naMPcHBcY88FcF 1"), runtime_error);


    BOOST_CHECK_THROW(CallRPC("z_gettotalbalance too manyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_gettotalbalance -1"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("z_gettotalbalance 0"));


    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress too many args"), runtime_error);
    // negative minconf not allowed
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab -1"), runtime_error);
    // invalid zaddr, taddr not allowed
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tmC6YZnCUhm19dEXxh3Jb7srdBJxDawaCab 0"), runtime_error);
    // don't have the spending key
    BOOST_CHECK_THROW(CallRPC("z_listreceivedbyaddress tnRZ8bPq2pff3xBWhTJhNkVUkm2uhzksDeW5PvEa7aFKGT9Qi3YgTALZfjaY4jU3HLVKBtHdSXxoPoLA3naMPcHBcY88FcF 1"), runtime_error);
}

/**
 * This test covers RPC command z_validateaddress
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_validateaddress)
{
    SelectParams(CBaseChainParams::MAIN);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // Check number of args
    BOOST_CHECK_THROW(CallRPC("z_validateaddress"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_validateaddress toomany args"), runtime_error);

    // Wallet should be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);

    // This address is not valid, it belongs to another network
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress ztaaga95QAPyp1kSQ1hD2kguCpzyMHjxWZqaYDEkzbvo7uYQYAw2S8X4Kx98AvhhofMtQL8PAXKHuZsmhRcanavKRKmdCzk"));
    UniValue resultObj = retValue.get_obj();
    bool b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // This address is valid, but the spending key is not in this wallet
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress zcfA19SDAKRYHLoRDoShcoz4nPohqWxuHcqg8WAxsiB2jFrrs6k7oSvst3UZvMYqpMNSRBkxBsnyjjngX5L55FxMzLKach8"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "type").get_str(), "sprout");
    b = find_value(resultObj, "ismine").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // Let's import a spending key to the wallet and validate its payment address
    BOOST_CHECK_NO_THROW(CallRPC("z_importkey SKxoWv77WGwFnUJitQKNEcD636bL4X5Gd6wWmgaA4Q9x8jZBPJXT"));
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress zcWsmqT4X2V4jgxbgiCzyrAfRT1vi1F4sn7M5Pkh66izzw8Uk7LBGAH3DtcSMJeUb2pi3W4SQF8LMKkU2cUuVP68yAGcomL"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "type").get_str(), "sprout");
    b = find_value(resultObj, "ismine").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "payingkey").get_str(), "f5bb3c888ccc9831e3f6ba06e7528e26a312eec3acc1823be8918b6a3a5e20ad");
    BOOST_CHECK_EQUAL(find_value(resultObj, "transmissionkey").get_str(), "7a58c7132446564e6b810cf895c20537b3528357dc00150a8e201f491efa9c1a");

    // This Sapling address is not valid, it belongs to another network
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress ztestsapling1knww2nyjc62njkard0jmx7hlsj6twxmxwprn7anvrv4dc2zxanl3nemc0qx2hvplxmd2uau8gyw"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, false);

    // This Sapling address is valid, but the spending key is not in this wallet
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_validateaddress zs1z7rejlpsa98s2rrrfkwmaxu53e4ue0ulcrw0h4x5g8jl04tak0d3mm47vdtahatqrlkngh9slya"));
    resultObj = retValue.get_obj();
    b = find_value(resultObj, "isvalid").get_bool();
    BOOST_CHECK_EQUAL(b, true);
    BOOST_CHECK_EQUAL(find_value(resultObj, "type").get_str(), "sapling");
    b = find_value(resultObj, "ismine").get_bool();
    BOOST_CHECK_EQUAL(b, false);
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifier").get_str(), "1787997c30e94f050c634d");
    BOOST_CHECK_EQUAL(find_value(resultObj, "diversifiedtransmissionkey").get_str(), "34ed1f60f5db5763beee1ddbb37dd5f7e541d4d4fbdcc09fbfcc6b8e949bbe9d");
}

/*
 * This test covers RPC command z_exportwallet
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_exportwallet)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // wallet should be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);

    // wallet should have one key
    libzcash::SproutPaymentAddress addr = pwalletMain->GenerateNewSproutZKey();
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 1);

    // Set up paths
    boost::filesystem::path tmppath = boost::filesystem::temp_directory_path();
    boost::filesystem::path tmpfilename = boost::filesystem::unique_path("%%%%%%%%");
    boost::filesystem::path exportfilepath = tmppath / tmpfilename;

    // export will fail since exportdir is not set
    BOOST_CHECK_THROW(CallRPC(string("z_exportwallet ") + tmpfilename.string()), runtime_error);

    // set exportdir
    mapArgs["-exportdir"] = tmppath.string();

    // run some tests
    BOOST_CHECK_THROW(CallRPC("z_exportwallet"), runtime_error);

    BOOST_CHECK_THROW(CallRPC("z_exportwallet toomany args"), runtime_error);

    BOOST_CHECK_THROW(CallRPC(string("z_exportwallet invalid!*/_chars.txt")), runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC(string("z_exportwallet ") + tmpfilename.string()));


    libzcash::SproutSpendingKey key;
    BOOST_CHECK(pwalletMain->GetSproutSpendingKey(addr, key));
    KeyIO keyIO(Params());
    std::string s1 = keyIO.EncodePaymentAddress(addr);
    std::string s2 = keyIO.EncodeSpendingKey(key);

    // There's no way to really delete a private key so we will read in the
    // exported wallet file and search for the spending key and payment address.

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(exportfilepath.string().c_str(), std::ios::in | std::ios::ate);
    BOOST_CHECK(file.is_open());
    bool fVerified = false;
    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);
    while (file.good()) {
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line.find(s1) != std::string::npos && line.find(s2) != std::string::npos) {
            fVerified = true;
            break;
        }
    }
    BOOST_CHECK(fVerified);
}


/*
 * This test covers RPC command z_importwallet
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_importwallet)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    KeyIO keyIO(Params());
    // error if no args
    BOOST_CHECK_THROW(CallRPC("z_importwallet"), runtime_error);

    // error if too many args
    BOOST_CHECK_THROW(CallRPC("z_importwallet toomany args"), runtime_error);

    // create a random key locally
    auto testSpendingKey = libzcash::SproutSpendingKey::random();
    auto testPaymentAddress = testSpendingKey.address();
    std::string testAddr = keyIO.EncodePaymentAddress(testPaymentAddress);
    std::string testKey = keyIO.EncodeSpendingKey(testSpendingKey);

    // create test data using the random key
    std::string format_str = "# Wallet dump created by Zcash v0.11.2.0.z8-9155cc6-dirty (2016-08-11 11:37:00 -0700)\n"
                             "# * Created on 2016-08-12T21:55:36Z\n"
                             "# * Best block at time of backup was 0 (0de0a3851fef2d433b9b4f51d4342bdd24c5ddd793eb8fba57189f07e9235d52),\n"
                             "#   mined on 2009-01-03T18:15:05Z\n"
                             "\n"
                             "# Zkeys\n"
                             "\n"
                             "%s 2016-08-12T21:55:36Z # zaddr=%s\n"
                             "\n"
                             "\n# End of dump";

    boost::format formatobject(format_str);
    std::string testWalletDump = (formatobject % testKey % testAddr).str();

    // write test data to file
    boost::filesystem::path temp = boost::filesystem::temp_directory_path() /
                                   boost::filesystem::unique_path();
    const std::string path = temp.string();
    std::ofstream file(path);
    file << testWalletDump;
    file << std::flush;

    // wallet should currently be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);

    // import test data from file into wallet
    BOOST_CHECK_NO_THROW(CallRPC(string("z_importwallet ") + path));

    // wallet should now have one zkey
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 1);

    // check that we have the spending key for the address
    auto address = keyIO.DecodePaymentAddress(testAddr);
    BOOST_CHECK(IsValidPaymentAddress(address));
    BOOST_ASSERT(std::get_if<libzcash::SproutPaymentAddress>(&address) != nullptr);
    auto addr = std::get<libzcash::SproutPaymentAddress>(address);
    BOOST_CHECK(pwalletMain->HaveSproutSpendingKey(addr));

    // Verify the spending key is the same as the test data
    libzcash::SproutSpendingKey k;
    BOOST_CHECK(pwalletMain->GetSproutSpendingKey(addr, k));
    BOOST_CHECK_EQUAL(testKey, keyIO.EncodeSpendingKey(k));
}


/*
 * This test covers RPC commands z_listaddresses, z_importkey, z_exportkey
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_z_importexport)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n1 = 1000; // number of times to import/export
    int n2 = 1000; // number of addresses to create and list
    KeyIO keyIO(Params());
    // error if no args
    BOOST_CHECK_THROW(CallRPC("z_importkey"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_exportkey"), runtime_error);

    // error if too many args
    BOOST_CHECK_THROW(CallRPC("z_importkey way too many args"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_exportkey toomany args"), runtime_error);

    // error if invalid args
    auto sk = libzcash::SproutSpendingKey::random();
    std::string prefix = std::string("z_importkey ") + keyIO.EncodeSpendingKey(sk) + " yes ";
    BOOST_CHECK_THROW(CallRPC(prefix + "-1"), runtime_error);
    BOOST_CHECK_THROW(CallRPC(prefix + "2147483647"), runtime_error); // allowed, but > height of active chain tip
    BOOST_CHECK_THROW(CallRPC(prefix + "2147483648"), runtime_error); // not allowed, > int32 used for nHeight
    BOOST_CHECK_THROW(CallRPC(prefix + "100badchars"), runtime_error);

    // wallet should currently be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);
    std::set<libzcash::SaplingPaymentAddress> saplingAddrs;
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK(saplingAddrs.empty());

    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);

    // verify import and export key
    for (int i = 0; i < n1; i++) {
        // create a random Sprout key locally
        auto testSpendingKey = libzcash::SproutSpendingKey::random();
        auto testPaymentAddress = testSpendingKey.address();
        std::string testAddr = keyIO.EncodePaymentAddress(testPaymentAddress);
        std::string testKey = keyIO.EncodeSpendingKey(testSpendingKey);
        BOOST_CHECK_NO_THROW(CallRPC(string("z_importkey ") + testKey));
        BOOST_CHECK_NO_THROW(retValue = CallRPC(string("z_exportkey ") + testAddr));
        BOOST_CHECK_EQUAL(retValue.get_str(), testKey);

        // create a random Sapling key locally
        auto testSaplingSpendingKey = m.Derive(i);
        auto testSaplingPaymentAddress = testSaplingSpendingKey.DefaultAddress();
        std::string testSaplingAddr = keyIO.EncodePaymentAddress(testSaplingPaymentAddress);
        std::string testSaplingKey = keyIO.EncodeSpendingKey(testSaplingSpendingKey);
        BOOST_CHECK_NO_THROW(CallRPC(string("z_importkey ") + testSaplingKey));
        BOOST_CHECK_NO_THROW(retValue = CallRPC(string("z_exportkey ") + testSaplingAddr));
        BOOST_CHECK_EQUAL(retValue.get_str(), testSaplingKey);
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK(arr.size() == (2 * n1));

    // Put addresses into a set
    std::unordered_set<std::string> myaddrs;
    for (UniValue element : arr.getValues()) {
        myaddrs.insert(element.get_str());
    }

    // Make new addresses for the set
    for (int i = 0; i < n2; i++) {
        myaddrs.insert(keyIO.EncodePaymentAddress(pwalletMain->GenerateNewSproutZKey()));
    }

    // Verify number of addresses stored in wallet is n1+n2
    int numAddrs = myaddrs.size();
    BOOST_CHECK(numAddrs == (2 * n1) + n2);
    pwalletMain->GetSproutPaymentAddresses(addrs);
    pwalletMain->GetSaplingPaymentAddresses(saplingAddrs);
    BOOST_CHECK(addrs.size() + saplingAddrs.size() == numAddrs);

    // Ask wallet to list addresses
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == numAddrs);

    // Create a set from them
    std::unordered_set<std::string> listaddrs;
    for (UniValue element : arr.getValues()) {
        listaddrs.insert(element.get_str());
    }

    // Verify the two sets of addresses are the same
    BOOST_CHECK(listaddrs.size() == numAddrs);
    BOOST_CHECK(myaddrs == listaddrs);

    // Add one more address
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getnewaddress sprout"));
    std::string newaddress = retValue.get_str();
    auto address = keyIO.DecodePaymentAddress(newaddress);
    BOOST_CHECK(IsValidPaymentAddress(address));
    BOOST_ASSERT(std::get_if<libzcash::SproutPaymentAddress>(&address) != nullptr);
    auto newAddr = std::get<libzcash::SproutPaymentAddress>(address);
    BOOST_CHECK(pwalletMain->HaveSproutSpendingKey(newAddr));

    // Check if too many args
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress toomanyargs"), runtime_error);
}


/**
 * Test Async RPC operations.
 * Tip: Create mock operations by subclassing AsyncRPCOperation.
 */

class MockSleepOperation : public AsyncRPCOperation
{
public:
    std::chrono::milliseconds naptime;
    MockSleepOperation(int t = 1000)
    {
        this->naptime = std::chrono::milliseconds(t);
    }
    virtual ~MockSleepOperation()
    {
    }
    virtual void main()
    {
        set_state(OperationStatus::EXECUTING);
        start_execution_clock();
        std::this_thread::sleep_for(std::chrono::milliseconds(naptime));
        stop_execution_clock();
        set_result(UniValue(UniValue::VSTR, "done"));
        set_state(OperationStatus::SUCCESS);
    }
};


/*
 * Test Aysnc RPC queue and operations.
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations)
{
    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    BOOST_CHECK(q->getNumberOfWorkers() == 0);
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size() == 0);

    std::shared_ptr<AsyncRPCOperation> op1 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    BOOST_CHECK(q->getOperationCount() == 1);

    OperationStatus status = op1->getState();
    BOOST_CHECK(status == OperationStatus::READY);

    AsyncRPCOperationId id1 = op1->getId();
    int64_t creationTime1 = op1->getCreationTime();

    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 1);

    // an AsyncRPCOperation doesn't do anything so will finish immediately
    std::this_thread::sleep_for(std::chrono::seconds(1));
    BOOST_CHECK(q->getOperationCount() == 0);

    // operation should be a success
    BOOST_CHECK_EQUAL(op1->isCancelled(), false);
    BOOST_CHECK_EQUAL(op1->isExecuting(), false);
    BOOST_CHECK_EQUAL(op1->isReady(), false);
    BOOST_CHECK_EQUAL(op1->isFailed(), false);
    BOOST_CHECK_EQUAL(op1->isSuccess(), true);
    BOOST_CHECK_EQUAL(op1->getError().isNull(), true);
    BOOST_CHECK_EQUAL(op1->getResult().isNull(), false);
    BOOST_CHECK_EQUAL(op1->getStateAsString(), "success");
    BOOST_CHECK_NE(op1->getStateAsString(), "executing");

    // Create a second operation which just sleeps
    std::shared_ptr<AsyncRPCOperation> op2(new MockSleepOperation(2500));
    AsyncRPCOperationId id2 = op2->getId();
    int64_t creationTime2 = op2->getCreationTime();

    // it's different from the previous operation
    BOOST_CHECK_NE(op1.get(), op2.get());
    BOOST_CHECK_NE(id1, id2);
    BOOST_CHECK_NE(creationTime1, creationTime2);

    // Only the first operation has been added to the queue
    std::vector<AsyncRPCOperationId> v = q->getAllOperationIds();
    std::set<AsyncRPCOperationId> opids(v.begin(), v.end());
    BOOST_CHECK(opids.size() == 1);
    BOOST_CHECK(opids.count(id1) == 1);
    BOOST_CHECK(opids.count(id2) == 0);
    std::shared_ptr<AsyncRPCOperation> p1 = q->getOperationForId(id1);
    BOOST_CHECK_EQUAL(p1.get(), op1.get());
    std::shared_ptr<AsyncRPCOperation> p2 = q->getOperationForId(id2);
    BOOST_CHECK(!p2); // null ptr as not added to queue yet

    // Add operation 2 and 3 to the queue
    q->addOperation(op2);
    std::shared_ptr<AsyncRPCOperation> op3(new MockSleepOperation(1000));
    q->addOperation(op3);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    BOOST_CHECK_EQUAL(op2->isExecuting(), true);
    op2->cancel(); // too late, already executing
    op3->cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    BOOST_CHECK_EQUAL(op2->isSuccess(), true);
    BOOST_CHECK_EQUAL(op2->isCancelled(), false);
    BOOST_CHECK_EQUAL(op3->isCancelled(), true);


    v = q->getAllOperationIds();
    std::copy(v.begin(), v.end(), std::inserter(opids, opids.end()));
    BOOST_CHECK(opids.size() == 3);
    BOOST_CHECK(opids.count(id1) == 1);
    BOOST_CHECK(opids.count(id2) == 1);
    BOOST_CHECK(opids.count(op3->getId()) == 1);
    q->finishAndWait();
}


// The CountOperation will increment this global
std::atomic<int64_t> gCounter(0);

class CountOperation : public AsyncRPCOperation
{
public:
    CountOperation() {}
    virtual ~CountOperation() {}
    virtual void main()
    {
        set_state(OperationStatus::EXECUTING);
        gCounter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        set_state(OperationStatus::SUCCESS);
    }
};

// This tests the queue waiting for multiple workers to finish
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations_parallel_wait)
{
    gCounter = 0;

    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 4);

    int64_t numOperations = 10; // 10 * 1000ms / 4 = 2.5 secs to finish
    for (int i = 0; i < numOperations; i++) {
        std::shared_ptr<AsyncRPCOperation> op(new CountOperation());
        q->addOperation(op);
    }

    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size() == numOperations);
    q->finishAndWait();
    BOOST_CHECK_EQUAL(q->isFinishing(), true);
    BOOST_CHECK_EQUAL(numOperations, gCounter.load());
}

// This tests the queue shutting down immediately
BOOST_AUTO_TEST_CASE(rpc_wallet_async_operations_parallel_cancel)
{
    gCounter = 0;

    std::shared_ptr<AsyncRPCQueue> q = std::make_shared<AsyncRPCQueue>();
    q->addWorker();
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 2);

    int numOperations = 10000; // 10000 seconds to complete
    for (int i = 0; i < numOperations; i++) {
        std::shared_ptr<AsyncRPCOperation> op(new CountOperation());
        q->addOperation(op);
    }
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size() == numOperations);
    q->closeAndWait();

    int numSuccess = 0;
    int numCancelled = 0;
    for (auto& id : ids) {
        std::shared_ptr<AsyncRPCOperation> ptr = q->popOperationForId(id);
        if (ptr->isCancelled()) {
            numCancelled++;
        } else if (ptr->isSuccess()) {
            numSuccess++;
        }
    }

    BOOST_CHECK_EQUAL(numOperations, numSuccess + numCancelled);
    BOOST_CHECK_EQUAL(gCounter.load(), numSuccess);
    BOOST_CHECK(q->getOperationCount() == 0);
    ids = q->getAllOperationIds();
    BOOST_CHECK(ids.size() == 0);
}

// This tests z_getoperationstatus, z_getoperationresult, z_listoperationids
BOOST_AUTO_TEST_CASE(rpc_z_getoperations)
{
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCQueue> sharedInstance = AsyncRPCQueue::sharedInstance();
    BOOST_CHECK(q == sharedInstance);

    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus []"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationstatus [\"opid-1234\"]"));
    BOOST_CHECK_THROW(CallRPC("z_getoperationstatus [] toomanyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getoperationstatus not_an_array"), runtime_error);

    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult []"));
    BOOST_CHECK_NO_THROW(CallRPC("z_getoperationresult [\"opid-1234\"]"));
    BOOST_CHECK_THROW(CallRPC("z_getoperationresult [] toomanyargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_getoperationresult not_an_array"), runtime_error);

    std::shared_ptr<AsyncRPCOperation> op1 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op1);
    std::shared_ptr<AsyncRPCOperation> op2 = std::make_shared<AsyncRPCOperation>();
    q->addOperation(op2);

    BOOST_CHECK(q->getOperationCount() == 2);
    BOOST_CHECK(q->getNumberOfWorkers() == 0);
    q->addWorker();
    BOOST_CHECK(q->getNumberOfWorkers() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    BOOST_CHECK(q->getOperationCount() == 0);

    // Check if too many args
    BOOST_CHECK_THROW(CallRPC("z_listoperationids toomany args"), runtime_error);

    UniValue retValue;
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listoperationids"));
    BOOST_CHECK(retValue.get_array().size() == 2);

    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    UniValue array = retValue.get_array();
    BOOST_CHECK(array.size() == 2);

    // idempotent
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    BOOST_CHECK(array.size() == 2);

    for (UniValue v : array.getValues()) {
        UniValue obj = v.get_obj();
        UniValue id = find_value(obj, "id");

        UniValue result;
        // removes result from internal storage
        BOOST_CHECK_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        UniValue resultArray = result.get_array();
        BOOST_CHECK(resultArray.size() == 1);

        UniValue resultObj = resultArray[0].get_obj();
        UniValue resultId = find_value(resultObj, "id");
        BOOST_CHECK_EQUAL(id.get_str(), resultId.get_str());

        // verify the operation has been removed
        BOOST_CHECK_NO_THROW(result = CallRPC("z_getoperationresult [\"" + id.get_str() + "\"]"));
        resultArray = result.get_array();
        BOOST_CHECK(resultArray.size() == 0);
    }

    // operations removed
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getoperationstatus"));
    array = retValue.get_array();
    BOOST_CHECK(array.size() == 0);

    q->close();
}

BOOST_AUTO_TEST_CASE(rpc_z_sendmany_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);
    KeyIO keyIO(Params());
    BOOST_CHECK_THROW(CallRPC("z_sendmany"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_sendmany toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_sendmany just too many args here"), runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ []"),
                      runtime_error);
    // empty amounts
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ []"),
                      runtime_error);

    // don't have the spending key for this address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"
                              "UkJ1oSfbhTJhm72WiZizvkZz5aH1 []"),
                      runtime_error);

    // duplicate address
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0},"
                              " {\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":12.0} ]"),
                      runtime_error);

    // invalid fee amount, cannot be negative
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
                              "1 -0.0001"),
                      runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
                              "1 21000001"),
                      runtime_error);

    // fee amount is bigger than sum of outputs
    BOOST_CHECK_THROW(CallRPC("z_sendmany "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "[{\"address\":\"tmQP9L3s31cLsghVYf2Jb5MhKj1jRBPoeQn\", \"amount\":50.0}] "
                              "1 50.00000001"),
                      runtime_error);

    // memo bigger than allowed length of ZC_MEMO_SIZE
    std::vector<char> v(2 * (ZC_MEMO_SIZE + 1)); // x2 for hexadecimal string format
    std::fill(v.begin(), v.end(), 'A');
    std::string badmemo(v.begin(), v.end());
    auto pa = pwalletMain->GenerateNewSproutZKey();
    std::string zaddr1 = keyIO.EncodePaymentAddress(pa);
    BOOST_CHECK_THROW(CallRPC(string("z_sendmany tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ ") + "[{\"address\":\"" + zaddr1 + "\", \"amount\":123.456}]"), runtime_error);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test constructor of AsyncRPCOperation_sendmany
    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "", {}, {}, -1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Minconf cannot be negative"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "", {}, {}, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "From address parameter missing"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ", {}, {}, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "No recipients"));
    }

    try {
        std::vector<SendManyRecipient> recipients = {SendManyRecipient("dummy", 1.0, "")};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "INVALID", recipients, {}, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Invalid from address"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<SendManyRecipient> recipients = {SendManyRecipient("dummy", 1.0, "")};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U", recipients, {}, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Invalid from address"));
    }

    // Note: The following will crash as a google test because AsyncRPCOperation_sendmany
    // invokes a method on pwalletMain, which is undefined in the google test environment.
    try {
        std::vector<SendManyRecipient> recipients = {SendManyRecipient("dummy", 1.0, "")};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, "ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP", recipients, {}, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "no spending key found for zaddr"));
    }
}


// TODO: test private methods
BOOST_AUTO_TEST_CASE(rpc_z_sendmany_internals)
{
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& consensusParams = Params().GetConsensus();

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // Mutable tx containing contextual information we need to build tx
    // We removed the ability to create pre-Sapling Sprout proofs, so we can
    // only create Sapling-onwards transactions.
    int nHeight = consensusParams.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // add keys manually
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getnewaddress"));
    std::string taddr1 = retValue.get_str();
    auto pa = pwalletMain->GenerateNewSproutZKey();
    KeyIO keyIO(Params());
    std::string zaddr1 = keyIO.EncodePaymentAddress(pa);

    // there are no utxos to spend
    {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient(zaddr1, 100*COIN, "DEADBEEF") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, taddr1, {}, recipients, 1) );
        operation->main();
        BOOST_CHECK(operation->isFailed());
        std::string msg = operation->getErrorMessage();
        BOOST_CHECK( msg.find("Insufficient transparent funds") != string::npos);
    }

    // minconf cannot be zero when sending from zaddr
    {
        try {
            std::vector<SendManyRecipient> recipients = {SendManyRecipient(taddr1, 100*COIN, "DEADBEEF")};
            std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, recipients, {}, 0));
            BOOST_CHECK(false); // Fail test if an exception is not thrown
        } catch (const UniValue& objError) {
            BOOST_CHECK(find_error(objError, "Minconf cannot be zero when sending from zaddr"));
        }
    }


    // there are no unspent notes to spend
    {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient(taddr1, 100*COIN, "DEADBEEF") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, recipients, {}, 1) );
        operation->main();
        BOOST_CHECK(operation->isFailed());
        std::string msg = operation->getErrorMessage();
        BOOST_CHECK( msg.find("Insufficient funds, no unspent notes") != string::npos);
    }

    // get_memo_from_hex_string())
    {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient(zaddr1, 100*COIN, "DEADBEEF") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, recipients, {}, 1) );
        std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany> (operation);
        TEST_FRIEND_AsyncRPCOperation_sendmany proxy(ptr);

        std::string memo = "DEADBEEF";
        std::array<unsigned char, ZC_MEMO_SIZE> array = proxy.get_memo_from_hex_string(memo);
        BOOST_CHECK_EQUAL(array[0], 0xDE);
        BOOST_CHECK_EQUAL(array[1], 0xAD);
        BOOST_CHECK_EQUAL(array[2], 0xBE);
        BOOST_CHECK_EQUAL(array[3], 0xEF);
        for (int i=4; i<ZC_MEMO_SIZE; i++) {
            BOOST_CHECK_EQUAL(array[i], 0x00);  // zero padding
        }

        // memo is longer than allowed
        std::vector<char> v (2 * (ZC_MEMO_SIZE+1));
        std::fill(v.begin(),v.end(), 'A');
        std::string bigmemo(v.begin(), v.end());

        try {
            proxy.get_memo_from_hex_string(bigmemo);
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "too big"));
        }

        // invalid hexadecimal string
        std::fill(v.begin(),v.end(), '@'); // not a hex character
        std::string badmemo(v.begin(), v.end());

        try {
            proxy.get_memo_from_hex_string(badmemo);
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "hexadecimal format"));
        }

        // odd length hexadecimal string
        std::fill(v.begin(),v.end(), 'A');
        v.resize(v.size() - 1);
        assert(v.size() %2 == 1); // odd length
        std::string oddmemo(v.begin(), v.end());
        try {
            proxy.get_memo_from_hex_string(oddmemo);
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "hexadecimal format"));
        }
    }


    // add_taddr_change_output_to_tx() will append a vout to a raw transaction
    {
        std::vector<SendManyRecipient> recipients = { SendManyRecipient(zaddr1, 100*COIN, "DEADBEEF") };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, recipients, {}, 1) );
        std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany> (operation);
        TEST_FRIEND_AsyncRPCOperation_sendmany proxy(ptr);

        CTransaction tx = proxy.getTx();
        BOOST_CHECK(tx.vout.size() == 0);

        CReserveKey keyChange(pwalletMain);
        CAmount amount = 12345600000;
        proxy.add_taddr_change_output_to_tx(keyChange, amount);
        tx = proxy.getTx();
        BOOST_CHECK(tx.vout.size() == 1);
        CTxOut out = tx.vout[0];
        BOOST_CHECK_EQUAL(out.nValue, amount);

        amount = 111100000;
        proxy.add_taddr_change_output_to_tx(keyChange, amount);
        tx = proxy.getTx();
        BOOST_CHECK(tx.vout.size() == 2);
        out = tx.vout[1];
        BOOST_CHECK_EQUAL(out.nValue, amount);
    }

    // add_taddr_outputs_to_tx() will append many vouts to a raw transaction
    {
        std::vector<SendManyRecipient> recipients = {
            SendManyRecipient("tmTGScYwiLMzHe4uGZtBYmuqoW4iEoYNMXt", 123000000, ""),
            SendManyRecipient("tmUSbHz3vxnwLvRyNDXbwkZxjVyDodMJEhh", 456000000, ""),
            SendManyRecipient("tmYZAXYPCP56Xa5JQWWPZuK7o7bfUQW6kkd", 789000000, ""),
        };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, recipients, {}, 1) );
        std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany> (operation);
        TEST_FRIEND_AsyncRPCOperation_sendmany proxy(ptr);

        proxy.add_taddr_outputs_to_tx();

        CTransaction tx = proxy.getTx();
        BOOST_CHECK(tx.vout.size() == 3);
        BOOST_CHECK_EQUAL(tx.vout[0].nValue, 123000000);
        BOOST_CHECK_EQUAL(tx.vout[1].nValue, 456000000);
        BOOST_CHECK_EQUAL(tx.vout[2].nValue, 789000000);
    }

    // Test the perform_joinsplit methods.
    {
        // Dummy input so the operation object can be instantiated.
        std::vector<SendManyRecipient> recipients = { SendManyRecipient(zaddr1, 50000, "ABCD") };

        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(std::nullopt, mtx, zaddr1, {}, recipients, 1) );
        std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany> (operation);
        TEST_FRIEND_AsyncRPCOperation_sendmany proxy(ptr);

        // Enable test mode so tx is not sent and proofs are not generated
        static_cast<AsyncRPCOperation_sendmany *>(operation.get())->testmode = true;

        AsyncJoinSplitInfo info;
        std::vector<std::optional < SproutWitness>> witnesses;
        uint256 anchor;
        try {
            proxy.perform_joinsplit(info, witnesses, anchor);
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("anchor is null")!= string::npos);
        }

        try {
            std::vector<JSOutPoint> v;
            proxy.perform_joinsplit(info, v);
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("anchor is null")!= string::npos);
        }

        info.notes.push_back(SproutNote());
        try {
            proxy.perform_joinsplit(info);
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("number of notes")!= string::npos);
        }

        info.notes.clear();
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        try {
            proxy.perform_joinsplit(info);
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("unsupported joinsplit input")!= string::npos);
        }

        info.vjsin.clear();
        try {
            proxy.perform_joinsplit(info);
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("error verifying joinsplit")!= string::npos);
        }
    }
}



BOOST_AUTO_TEST_CASE(rpc_z_sendmany_taddr_to_sapling)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    LOCK(pwalletMain->cs_wallet);
    KeyIO keyIO(Params());
    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    UniValue retValue;

    // add keys manually
    auto taddr = pwalletMain->GenerateNewKey().GetID();
    std::string taddr1 = keyIO.EncodeDestination(taddr);
    auto pa = pwalletMain->GenerateNewSaplingZKey();
    std::string zaddr1 = keyIO.EncodePaymentAddress(pa);

    auto consensusParams = Params().GetConsensus();
    retValue = CallRPC("getblockcount");
    int nextBlockHeight = retValue.get_int() + 1;

    // Add a fake transaction to the wallet
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight);
    CScript scriptPubKey = CScript() << OP_DUP << OP_HASH160 << ToByteVector(taddr) << OP_EQUALVERIFY << OP_CHECKSIG;
    mtx.vout.push_back(CTxOut(5 * COIN, scriptPubKey));
    CWalletTx wtx(pwalletMain, mtx);
    pwalletMain->AddToWallet(wtx, true, NULL);

    // Fake-mine the transaction
    BOOST_CHECK_EQUAL(0, chainActive.Height());
    CBlock block;
    block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
    block.vtx.push_back(wtx);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex fakeIndex{block};
    fakeIndex.nHeight = 1;
    mapBlockIndex.insert(std::make_pair(blockHash, &fakeIndex));
    chainActive.SetTip(&fakeIndex);
    BOOST_CHECK(chainActive.Contains(&fakeIndex));
    BOOST_CHECK_EQUAL(1, chainActive.Height());
    wtx.SetMerkleBranch(block);
    pwalletMain->AddToWallet(wtx, true, NULL);

    // Context that z_sendmany requires
    auto builder = TransactionBuilder(consensusParams, nextBlockHeight, pwalletMain);
    mtx = CreateNewContextualCMutableTransaction(consensusParams, nextBlockHeight);

    std::vector<SendManyRecipient> recipients = {SendManyRecipient(zaddr1, 1 * COIN, "ABCD")};
    std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sendmany(builder, mtx, taddr1, {}, recipients, 0));
    std::shared_ptr<AsyncRPCOperation_sendmany> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_sendmany>(operation);

    // Enable test mode so tx is not sent
    static_cast<AsyncRPCOperation_sendmany*>(operation.get())->testmode = true;

    // Generate the Sapling shielding transaction
    operation->main();
    BOOST_CHECK(operation->isSuccess());

    // Get the transaction
    auto result = operation->getResult();
    BOOST_ASSERT(result.isObject());
    auto hexTx = result["hex"].getValStr();
    CDataStream ss(ParseHex(hexTx), SER_NETWORK, PROTOCOL_VERSION);
    CTransaction tx;
    ss >> tx;
    BOOST_ASSERT(!tx.vShieldedOutput.empty());

    // We shouldn't be able to decrypt with the empty ovk
    BOOST_CHECK(!AttemptSaplingOutDecryption(
        tx.vShieldedOutput[0].outCiphertext,
        uint256(),
        tx.vShieldedOutput[0].cv,
        tx.vShieldedOutput[0].cm,
        tx.vShieldedOutput[0].ephemeralKey));

    // We should be able to decrypt the outCiphertext with the ovk
    // generated for transparent addresses
    HDSeed seed;
    BOOST_ASSERT(pwalletMain->GetHDSeed(seed));
    BOOST_CHECK(AttemptSaplingOutDecryption(
        tx.vShieldedOutput[0].outCiphertext,
        ovkForShieldingFromTaddr(seed),
        tx.vShieldedOutput[0].cv,
        tx.vShieldedOutput[0].cm,
        tx.vShieldedOutput[0].ephemeralKey));

    // Tear down
    chainActive.SetTip(NULL);
    mapBlockIndex.erase(blockHash);
    mapArgs.erase("-developersapling");
    mapArgs.erase("-experimentalfeatures");

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}


/*
 * This test covers storing encrypted zkeys in the wallet.
 */
BOOST_AUTO_TEST_CASE(rpc_wallet_encrypted_wallet_zkeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n = 100;

    // wallet should currently be empty
    std::set<libzcash::SproutPaymentAddress> addrs;
    pwalletMain->GetSproutPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);

    // create keys
    for (int i = 0; i < n; i++) {
        CallRPC("z_getnewaddress sprout");
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Verify that the wallet encryption RPC is disabled
    BOOST_CHECK_THROW(CallRPC("encryptwallet passphrase"), runtime_error);

    // Encrypt the wallet (we can't call RPC encryptwallet as that shuts down node)
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";

    boost::filesystem::current_path(GetArg("-datadir", "/tmp/thisshouldnothappen"));
    BOOST_CHECK(pwalletMain->EncryptWallet(strWalletPass));

    // Verify we can still list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Try to add a new key, but we can't as the wallet is locked
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress sprout"), runtime_error);

    // We can't call RPC walletpassphrase as that invokes RPCRunLater which breaks tests.
    // So we manually unlock.
    BOOST_CHECK(pwalletMain->Unlock(strWalletPass));

    // Now add a key
    BOOST_CHECK_NO_THROW(CallRPC("z_getnewaddress sprout"));

    // Verify the key has been added
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n + 1);

    // We can't simulate over RPC the wallet closing and being reloaded
    // but there are tests for this in gtest.
}

BOOST_AUTO_TEST_CASE(rpc_wallet_encrypted_wallet_sapzkeys)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    UniValue retValue;
    int n = 100;

    if (!pwalletMain->HaveHDSeed()) {
        pwalletMain->GenerateNewSeed();
    }

    // wallet should currently be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    pwalletMain->GetSaplingPaymentAddresses(addrs);
    BOOST_CHECK(addrs.size() == 0);

    // create keys
    for (int i = 0; i < n; i++) {
        CallRPC("z_getnewaddress sapling");
    }

    // Verify we can list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Verify that the wallet encryption RPC is disabled
    BOOST_CHECK_THROW(CallRPC("encryptwallet passphrase"), runtime_error);

    // Encrypt the wallet (we can't call RPC encryptwallet as that shuts down node)
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";

    boost::filesystem::current_path(GetArg("-datadir", "/tmp/thisshouldnothappen"));
    BOOST_CHECK(pwalletMain->EncryptWallet(strWalletPass));

    // Verify we can still list the keys imported
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n);

    // Try to add a new key, but we can't as the wallet is locked
    BOOST_CHECK_THROW(CallRPC("z_getnewaddress sapling"), runtime_error);

    // We can't call RPC walletpassphrase as that invokes RPCRunLater which breaks tests.
    // So we manually unlock.
    BOOST_CHECK(pwalletMain->Unlock(strWalletPass));

    // Now add a key
    BOOST_CHECK_NO_THROW(CallRPC("z_getnewaddress sapling"));

    // Verify the key has been added
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listaddresses"));
    arr = retValue.get_array();
    BOOST_CHECK(arr.size() == n + 1);

    // We can't simulate over RPC the wallet closing and being reloaded
    // but there are tests for this in gtest.
}


BOOST_AUTO_TEST_CASE(rpc_z_listunspent_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    UniValue retValue;

    // too many args
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 2 3 4 5"), runtime_error);

    // minconf must be >= 0
    BOOST_CHECK_THROW(CallRPC("z_listunspent -1"), runtime_error);

    // maxconf must be > minconf
    BOOST_CHECK_THROW(CallRPC("z_listunspent 2 1"), runtime_error);

    // maxconf must not be out of range
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 9999999999"), runtime_error);

    // must be an array of addresses
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 false ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP"), runtime_error);

    // address must be string
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 false [123456]"), runtime_error);

    // no spending key
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 false [\"ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP\"]"), runtime_error);

    // allow watch only
    BOOST_CHECK_NO_THROW(CallRPC("z_listunspent 1 999 true [\"ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP\"]"));

    // wrong network, mainnet instead of testnet
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 true [\"zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U\"]"), runtime_error);

    // create shielded address so we have the spending key
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_getnewaddress sprout"));
    std::string myzaddr = retValue.get_str();

    // return empty array for this address
    BOOST_CHECK_NO_THROW(retValue = CallRPC("z_listunspent 1 999 false [\"" + myzaddr + "\"]"));
    UniValue arr = retValue.get_array();
    BOOST_CHECK_EQUAL(0, arr.size());

    // duplicate address error
    BOOST_CHECK_THROW(CallRPC("z_listunspent 1 999 false [\"" + myzaddr + "\", \"" + myzaddr + "\"]"), runtime_error);
}


BOOST_AUTO_TEST_CASE(rpc_z_shieldcoinbase_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);

    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase too many args shown here"), runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "INVALIDtmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"),
                      runtime_error);

    // bad from address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "** tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"),
                      runtime_error);

    // bad to address
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ INVALIDtnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB"),
                      runtime_error);

    // invalid fee amount, cannot be negative
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
                              "-0.0001"),
                      runtime_error);

    // invalid fee amount, bigger than MAX_MONEY
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
                              "21000001"),
                      runtime_error);

    // invalid limit, must be at least 0
    BOOST_CHECK_THROW(CallRPC("z_shieldcoinbase "
                              "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ "
                              "tnpoQJVnYBZZqkFadj2bJJLThNCxbADGB5gSGeYTAGGrT5tejsxY9Zc1BtY8nnHmZkB "
                              "100 -1"),
                      runtime_error);

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test constructor of AsyncRPCOperation_sendmany
    std::string testnetzaddr = "ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP";
    std::string mainnetzaddr = "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U";

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, {}, testnetzaddr, -1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Fee is out of range"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, {}, testnetzaddr, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Empty inputs"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<ShieldCoinbaseUTXO> inputs = {ShieldCoinbaseUTXO{uint256(), 0, 0}};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, inputs, mainnetzaddr, 1));
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Invalid to address"));
    }
}


BOOST_AUTO_TEST_CASE(rpc_z_shieldcoinbase_internals)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK(pwalletMain->cs_wallet);
    KeyIO keyIO(Params());
    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);
    if (mtx.nVersion == 1) {
        mtx.nVersion = 2;
    }

    // Test that option -mempooltxinputlimit is respected.
    mapArgs["-mempooltxinputlimit"] = "1";

    // Add keys manually
    auto pa = pwalletMain->GenerateNewSproutZKey();
    std::string zaddr = keyIO.EncodePaymentAddress(pa);

    // Supply 2 inputs when mempool limit is 1
    {
        std::vector<ShieldCoinbaseUTXO> inputs = {ShieldCoinbaseUTXO{uint256(), 0, 0}, ShieldCoinbaseUTXO{uint256(), 0, 0}};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, inputs, zaddr));
        operation->main();
        BOOST_CHECK(operation->isFailed());
        std::string msg = operation->getErrorMessage();
        BOOST_CHECK(msg.find("Number of inputs 2 is greater than mempooltxinputlimit of 1") != string::npos);
    }

    // Insufficient funds
    {
        std::vector<ShieldCoinbaseUTXO> inputs = {ShieldCoinbaseUTXO{uint256(), 0, 0}};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, inputs, zaddr));
        operation->main();
        BOOST_CHECK(operation->isFailed());
        std::string msg = operation->getErrorMessage();
        BOOST_CHECK(msg.find("Insufficient coinbase funds") != string::npos);
    }

    // Test the perform_joinsplit methods.
    {
        // Dummy input so the operation object can be instantiated.
        std::vector<ShieldCoinbaseUTXO> inputs = {ShieldCoinbaseUTXO{uint256(), 0, 100000}};
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_shieldcoinbase(TransactionBuilder(), mtx, inputs, zaddr));
        std::shared_ptr<AsyncRPCOperation_shieldcoinbase> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_shieldcoinbase>(operation);
        TEST_FRIEND_AsyncRPCOperation_shieldcoinbase proxy(ptr);
        static_cast<AsyncRPCOperation_shieldcoinbase*>(operation.get())->testmode = true;

        ShieldCoinbaseJSInfo info;
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        try {
            proxy.perform_joinsplit(info);
        } catch (const std::runtime_error& e) {
            BOOST_CHECK(string(e.what()).find("unsupported joinsplit input") != string::npos);
        }

        info.vjsin.clear();
        try {
            proxy.perform_joinsplit(info);
        } catch (const std::runtime_error& e) {
            BOOST_CHECK(string(e.what()).find("error verifying joinsplit") != string::npos);
        }
    }
}


void CheckRPCThrows(std::string rpcString, std::string expectedErrorMessage)
{
    try {
        CallRPC(rpcString);
        // Note: CallRPC catches (const UniValue& objError) and rethrows a runtime_error
        BOOST_FAIL("Should have caused an error");
    } catch (const std::runtime_error& e) {
        BOOST_CHECK_EQUAL(expectedErrorMessage, e.what());
    } catch (const std::exception& e) {
        BOOST_FAIL(std::string("Unexpected exception: ") + typeid(e).name() + ", message=\"" + e.what() + "\"");
    }
}

BOOST_AUTO_TEST_CASE(rpc_z_mergetoaddress_parameters)
{
    SelectParams(CBaseChainParams::TESTNET);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress toofewargs"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("z_mergetoaddress just too many args present for this method"), runtime_error);

    std::string taddr1 = "tmRr6yJonqGK23UVhrKuyvTpF8qxQQjKigJ";
    std::string taddr2 = "tmYmhvdKqEte49iohoB9utgL1kPbGgWSdNc";
    std::string aSproutAddr = "ztVtBC7vJFXPsZC8S3hXRu51rZysoJkSe6r1t9wk56bELrV9xTK6dx5TgSCH6RTw1dRD7HuApmcY1nhuQW9QfvE4MQXRRYU";
    std::string aSaplingAddr = "ztestsapling19rnyu293v44f0kvtmszhx35lpdug574twc0lwyf4s7w0umtkrdq5nfcauxrxcyfmh3m7slemqsj";

    CheckRPCThrows("z_mergetoaddress [] " + taddr1,
        "Invalid parameter, fromaddresses array is empty.");

    // bad from address
    CheckRPCThrows("z_mergetoaddress [\"INVALID" + taddr1 + "\"] " + taddr2,
        "Unknown address format: INVALID" + taddr1);

    // bad from address
    CheckRPCThrows("z_mergetoaddress ** " + taddr2,
        "Error parsing JSON:**");

    // bad from address
    CheckRPCThrows("z_mergetoaddress [\"**\"] " + taddr2,
        "Unknown address format: **");

    // bad from address
    CheckRPCThrows("z_mergetoaddress " + taddr1 + " " + taddr2,
        "Error parsing JSON:" + taddr1);

    // bad from address
    CheckRPCThrows("z_mergetoaddress [" + taddr1 + "] " + taddr2,
        "Error parsing JSON:[" + taddr1 + "]");

    // bad to address
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] INVALID" + taddr2,
        "Invalid parameter, unknown address format: INVALID" + taddr2);

    // duplicate address
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\",\"" + taddr1 + "\"] " + taddr2,
        "Invalid parameter, duplicated address: " + taddr1);

    // invalid fee amount, cannot be negative
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " -0.00001",
        "Amount out of range");

    // invalid fee amount, bigger than MAX_MONEY
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] "  + taddr2 + " 21000001",
        "Amount out of range");

    // invalid transparent limit, must be at least 0
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " 0.00001 -1",
        "Limit on maximum number of UTXOs cannot be negative");

    // invalid shielded limit, must be at least 0
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + taddr2 + " 0.00001 100 -1",
        "Limit on maximum number of notes cannot be negative");

    CheckRPCThrows("z_mergetoaddress [\"ANY_TADDR\",\"" + taddr1 + "\"] " + taddr2,
        "Cannot specify specific taddrs when using \"ANY_TADDR\"");

    CheckRPCThrows("z_mergetoaddress [\"ANY_SPROUT\",\"" + aSproutAddr + "\"] " + taddr2,
        "Cannot specify specific zaddrs when using \"ANY_SPROUT\" or \"ANY_SAPLING\"");

    CheckRPCThrows("z_mergetoaddress [\"ANY_SAPLING\",\"" + aSaplingAddr + "\"] " + taddr2,
        "Cannot specify specific zaddrs when using \"ANY_SPROUT\" or \"ANY_SAPLING\"");

    // memo bigger than allowed length of ZC_MEMO_SIZE
    std::vector<char> v (2 * (ZC_MEMO_SIZE+1));     // x2 for hexadecimal string format
    std::fill(v.begin(),v.end(), 'A');
    std::string badmemo(v.begin(), v.end());
    CheckRPCThrows("z_mergetoaddress [\"" + taddr1 + "\"] " + aSproutAddr + " 0.00001 100 100 " + badmemo,
        "Invalid parameter, size of memo is larger than maximum allowed 512");

    // Mutable tx containing contextual information we need to build tx
    UniValue retValue = CallRPC("getblockcount");
    int nHeight = retValue.get_int();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight + 1);

    // Test constructor of AsyncRPCOperation_mergetoaddress
    MergeToAddressRecipient testnetzaddr(
        "ztjiDe569DPNbyTE6TSdJTaSDhoXEHLGvYoUnBU1wfVNU52TEyT6berYtySkd21njAeEoh8fFJUT42kua9r8EnhBaEKqCpP",
        "testnet memo");
    MergeToAddressRecipient mainnetzaddr(
        "zcMuhvq8sEkHALuSU2i4NbNQxshSAYrpCExec45ZjtivYPbuiFPwk6WHy4SvsbeZ4siy1WheuRGjtaJmoD1J8bFqNXhsG6U",
        "mainnet memo");

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(std::nullopt,  mtx, {}, {}, {}, testnetzaddr, -1 ));
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Fee is out of range"));
    }

    try {
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, {}, {}, {}, testnetzaddr, 1));
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "No inputs"));
    }

    std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{ COutPoint{uint256(), 0}, 0, CScript()} };

    try {
        MergeToAddressRecipient badaddr("", "memo");
        std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, {}, {}, badaddr, 1));
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Recipient parameter missing"));
    }

    std::vector<MergeToAddressInputSproutNote> sproutNoteInputs = 
        {MergeToAddressInputSproutNote{JSOutPoint(), SproutNote(), 0, SproutSpendingKey()}};
    std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs = 
        {MergeToAddressInputSaplingNote{SaplingOutPoint(), SaplingNote({}, uint256(), 0, uint256(), Zip212Enabled::BeforeZip212), 0, SaplingExpandedSpendingKey()}};

    // Sprout and Sapling inputs -> throw
    try {
        auto operation = new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, sproutNoteInputs, saplingNoteInputs, testnetzaddr, 1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Cannot send from both Sprout and Sapling addresses using z_mergetoaddress"));
    }
    // Sprout inputs and TransactionBuilder -> throw
    try {
        auto operation = new AsyncRPCOperation_mergetoaddress(TransactionBuilder(), mtx, inputs, sproutNoteInputs, {}, testnetzaddr, 1);
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK(find_error(objError, "Sprout notes are not supported by the TransactionBuilder"));
    }

    // Testnet payment addresses begin with 'zt'.  This test detects an incorrect prefix.
    try {
        std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{ COutPoint{uint256(), 0}, 0, CScript()} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, {}, {}, mainnetzaddr, 1) );
        BOOST_FAIL("Should have caused an error");
    } catch (const UniValue& objError) {
        BOOST_CHECK( find_error(objError, "Invalid recipient address"));
    }
}



// TODO: test private methods
BOOST_AUTO_TEST_CASE(rpc_z_mergetoaddress_internals)
{
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& consensusParams = Params().GetConsensus();

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue retValue;

    // Mutable tx containing contextual information we need to build tx
    // We removed the ability to create pre-Sapling Sprout proofs, so we can
    // only create Sapling-onwards transactions.
    int nHeight = consensusParams.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(consensusParams, nHeight + 1);

    // Add keys manually
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getnewaddress"));
    MergeToAddressRecipient taddr1(retValue.get_str(), "");
    auto pa = pwalletMain->GenerateNewSproutZKey();
    KeyIO keyIO(Params());
    MergeToAddressRecipient zaddr1(keyIO.EncodePaymentAddress(pa), "DEADBEEF");

    // Insufficient funds
    {
        std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{COutPoint{uint256(),0},0, CScript()} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, {}, {}, zaddr1) );
        operation->main();
        BOOST_CHECK(operation->isFailed());
        std::string msg = operation->getErrorMessage();
        BOOST_CHECK( msg.find("Insufficient funds, have 0.00 and miners fee is 0.00001") != string::npos);
    }

    // get_memo_from_hex_string())
    {
        std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{COutPoint{uint256(),0},100000, CScript()} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, {}, {}, zaddr1) );
        std::shared_ptr<AsyncRPCOperation_mergetoaddress> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_mergetoaddress> (operation);
        TEST_FRIEND_AsyncRPCOperation_mergetoaddress proxy(ptr);

        std::string memo = "DEADBEEF";
        std::array<unsigned char, ZC_MEMO_SIZE> array = proxy.get_memo_from_hex_string(memo);
        BOOST_CHECK_EQUAL(array[0], 0xDE);
        BOOST_CHECK_EQUAL(array[1], 0xAD);
        BOOST_CHECK_EQUAL(array[2], 0xBE);
        BOOST_CHECK_EQUAL(array[3], 0xEF);
        for (int i=4; i<ZC_MEMO_SIZE; i++) {
            BOOST_CHECK_EQUAL(array[i], 0x00);  // zero padding
        }

        // memo is longer than allowed
        std::vector<char> v (2 * (ZC_MEMO_SIZE+1));
        std::fill(v.begin(),v.end(), 'A');
        std::string bigmemo(v.begin(), v.end());

        try {
            proxy.get_memo_from_hex_string(bigmemo);
            BOOST_FAIL("Should have caused an error");
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "too big"));
        }

        // invalid hexadecimal string
        std::fill(v.begin(),v.end(), '@'); // not a hex character
        std::string badmemo(v.begin(), v.end());

        try {
            proxy.get_memo_from_hex_string(badmemo);
            BOOST_FAIL("Should have caused an error");
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "hexadecimal format"));
        }

        // odd length hexadecimal string
        std::fill(v.begin(),v.end(), 'A');
        v.resize(v.size() - 1);
        assert(v.size() %2 == 1); // odd length
        std::string oddmemo(v.begin(), v.end());
        try {
            proxy.get_memo_from_hex_string(oddmemo);
            BOOST_FAIL("Should have caused an error");
        } catch (const UniValue& objError) {
            BOOST_CHECK( find_error(objError, "hexadecimal format"));
        }
    }

    // Test the perform_joinsplit methods.
    {
        // Dummy input so the operation object can be instantiated.
        std::vector<MergeToAddressInputUTXO> inputs = { MergeToAddressInputUTXO{COutPoint{uint256(),0},100000, CScript()} };
        std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_mergetoaddress(std::nullopt, mtx, inputs, {}, {}, zaddr1) );
        std::shared_ptr<AsyncRPCOperation_mergetoaddress> ptr = std::dynamic_pointer_cast<AsyncRPCOperation_mergetoaddress> (operation);
        TEST_FRIEND_AsyncRPCOperation_mergetoaddress proxy(ptr);

        // Enable test mode so tx is not sent and proofs are not generated
        static_cast<AsyncRPCOperation_mergetoaddress *>(operation.get())->testmode = true;

        MergeToAddressJSInfo info;
        std::vector<std::optional < SproutWitness>> witnesses;
        uint256 anchor;
        try {
            proxy.perform_joinsplit(info, witnesses, anchor);
            BOOST_FAIL("Should have caused an error");
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("anchor is null")!= string::npos);
        }

        try {
            std::vector<JSOutPoint> v;
            proxy.perform_joinsplit(info, v);
            BOOST_FAIL("Should have caused an error");
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("anchor is null")!= string::npos);
        }

        info.notes.push_back(SproutNote());
        try {
            proxy.perform_joinsplit(info);
            BOOST_FAIL("Should have caused an error");
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("number of notes")!= string::npos);
        }

        info.notes.clear();
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        info.vjsin.push_back(JSInput());
        try {
            proxy.perform_joinsplit(info);
            BOOST_FAIL("Should have caused an error");
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("unsupported joinsplit input")!= string::npos);
        }

        info.vjsin.clear();
        try {
            proxy.perform_joinsplit(info);
            BOOST_FAIL("Should have caused an error");
        } catch (const std::runtime_error & e) {
            BOOST_CHECK( string(e.what()).find("error verifying joinsplit")!= string::npos);
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()
