#ifndef __NN_NIM_FUNCTIONS_H_
#define __NN_NIM_FUNCTIONS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <gctypes.h>

/* Handle for coreinit */
extern unsigned int nn_nim_handle;

void InitNimFunctionPointers(void);

extern int (* NeedsNetworkUpdate__Q2_2nn3nimFPb)(bool * result);

#ifdef __cplusplus
}
#endif

#endif // __NN_NIM_FUNCTIONS_H_