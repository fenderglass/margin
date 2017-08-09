/*
 * Copyright (C) 2017 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

#include "stRPHmm.h"

stGenomeFragment *stGenomeFragment_construct(stRPHmm *hmm, stList *path) {
    /*
     * Returns an genome fragment inferred from the hmm and given path through it.
     */
    stGenomeFragment *gF = st_calloc(1, sizeof(stGenomeFragment));

    // Set coordinates
    gF->referenceName = stString_copy(hmm->referenceName);
    gF->refStart = hmm->refStart;
    gF->length = hmm->refLength;

    // Allocate genotype arrays
    gF->genotypeString = st_calloc(gF->length, sizeof(uint64_t));
    gF->genotypeProbs = st_calloc(gF->length, sizeof(float));
    gF->referenceSequence = st_calloc(gF->length, sizeof(uint8_t));

    // Allocate haplotype arrays
    gF->haplotypeString1 = st_calloc(gF->length, sizeof(uint64_t));
    gF->haplotypeProbs1 = st_calloc(gF->length, sizeof(float));
    gF->haplotypeString2 = st_calloc(gF->length, sizeof(uint64_t));
    gF->haplotypeProbs2 = st_calloc(gF->length, sizeof(float));

    // For each cell in the hmm
    stRPColumn *column = hmm->firstColumn;
    for(int64_t i=0; i<stList_length(path)-1; i++) {
        stRPCell *cell = stList_get(path, i);
        assert(cell != NULL);

        // Calculate the predicted genotype/haplotypes for the given cell
        fillInPredictedGenome(gF, cell->partition, column, hmm->referencePriorProbs, (stRPHmmParameters *)hmm->parameters);

        column = column->nColumn->nColumn;
    }

    // Get predictions for the last column
    assert(column != NULL);
    assert(column->nColumn == NULL);
    fillInPredictedGenome(gF, ((stRPCell *)stList_peek(path))->partition, column, hmm->referencePriorProbs, (stRPHmmParameters *)hmm->parameters);

    return gF;
}

double getLogProbOfReadGivenHaplotype(uint64_t *haplotypeString, int64_t start, int64_t length, stProfileSeq *profileSeq, stRPHmmParameters *params) {
    /*
     * Returns the log probability of the read given the haplotype.
     */
    double totalProb = 0.0;

    for(int64_t i=0; i<profileSeq->length; i++) {
        // Get base in the haplotype sequence
        int64_t j = i + profileSeq->refStart - start;
        if(j >= 0 && j < length) {
            uint64_t hapBase = haplotypeString[j];
            assert(hapBase < ALPHABET_SIZE);

            // Expectation of a match
            for(int64_t k=0; k<ALPHABET_SIZE; k++) {
                totalProb += *getSubstitutionProbSlow(params->readErrorSubModelSlow, hapBase, k) *
                                            getProb(&(profileSeq->profileProbs[i * ALPHABET_SIZE]), k);
            }
        }
    }
    return totalProb;
}

stSet *findReadsThatWereMoreProbablyGeneratedByTheOtherHaplotype(uint64_t *haplotypeString1, uint64_t *haplotypeString2,
        int64_t start, int64_t length, stSet *profileSeqs, stRPHmmParameters *params) {
    /*
     * Returns the subset of profileSeqs that were more probably generated by the second haplotype string
     * than the first.
     */
    stSet *subset = stSet_construct();
    stSetIterator *it = stSet_getIterator(profileSeqs);
    stProfileSeq *pSeq;
    while((pSeq = stSet_getNext(it)) != NULL) {

        // Calculate probability that the read was generated from haplotype1 and haplotype2
        double i = getLogProbOfReadGivenHaplotype(haplotypeString1, start, length, pSeq, params);
        double j = getLogProbOfReadGivenHaplotype(haplotypeString2, start, length, pSeq, params);

        if(i < j) {
            // Read is more likely to have been generated by the second haplotype rather than the first
            stSet_insert(subset, pSeq);
        }
    }
    stSet_destructIterator(it);

    return subset;
}

