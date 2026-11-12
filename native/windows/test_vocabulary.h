#pragma once
#include "vocabulary.h"

namespace keyrill::vocabulary {
BOOL SetTestHooks(HMODULE, const Hooks &);
BOOL ClearTestHooks(HMODULE);
} // namespace keyrill::vocabulary
