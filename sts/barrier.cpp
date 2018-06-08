#include "barrier.h"

std::map<std::string, MOBarrier *> MOBarrier::barrierInstances_ = {};
std::map<std::string, OMBarrier *> OMBarrier::barrierInstances_ = {};
std::map<std::string, MMBarrier *> MMBarrier::barrierInstances_ = {};