static uint64_t flipReadsBetweenPartitions(uint64_t partition, stRPColumn *column, stSet *flippingReads) {
    stSetIterator *setIt = stSet_getIterator(flippingReads);

    for(uint64_t i=0; i<column->depth; i++) {
        stProfileSeq *pSeq = column->seqHeaders[i];

        if(stSet_search(flippingReads, pSeq) != NULL) {
            partition = partition ^ ((uint64_t)1 << i);
        }
    }

    return partition;
}

void stSet_removeAll(stSet *set, stSet *subset) {
    /*
     * Removes the given subset from the given set.
     */
    stSetIterator *it = stSet_getIterator(subset);

    void *o;
    while((o = stSet_getNext(it)) != NULL) {
        stSet_remove(set, o);
    }
    stSet_destructIterator(it);
}

void stGenomeFragment_refineGenomeFragment(stGenomeFragment *gF, stSet *reads1, stSet *reads2, stRPHmm *hmm, stList *path, int64_t maxIterations) {
    /*
     * Refines the genome fragment and reas partitions by greedily and iteratively moving reads between the two partitions
     * according to which haplotype they best match.
     */

    // Copy the path as a sequence of unsigned integers, one for each cell on the path
    int64_t pathLength = stList_length(path);
    uint64_t p[pathLength];
    for(int64_t i=0; i<pathLength; i++) {
        p[i] = ((stRPCell *)stList_get(path, i))->partition;
    }

    int64_t iteration = 0;
    while(iteration++ < maxIterations) {
        // Get the subset of reads in each partition that want to switch to the other partition
        stSet *reads1To2 = findReadsThatWereMoreProbablyGeneratedByTheOtherHaplotype(gF->haplotypeString1, gF->haplotypeString2,
                gF->refStart, gF->length, reads1, (stRPHmmParameters *)hmm->parameters);
        stSet *reads2To1 = findReadsThatWereMoreProbablyGeneratedByTheOtherHaplotype(gF->haplotypeString2, gF->haplotypeString1,
                gF->refStart, gF->length, reads2, (stRPHmmParameters *)hmm->parameters);

        // If there are no reads wanting to switch then break
        st_logDebug("At iteration %" PRIi64 " of partition found %" PRIi64 " reads from partition 1 switching to 2 and %" PRIi64
                    " reads from partition 2 switching to 1\n", iteration, stSet_size(reads1To2), stSet_size(reads2To1));
        if(stSet_size(reads1To2) + stSet_size(reads2To1) == 0) {
            break;
        }

        // Update the read partitions
        stSet_removeAll(reads1, reads1To2);
        stSet_removeAll(reads2, reads2To1);
        stSet_insertAll(reads1, reads2To1);
        stSet_insertAll(reads2, reads1To2);

        assert(stSet_size(reads1) + stSet_size(reads2) == stList_length(hmm->profileSeqs));

        // Update the path and update the genome fragment
        stRPColumn *column = hmm->firstColumn;
        for(int64_t i=0; i<pathLength; i++) {
            // Update the partition for the column by shifting the reads accordingly
            p[i] = flipReadsBetweenPartitions(p[i], column, reads1To2);
            p[i] = flipReadsBetweenPartitions(p[i], column, reads2To1);

            // Update the genome fragment
            fillInPredictedGenome(gF, p[i], column, hmm->referencePriorProbs, (stRPHmmParameters *)hmm->parameters);

            // Get the next column
            if(i+1<pathLength) {
                column = column->nColumn->nColumn;
            }
        }

        // Clean up
        stSet_destruct(reads1To2);
        stSet_destruct(reads2To1);
    }
}

void stGenomeFragment_destruct(stGenomeFragment *genomeFragment) {
    // Coordinates
    free(genomeFragment->referenceName);
    free(genomeFragment->referenceSequence);

    // Genotypes
    free(genomeFragment->genotypeString);
    free(genomeFragment->genotypeProbs);

    // Haplotypes
    free(genomeFragment->haplotypeString1);
    free(genomeFragment->haplotypeString2);
    free(genomeFragment->haplotypeProbs1);
    free(genomeFragment->haplotypeProbs2);

    free(genomeFragment);
}

