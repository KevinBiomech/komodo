/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCinclude.h"

/*
 */

#define EVAL_REWARDS 0xe5

extern const char *RewardsCCaddr;
extern char RewardsCChexstr[67];

uint64_t RewardsCalc(uint64_t claim,uint256 txid)
{
    uint64_t reward = 0;
    return(reward);
}

CC *MakeRewardsCond(CPubKey pk)
{
    std::vector<CC*> pks; uint8_t evalcode = EVAL_REWARDS;
    pks.push_back(CCNewSecp256k1(pk));
    CC *rewardsCC = CCNewEval(E_MARSHAL(ss << evalcode));
    CC *Sig = CCNewThreshold(1, pks);
    return CCNewThreshold(2, {rewardsCC, Sig});
}

CTxOut MakeRewardsVout(CAmount nValue,CPubKey pk)
{
    CTxOut vout;
    CC *payoutCond = MakeRewardsCond(pk);
    vout = CTxOut(nValue,CCPubKey(payoutCond));
    cc_free(payoutCond);
    return(vout);
}

uint64_t IsRewardsvout(const CTransaction& tx,int32_t v)
{
    char destaddr[64];
    if ( tx.vout[v].scriptPubKey.IsPayToCryptoCondition() != 0 )
    {
        if ( Getscriptaddress(destaddr,tx.vout[v].scriptPubKey) > 0 && strcmp(destaddr,RewardsCCaddr) == 0 )
            return(tx.vout[v].nValue);
    }
    return(0);
}

bool RewardsExactAmounts(Eval* eval,const CTransaction &tx,int32_t minage,uint64_t txfee)
{
    static uint256 zerohash;
    CTransaction vinTx; uint256 hashBlock,activehash; int32_t i,numvins,numvouts; uint64_t inputs=0,outputs=0,assetoshis;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    for (i=0; i<numvins; i++)
    {
        if ( IsRewardsInput(tx.vin[i].scriptSig) != 0 )
        {
            if ( eval->GetTxUnconfirmed(tx.vin[i].prevout.hash,vinTx,hashBlock) == 0 )
                return eval->Invalid("always should find vin, but didnt");
            else
            {
                if ( hashBlock == zerohash )
                    return eval->Invalid("cant rewards from mempool");
                if ( (assetoshis= IsRewardsvout(vinTx,tx.vin[i].prevout.n)) != 0 )
                    inputs += assetoshis;
            }
        }
    }
    for (i=0; i<numvouts; i++)
    {
        //fprintf(stderr,"i.%d of numvouts.%d\n",i,numvouts);
        if ( (assetoshis= IsRewardsvout(tx,i)) != 0 )
            outputs += assetoshis;
    }
    if ( inputs != outputs+COIN+txfee )
    {
        fprintf(stderr,"inputs %llu vs outputs %llu\n",(long long)inputs,(long long)outputs);
        return eval->Invalid("mismatched inputs != outputs + COIN + txfee");
    }
    else return(true);
}

bool RewardsValidate(Eval* eval,const CTransaction &tx)
{
    int32_t numvins,numvouts,preventCCvins,preventCCvouts,i;
    numvins = tx.vin.size();
    numvouts = tx.vout.size();
    preventCCvins = preventCCvouts = -1;
    if ( numvouts < 1 )
        return eval->Invalid("no vouts");
    else
    {
        for (i=0; i<numvins; i++)
        {
            if ( IsCCInput(tx.vin[0].scriptSig) == 0 )
                return eval->Invalid("illegal normal vini");
        }
        if ( RewardsExactAmounts(eval,tx,1,10000) == false )
            return false;
        else
        {
            preventCCvouts = 1;
            if ( IsRewardsvout(tx,0) != 0 )
            {
                preventCCvouts++;
                i = 1;
            } else i = 0;
            if ( tx.vout[i].nValue != COIN )
                return eval->Invalid("invalid rewards output");
            return(PreventCC(eval,tx,preventCCvins,numvins,preventCCvouts,numvouts));
        }
    }
    return(true);
}

bool ProcessRewards(Eval* eval, std::vector<uint8_t> paramsNull,const CTransaction &ctx, unsigned int nIn)
{
    if ( paramsNull.size() != 0 ) // Don't expect params
        return eval->Invalid("Cannot have params");
    else if ( ctx.vout.size() == 0 )
        return eval->Invalid("no-vouts");
    if ( RewardsValidate(eval,ctx) != 0 )
        return(true);
    else return(false);
}

