#ifndef SIMD_OPTIMIZER_H
#define SIMD_OPTIMIZER_H

#include "architecture.h"
#include "tac.h"

#include <vector>

using namespace std;

vector<Instruction> optimize_simd(const vector<Instruction>& code,
                                  const Architecture& arch);

#endif
