// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

static const int64_t nTargetTimespan = 45; // 45 seconds
static const int64_t nTargetSpacing = 45; // 45 seconds
static const int64_t nInterval = nTargetTimespan / nTargetSpacing; // Joulecoin: retarget every block

static const int64_t nHeightVer2 = 32000;
// static unsigned int nCheckpointTimeVer2 = 1380608826;

static const int64_t nHeightVer3 = 90000;
//static unsigned int nCheckpointTimeVer3 = 1384372762;

static const int64_t nAveragingInterval1 = nInterval * 160; // 160 blocks
static const int64_t nAveragingTargetTimespan1 = nAveragingInterval1 * nTargetSpacing; // 120 minutes

static const int64_t nAveragingInterval2 = nInterval * 8; // 8 blocks
static const int64_t nAveragingTargetTimespan2 = nAveragingInterval2 * nTargetSpacing; // 6 minutes

static const int64_t nAveragingInterval3 = nAveragingInterval2; // 8 blocks
static const int64_t nAveragingTargetTimespan3 = nAveragingTargetTimespan2; // 6 minutes

static const int64_t nMaxAdjustDown1 = 10; // 10% adjustment down
static const int64_t nMaxAdjustUp1 = 1; // 1% adjustment up

static const int64_t nMaxAdjustDown2 = 1; // 1% adjustment down
static const int64_t nMaxAdjustUp2 = 1; // 1% adjustment up

static const int64_t nMaxAdjustDown3 = 3; // 3% adjustment down
static const int64_t nMaxAdjustUp3 = 1; // 1% adjustment up

static const int64_t nTargetTimespanAdjDown1 = nTargetTimespan * (100 + nMaxAdjustDown1) / 100;
static const int64_t nTargetTimespanAdjDown2 = nTargetTimespan * (100 + nMaxAdjustDown2) / 100;
static const int64_t nTargetTimespanAdjDown3 = nTargetTimespan * (100 + nMaxAdjustDown3) / 100;

static const int64_t nMinActualTimespan1 = nAveragingTargetTimespan1 * (100 - nMaxAdjustUp1) / 100;
static const int64_t nMaxActualTimespan1 = nAveragingTargetTimespan1 * (100 + nMaxAdjustDown1) / 100;

static const int64_t nMinActualTimespan2 = nAveragingTargetTimespan2 * (100 - nMaxAdjustUp2) / 100;
static const int64_t nMaxActualTimespan2 = nAveragingTargetTimespan2 * (100 + nMaxAdjustDown2) / 100;

static const int64_t nMinActualTimespan3 = nAveragingTargetTimespan3 * (100 - nMaxAdjustUp3) / 100;
static const int64_t nMaxActualTimespan3 = nAveragingTargetTimespan3 * (100 + nMaxAdjustDown3) / 100;

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Genesis block
    if (pindexLast == NULL)
        return nProofOfWorkLimit;
    if (pindexLast->nHeight+1 < nAveragingInterval1)
        return nProofOfWorkLimit;

    if (params.fPowAllowMinDifficultyBlocks)
    {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than 2* 10 minutes
        // then allow mining of a min-difficulty block.
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
            return nProofOfWorkLimit;
        else
        {
            // Return the last non-special-min-difficulty-rules-block
            const CBlockIndex* pindex = pindexLast;
            while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                pindex = pindex->pprev;
            return pindex->nBits;
        }
    }

    int64_t nAveragingInterval;

    if (pindexLast->nHeight+1 >= nHeightVer3)
    {
        nAveragingInterval = nAveragingInterval3;
    }
    else
    {
        if (pindexLast->nHeight+1 >= nHeightVer2)
        {
            nAveragingInterval = nAveragingInterval2;
        }
        else
        {
            nAveragingInterval = nAveragingInterval1;
        }
    }

    // Go back by what we want to be nAveragingInterval worth of blocks
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; pindexFirst && i < nAveragingInterval-1; i++)
        pindexFirst = pindexFirst->pprev;
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    int64_t nMinActualTimespan;
    int64_t nMaxActualTimespan;
    int64_t nAveragingTargetTimespan;

    if (pindexLast->nHeight+1 >= nHeightVer3)
    {
        nMinActualTimespan = nMinActualTimespan3;
        nMaxActualTimespan = nMaxActualTimespan3;
        nAveragingTargetTimespan = nAveragingTargetTimespan3;
    }
    else
    {
        if (pindexLast->nHeight+1 >= nHeightVer2)
        {
            nMinActualTimespan = nMinActualTimespan2;
            nMaxActualTimespan = nMaxActualTimespan2;
            nAveragingTargetTimespan = nAveragingTargetTimespan2;
        }
        else
        {
            nMinActualTimespan = nMinActualTimespan1;
            nMaxActualTimespan = nMaxActualTimespan1;
            nAveragingTargetTimespan = nAveragingTargetTimespan1;
        }
    }
    
    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    LogPrintf("  nActualTimespan = %d  before bounds\n", nActualTimespan);
    if (nActualTimespan < nMinActualTimespan)
        nActualTimespan = nMinActualTimespan;
    if (nActualTimespan > nMaxActualTimespan)
        nActualTimespan = nMaxActualTimespan;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    bnNew *= nActualTimespan;
    bnNew /= nAveragingTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    /// debug print
    LogPrintf("GetNextWorkRequired RETARGET\n");
    LogPrintf("params.nPowTargetTimespan = %d    nActualTimespan = %d\n", params.nPowTargetTimespan, nActualTimespan);
    LogPrintf("Before: %08x  %s\n", pindexLast->nBits, bnOld.ToString());
    LogPrintf("After:  %08x  %s\n", bnNew.GetCompact(), bnNew.ToString());

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return error("CheckProofOfWork(): nBits below minimum work");

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return error("CheckProofOfWork(): hash doesn't match nBits");

    return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block)
{
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || bnTarget == 0)
        return 0;
    // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
    // as it's too large for a arith_uint256. However, as 2**256 is at least as large
    // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
    // or ~bnTarget / (nTarget+1) + 1.
    return (~bnTarget / (bnTarget + 1)) + 1;
}

int64_t GetBlockProofEquivalentTime(const CBlockIndex& to, const CBlockIndex& from, const CBlockIndex& tip, const Consensus::Params& params)
{
    arith_uint256 r;
    int sign = 1;
    if (to.nChainWork > from.nChainWork) {
        r = to.nChainWork - from.nChainWork;
    } else {
        r = from.nChainWork - to.nChainWork;
        sign = -1;
    }
    r = r * arith_uint256(params.nPowTargetSpacing) / GetBlockProof(tip);
    if (r.bits() > 63) {
        return sign * std::numeric_limits<int64_t>::max();
    }
    return sign * r.GetLow64();
}