uint64_t AddRewardsInputs(CMutableTransaction &mtx,CPubKey pk,uint64_t total,int32_t maxinputs)
{
    char coinaddr[64]; uint64_t nValue,price,totalinputs = 0; uint256 txid,hashBlock; std::vector<uint8_t> origpubkey; CTransaction vintx; int32_t n = 0;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    GetCCaddress(EVAL_REWARDS,coinaddr,pk);
    SetCCunspents(unspentOutputs,coinaddr);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        if ( GetTransaction(txid,vintx,hashBlock,false) != 0 )
        {
            if ( (nValue= IsRewardsvout(vintx,(int32_t)it->first.index)) > 0 )
            {
                if ( total != 0 && maxinputs != 0 )
                    mtx.vin.push_back(CTxIn(txid,(int32_t)it->first.index,CScript()));
                nValue = it->second.satoshis;
                totalinputs += nValue;
                n++;
                if ( (total > 0 && totalinputs >= total) || (maxinputs > 0 && n >= maxinputs) )
                    break;
            }
        }
    }
    return(totalinputs);
}

// 0.834% every 60 days, min 100, capped at 0.834%

std::string RewardsFund(uint64_t txfee,uint64_t funds,uint64_t APR,uint64_t minseconds,uint64_t maxseconds,uint64_t mindeposit)
{
    CMutableTransaction mtx; CPubKey mypk,rewardspk; CScript opret;
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    rewardspk = GetUnspendable(EVAL_REWARDS,0);
    if ( AddNormalinputs(mtx,mypk,funds+2*txfee,64) > 0 )
    {
        mtx.vout.push_back(MakeRewardsVout(funds,rewardspk));
        mtx.vout.push_back(CTxOut(APR,CScript() << ParseHex(HexStr(rewardspk)) << OP_CHECKSIG));
        mtx.vout.push_back(CTxOut(minseconds,CScript() << ParseHex(HexStr(rewardspk)) << OP_CHECKSIG));
        mtx.vout.push_back(CTxOut(maxseconds,CScript() << ParseHex(HexStr(rewardspk)) << OP_CHECKSIG));
        mtx.vout.push_back(CTxOut(mindeposit,CScript() << ParseHex(HexStr(rewardspk)) << OP_CHECKSIG));
        return(FinalizeCCTx(EVAL_REWARDS,mtx,mypk,txfee,opret));
    }
    return(0);
}

std::string RewardsLock(uint64_t txfee,uint64_t amount)
{
    CMutableTransaction mtx; CPubKey mypk,rewardspk; CScript opret;
    if ( txfee == 0 )
        txfee = 10000;
    rewardspk = GetUnspendable(EVAL_REWARDS,0);
    mypk = pubkey2pk(Mypubkey());
    if ( AddNormalinputs(mtx,mypk,amount+2*txfee,64) > 0 )
    {
        mtx.vout.push_back(MakeRewardsVout(amount,rewardspk));
        // specify destination pubkey, funding txid
        return(FinalizeCCTx(EVAL_REWARDS,mtx,mypk,txfee,opret create script));
    } else fprintf(stderr,"cant find rewards inputs\n");
    return(0);
}

std::string RewardsUnlock(uint64_t txfee)
{
    CMutableTransaction mtx; CPubKey mypk,rewardspk; CScript opret; uint64_t reward,claim,inputs,CCchange=0;
    if ( txfee == 0 )
        txfee = 10000;
    rewardspk = GetUnspendable(EVAL_REWARDS,0);
    mypk = pubkey2pk(Mypubkey());
    if ( (claim= AddRewardsInputs(mtx,mypk,(1LL << 30),1)) > 0 && (reward= RewardsCalc(claim,mtx.vin[0].prevout.hash)) > txfee )
    {
        if ( (inputs= AddRewardsInputs(mtx,mypk,reward+txfee,30)) > 0 )
        {
            if ( inputs > (reward+txfee) )
                CCchange = (inputs - reward - txfee);
            if ( CCchange != 0 )
                mtx.vout.push_back(MakeRewardsVout(CCchange,rewardspk));
            mtx.vout.push_back(CTxOut(claim+reward,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
            return(FinalizeCCTx(EVAL_REWARDS,mtx,mypk,txfee,opret));
        }
    } else fprintf(stderr,"cant find rewards inputs\n");
    return(0);
}

