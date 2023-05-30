#ifndef PARAM_H_
#define PARAM_H_

//=============================================================================

static const double UpperBoundLevel = 0.0005;
static const double UpperBoundFactor = 3.1;

static const int M1 = 5;
static const int M2 = 5;
static const unsigned M = M1 * M2;
static const unsigned EigenValueCount = 7;
static const double EigenValueDiffThreshold = 49.0;
static const double LevelStep = 0.05;
static const double MinLevel = 0.005;            // 0.06;
static const unsigned MaxClippedPixelCount = 2;  // = 0.1 * M
static const unsigned MaxSubsetCount = 32;       // >= 1 / LevelStep

//=============================================================================

#endif /*PARAM_H_*/
