// ---------------------------------------------------------------------------
// Fault injection implementation.
//
// This file exists primarily to satisfy build systems that expect a .cpp
// for every header. The FaultInjector class is entirely inline/template-free
// in the header; no additional code is required here.
//
// When FAULT_INJECTION_ENABLED=0 (the default), the zero-cost stub in the
// header makes this compilation unit empty.
// ---------------------------------------------------------------------------

#include "fault_injector.h"