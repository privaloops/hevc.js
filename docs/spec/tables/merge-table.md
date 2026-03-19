# Merge Candidate Combination Table

Table complete des paires pour les combined bi-predictive merge candidates (spec ITU-T H.265, Table 8-8).

## Table 8-8 — combIdx to l0CandIdx and l1CandIdx

Utilisee dans §8.5.3.2.4 pour generer les candidats bi-predictifs combines a partir des candidats uni-directionnels existants.

```cpp
// Nombre max de combinaisons a tester
// numOrigMergeCand * (numOrigMergeCand - 1) paires possibles
// mais on s'arrete des que numMergeCand == MaxNumMergeCand

struct MergeCombination {
    int l0CandIdx;
    int l1CandIdx;
};

// Table ordonnee par combIdx (pour MaxNumMergeCand = 5)
const MergeCombination mergeCombTable[] = {
    { 0, 1 },  // combIdx 0
    { 1, 0 },  // combIdx 1
    { 0, 2 },  // combIdx 2
    { 2, 0 },  // combIdx 3
    { 1, 2 },  // combIdx 4
    { 2, 1 },  // combIdx 5
    { 0, 3 },  // combIdx 6
    { 3, 0 },  // combIdx 7
    { 1, 3 },  // combIdx 8
    { 3, 1 },  // combIdx 9
    { 2, 3 },  // combIdx 10
    { 3, 2 },  // combIdx 11
};
```

## Algorithme de generation

```cpp
void generateCombinedBiPredCandidates(
    MergeCandidate* mergeCandList,
    int& numMergeCand,
    int MaxNumMergeCand,
    int numOrigMergeCand
) {
    // Seulement pour les B-slices
    int combIdx = 0;
    int numCombinations = numOrigMergeCand * (numOrigMergeCand - 1);

    for (combIdx = 0;
         combIdx < numCombinations && numMergeCand < MaxNumMergeCand;
         combIdx++)
    {
        int l0CandIdx = mergeCombTable[combIdx].l0CandIdx;
        int l1CandIdx = mergeCombTable[combIdx].l1CandIdx;

        if (l0CandIdx >= numOrigMergeCand || l1CandIdx >= numOrigMergeCand)
            continue;

        MergeCandidate& l0Cand = mergeCandList[l0CandIdx];
        MergeCandidate& l1Cand = mergeCandList[l1CandIdx];

        // Condition : les ref pics doivent etre differentes
        // pour que le candidat combine apporte une information nouvelle
        if (l0Cand.predFlagL0 == 1 && l1Cand.predFlagL1 == 1) {
            if (RefPicList0[l0Cand.refIdxL0] != RefPicList1[l1Cand.refIdxL1] ||
                l0Cand.mvL0 != l1Cand.mvL1)
            {
                MergeCandidate combined;
                combined.predFlagL0 = 1;
                combined.predFlagL1 = 1;
                combined.mvL0 = l0Cand.mvL0;
                combined.refIdxL0 = l0Cand.refIdxL0;
                combined.mvL1 = l1Cand.mvL1;
                combined.refIdxL1 = l1Cand.refIdxL1;

                mergeCandList[numMergeCand++] = combined;
            }
        }
    }
}
```

## Zero MV Candidates (padding)

Apres les combined bi-pred candidates, si `numMergeCand < MaxNumMergeCand`, on ajoute des candidats zero MV :

```cpp
void addZeroMVCandidates(
    MergeCandidate* mergeCandList,
    int& numMergeCand,
    int MaxNumMergeCand,
    int numRefFrames
) {
    int zeroIdx = 0;
    while (numMergeCand < MaxNumMergeCand) {
        MergeCandidate zero;
        zero.predFlagL0 = 1;
        zero.predFlagL1 = (slice_type == B) ? 1 : 0;
        zero.mvL0 = { 0, 0 };
        zero.mvL1 = { 0, 0 };
        zero.refIdxL0 = min(zeroIdx, numRefFramesL0 - 1);
        zero.refIdxL1 = (slice_type == B) ? min(zeroIdx, numRefFramesL1 - 1) : -1;

        mergeCandList[numMergeCand++] = zero;
        zeroIdx++;
    }
}
```
