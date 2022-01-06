// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2017 The SnowGem developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORK_H
#define SPORK_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "messagesigner.h"
#include "net.h"
#include "sync.h"
#include "sporkid.h"
#include "util.h"

#include "protocol.h"


using namespace std;
using namespace boost;

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what

    Sporks 11,12, and 16 to be removed with 1st zerocoin release
*/

class CSporkMessage;
class CSporkManager;

extern std::vector<CSporkDef> sporkDefs;
extern std::map<uint256, CSporkMessage> mapSporks;
extern CSporkManager sporkManager;

void ReprocessBlocks(int nBlocks);

//
// Spork Class
// Keeps track of all of the network spork settings
//

class CSporkMessage: public CSignedMessage
{
public:
    int nSporkID;
    int64_t nValue;
    int64_t nTimeSigned;
    
    CSporkMessage() :
        CSignedMessage(),
        nSporkID(0),
        nValue(0),
        nTimeSigned(0)
    {}

    CSporkMessage(int nSporkID, int64_t nValue, int64_t nTimeSigned) :
        CSignedMessage(),
        nSporkID(nSporkID),
        nValue(nValue),
        nTimeSigned(nTimeSigned)
    { }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nSporkID;
        ss << nValue;
        ss << nTimeSigned;
        return ss.GetHash();
    }

    uint256 GetSignatureHash() const override;
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override { return CTxIn(); };

    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nSporkID);
        READWRITE(nValue);
        READWRITE(nTimeSigned);
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }
};


class CSporkManager
{
private:
    mutable CCriticalSection cs;
    std::string strMasterPrivKey;
    std::map<int, CSporkDef*> sporkDefsById;
    std::map<std::string, CSporkDef*> sporkDefsByName;
    std::map<int, CSporkMessage> mapSporksActive;

public:
    CSporkManager();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapSporksActive);
        // we don't serialize private key to prevent its leakage
    }
    
    void Clear();
    void LoadSporksFromDB();

    void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    int64_t GetSporkValue(int nSporkID);
    void ExecuteSpork(int nSporkID, int nValue);
    bool UpdateSpork(int nSporkID, int64_t nValue);

    bool IsSporkActive(int nSporkID);
    std::string GetSporkNameByID(int id);
    SporkId GetSporkIDByName(std::string strName);
    
    
    bool SetPrivKey(std::string strPrivKey);
    std::string ToString() const;
};

#endif
