#include "os_functions.h"
#include "nn_nim_functions.h"

unsigned int nn_nim_handle = 0;

EXPORT_DECL(int, NeedsNetworkUpdate__Q2_2nn3nimFPb, bool * result);

void InitNimFunctionPointers(void)
{
    unsigned int *funcPointer = 0;
    OSDynLoad_Acquire("nn_nim.rpl", &nn_nim_handle);

    OS_FIND_EXPORT(nn_nim_handle, NeedsNetworkUpdate__Q2_2nn3nimFPb);
}