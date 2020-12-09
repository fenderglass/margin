/*
 * Copyright (C) 2017 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

typedef struct _phasedVariant PhasedVariant;
struct _phasedVariant {
    char *refSeqName;
    int64_t refPos;
    double quality;
    stList *alleles; //refAllele is alleles[0]
    int64_t gt1;
    int64_t gt2;
    char *phaseSet;
};

typedef struct _partialPhaseSums PartialPhaseSums;
struct _partialPhaseSums {
    char *queryPhaseSet;
    char *truthPhaseSet;
    double unphasedSum;
    double phaseSum1;
    double phaseSum2;
};

PhasedVariant *phasedVariant_construct(const char *refSeqName, int64_t refPos, double quality, stList *alleles, int64_t gt1, int64_t gt2, char * phaseSet);

void phasedVariant_destruct(PhasedVariant *pv);

int phasedVariant_positionCmp(const void *a, const void *b);

stHash *getPhasedVariants(const char *vcfFile);

stList *getSharedContigs(stHash *entry1, stHash *entry2);


PartialPhaseSums *partialPhaseSums_construct(const char *queryPhaseSet, const char *truthPhaseSet);

void partialPhaseSums_destruct(PartialPhaseSums *pps);

stHash *phaseSetIntervals(stList *phasedVariants);

double phasingCorrectness(stList *queryPhasedVariants, stList *truthPhasedVariants, double decay);

