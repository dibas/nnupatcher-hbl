#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "common/common.h"
#include "common/fs_defs.h"
#include "common/loader_defs.h"
#include "game/rpx_rpl_table.h"
#include "dynamic_libs/aoc_functions.h"
#include "dynamic_libs/ax_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/gx2_functions.h"
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/padscore_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/acp_functions.h"
#include "dynamic_libs/syshid_functions.h"
#include "dynamic_libs/nn_nim_functions.h"
#include "kernel/kernel_functions.h"
#include "system/exception_handler.h"
#include "function_hooks.h"
#include "fs/fs_utils.h"
#include "utils/logger.h"
#include "system/memory.h"

#define LIB_CODE_RW_BASE_OFFSET                         0xC1000000
#define CODE_RW_BASE_OFFSET                             0x00000000
#define DEBUG_LOG_DYN                                   0

#define USE_EXTRA_LOG_FUNCTIONS   0

#define DECL(res, name, ...) \
        res (* real_ ## name)(__VA_ARGS__) __attribute__((section(".data"))); \
        res my_ ## name(__VA_ARGS__)

DECL(int, NeedsNetworkUpdate__Q2_2nn3nimFPb, bool * result) 
{
    /*
    asm volatile (
        "li %r3, 0 ;"
        "li %r4, 0 ;"
        "blr";
    );
    */
    log_printf("Hello NeedsNetworkUpdate()! Nobody needs you!\n");
    //return real_NeedsNetworkUpdate__Q2_2nn3nimFPb(result);
    *result = 0;
    return 0;
}

/* *****************************************************************************
 * Creates function pointer array
 * ****************************************************************************/
#define MAKE_MAGIC(x, lib,functionType) { (unsigned int) my_ ## x, (unsigned int) &real_ ## x, lib, # x,0,0,functionType,0}

static struct hooks_magic_t {
    const unsigned int replaceAddr;
    const unsigned int replaceCall;
    const unsigned int library;
    const char functionName[50];
    unsigned int realAddr;
    unsigned int restoreInstruction;
    unsigned char functionType;
    unsigned char alreadyPatched;
} method_hooks[] = {
    // Nintendo eshop patcher
    MAKE_MAGIC(NeedsNetworkUpdate__Q2_2nn3nimFPb,   LIB_NIM,DYNAMIC_FUNCTION),
};

//! buffer to store our 7 instructions needed for our replacements
//! the code will be placed in the address of that buffer - CODE_RW_BASE_OFFSET
//! avoid this buffer to be placed in BSS and reset on start up
volatile unsigned int dynamic_method_calls[sizeof(method_hooks) / sizeof(struct hooks_magic_t) * 7] __attribute__((section(".data")));

/*
*Patches a function that is loaded at the start of each application. Its not required to restore, at least when they are really dynamic.
* "normal" functions should be patch with the normal patcher.
*/
void PatchMethodHooks(void)
{
    /* Patch branches to it.  */
    volatile unsigned int *space = &dynamic_method_calls[0];

    int method_hooks_count = sizeof(method_hooks) / sizeof(struct hooks_magic_t);

    u32 skip_instr = 1;
    u32 my_instr_len = 6;
    u32 instr_len = my_instr_len + skip_instr;
    u32 flush_len = 4*instr_len;
    for(int i = 0; i < method_hooks_count; i++)
    {
        if(method_hooks[i].functionType == STATIC_FUNCTION && method_hooks[i].alreadyPatched == 1){
            if(isDynamicFunction((u32)OSEffectiveToPhysical((void*)method_hooks[i].realAddr))){
                log_printf("The function %s is a dynamic function. Please fix that <3\n", method_hooks[i].functionName);
                method_hooks[i].functionType = DYNAMIC_FUNCTION;
            }else{
                log_printf("Skipping %s, its already patched\n", method_hooks[i].functionName);
                space += instr_len;
                continue;
            }
        }

        u32 physical = 0;
        unsigned int repl_addr = (unsigned int)method_hooks[i].replaceAddr;
        unsigned int call_addr = (unsigned int)method_hooks[i].replaceCall;

        unsigned int real_addr = GetAddressOfFunction(method_hooks[i].functionName,method_hooks[i].library);

        if(!real_addr){
            log_printf("OSDynLoad_FindExport failed for %s\n", method_hooks[i].functionName);
            space += instr_len;
            continue;
        }

        if(DEBUG_LOG_DYN)log_printf("%s is located at %08X!\n", method_hooks[i].functionName,real_addr);

        physical = (u32)OSEffectiveToPhysical((void*)real_addr);
        if(!physical){
             log_printf("Something is wrong with the physical address\n");
             space += instr_len;
             continue;
        }

        if(DEBUG_LOG_DYN)log_printf("%s physical is located at %08X!\n", method_hooks[i].functionName,physical);

        bat_table_t my_dbat_table;
        if(DEBUG_LOG_DYN)log_printf("Setting up DBAT\n");
        KernelSetDBATsForDynamicFuction(&my_dbat_table,physical);

        //log_printf("Setting call_addr to %08X\n",(unsigned int)(space) - CODE_RW_BASE_OFFSET);
        *(volatile unsigned int *)(call_addr) = (unsigned int)(space) - CODE_RW_BASE_OFFSET;

        // copy instructions from real function.
        u32 offset_ptr = 0;
        for(offset_ptr = 0;offset_ptr<skip_instr*4;offset_ptr +=4){
             if(DEBUG_LOG_DYN)log_printf("(real_)%08X = %08X\n",space,*(volatile unsigned int*)(physical+offset_ptr));
            *space = *(volatile unsigned int*)(physical+offset_ptr);
            space++;
        }

        //Only works if skip_instr == 1
        if(skip_instr == 1){
            // fill the restore instruction section
            method_hooks[i].realAddr = real_addr;
            method_hooks[i].restoreInstruction = *(volatile unsigned int*)(physical);
        }else{
            log_printf("Can't save %s for restoring!\n", method_hooks[i].functionName);
        }

        //adding jump to real function
        /*
            90 61 ff e0     stw     r3,-32(r1)
            3c 60 12 34     lis     r3,4660
            60 63 56 78     ori     r3,r3,22136
            7c 69 03 a6     mtctr   r3
            80 61 ff e0     lwz     r3,-32(r1)
            4e 80 04 20     bctr*/
        *space = 0x9061FFE0;
        space++;
        *space = 0x3C600000 | (((real_addr + (skip_instr * 4)) >> 16) & 0x0000FFFF); // lis r3, real_addr@h
        space++;
        *space = 0x60630000 |  ((real_addr + (skip_instr * 4)) & 0x0000ffff); // ori r3, r3, real_addr@l
        space++;
        *space = 0x7C6903A6; // mtctr   r3
        space++;
        *space = 0x8061FFE0; // lwz     r3,-32(r1)
        space++;
        *space = 0x4E800420; // bctr
        space++;
        DCFlushRange((void*)(space - instr_len), flush_len);
        ICInvalidateRange((unsigned char*)(space - instr_len), flush_len);

        //setting jump back
        unsigned int replace_instr = 0x48000002 | (repl_addr & 0x03fffffc);
        *(volatile unsigned int *)(physical) = replace_instr;
        ICInvalidateRange((void*)(real_addr), 4);

        //restore my dbat stuff
        KernelRestoreDBATs(&my_dbat_table);

        method_hooks[i].alreadyPatched = 1;
    }
    log_print("Done with patching all functions!\n");
}

/* ****************************************************************** */
/*                  RESTORE ORIGINAL INSTRUCTIONS                     */
/* ****************************************************************** */
void RestoreInstructions(void)
{
    bat_table_t table;
    log_printf("Restore functions!\n");
    int method_hooks_count = sizeof(method_hooks) / sizeof(struct hooks_magic_t);
    for(int i = 0; i < method_hooks_count; i++)
    {
        if(method_hooks[i].restoreInstruction == 0 || method_hooks[i].realAddr == 0){
            log_printf("I dont have the information for the restore =( skip\n");
            continue;
        }

        unsigned int real_addr = GetAddressOfFunction(method_hooks[i].functionName,method_hooks[i].library);

        if(!real_addr){
            log_printf("OSDynLoad_FindExport failed for %s\n", method_hooks[i].functionName);
            continue;
        }

        u32 physical = (u32)OSEffectiveToPhysical((void*)real_addr);
        if(!physical){
            log_printf("Something is wrong with the physical address\n");
            continue;
        }

        if(isDynamicFunction(physical)){
             log_printf("Its a dynamic function. We don't need to restore it! %s\n",method_hooks[i].functionName);
        }else{
            KernelSetDBATs(&table);

            *(volatile unsigned int *)(LIB_CODE_RW_BASE_OFFSET + method_hooks[i].realAddr) = method_hooks[i].restoreInstruction;
            DCFlushRange((void*)(LIB_CODE_RW_BASE_OFFSET + method_hooks[i].realAddr), 4);
            ICInvalidateRange((void*)method_hooks[i].realAddr, 4);
            log_printf("Restored %s\n",method_hooks[i].functionName);
            KernelRestoreDBATs(&table);
        }
        method_hooks[i].alreadyPatched = 0; // In case a
    }
    KernelRestoreInstructions();
    log_print("Done with restoring all functions!\n");
}

int isDynamicFunction(unsigned int physicalAddress){
    if((physicalAddress & 0x80000000) == 0x80000000){
        return 1;
    }
    return 0;
}

unsigned int GetAddressOfFunction(const char * functionName,unsigned int library){
    unsigned int real_addr = 0;

    unsigned int rpl_handle = 0;
    if(library == LIB_CORE_INIT){
        log_printf("FindExport of %s! From LIB_CORE_INIT\n", functionName);
        if(coreinit_handle == 0){log_print("LIB_CORE_INIT not aquired\n"); return 0;}
        rpl_handle = coreinit_handle;
    }
    else if(library == LIB_NSYSNET){
        log_printf("FindExport of %s! From LIB_NSYSNET\n", functionName);
        if(nsysnet_handle == 0){log_print("LIB_NSYSNET not aquired\n"); return 0;}
        rpl_handle = nsysnet_handle;
    }
    else if(library == LIB_GX2){
        log_printf("FindExport of %s! From LIB_GX2\n", functionName);
        if(gx2_handle == 0){log_print("LIB_GX2 not aquired\n"); return 0;}
        rpl_handle = gx2_handle;
    }
    else if(library == LIB_AOC){
        log_printf("FindExport of %s! From LIB_AOC\n", functionName);
        if(aoc_handle == 0){log_print("LIB_AOC not aquired\n"); return 0;}
        rpl_handle = aoc_handle;
    }
    else if(library == LIB_AX){
        log_printf("FindExport of %s! From LIB_AX\n", functionName);
        if(sound_handle == 0){log_print("LIB_AX not aquired\n"); return 0;}
        rpl_handle = sound_handle;
    }
    else if(library == LIB_FS){
        log_printf("FindExport of %s! From LIB_FS\n", functionName);
        if(coreinit_handle == 0){log_print("LIB_FS not aquired\n"); return 0;}
        rpl_handle = coreinit_handle;
    }
    else if(library == LIB_OS){
        log_printf("FindExport of %s! From LIB_OS\n", functionName);
        if(coreinit_handle == 0){log_print("LIB_OS not aquired\n"); return 0;}
        rpl_handle = coreinit_handle;
    }
    else if(library == LIB_PADSCORE){
        log_printf("FindExport of %s! From LIB_PADSCORE\n", functionName);
        if(padscore_handle == 0){log_print("LIB_PADSCORE not aquired\n"); return 0;}
        rpl_handle = padscore_handle;
    }
    else if(library == LIB_SOCKET){
        log_printf("FindExport of %s! From LIB_SOCKET\n", functionName);
        if(nsysnet_handle == 0){log_print("LIB_SOCKET not aquired\n"); return 0;}
        rpl_handle = nsysnet_handle;
    }
    else if(library == LIB_SYS){
        log_printf("FindExport of %s! From LIB_SYS\n", functionName);
        if(sysapp_handle == 0){log_print("LIB_SYS not aquired\n"); return 0;}
        rpl_handle = sysapp_handle;
    }
    else if(library == LIB_VPAD){
        log_printf("FindExport of %s! From LIB_VPAD\n", functionName);
        if(vpad_handle == 0){log_print("LIB_VPAD not aquired\n"); return 0;}
        rpl_handle = vpad_handle;
    }
    else if(library == LIB_NN_ACP){
        log_printf("FindExport of %s! From LIB_NN_ACP\n", functionName);
        if(acp_handle == 0){log_print("LIB_NN_ACP not aquired\n"); return 0;}
        rpl_handle = acp_handle;
    }
    else if(library == LIB_SYSHID){
        log_printf("FindExport of %s! From LIB_SYSHID\n", functionName);
        if(syshid_handle == 0){log_print("LIB_SYSHID not aquired\n"); return 0;}
        rpl_handle = syshid_handle;
    }
    else if(library == LIB_VPADBASE){
        log_printf("FindExport of %s! From LIB_VPADBASE\n", functionName);
        if(vpadbase_handle == 0){log_print("LIB_VPADBASE not aquired\n"); return 0;}
        rpl_handle = vpadbase_handle;
    }
    else if(library == LIB_NIM){
        log_printf("FindExport of %s! From LIB_NIM\n", functionName);
        if(nn_nim_handle == 0){log_print("LIB_NIM not aquired\n"); return 0;}
        rpl_handle = nn_nim_handle;
    }

    if(!rpl_handle){
        log_printf("Failed to find the RPL handle for %s\n", functionName);
        return 0;
    }

    OSDynLoad_FindExport(rpl_handle, 0, functionName, &real_addr);

    if(!real_addr){
        log_printf("OSDynLoad_FindExport failed for %s\n", functionName);
        return 0;
    }

    if((u32)(*(volatile unsigned int*)(real_addr) & 0xFF000000) == 0x48000000){
        real_addr += (u32)(*(volatile unsigned int*)(real_addr) & 0x0000FFFF);
        if((u32)(*(volatile unsigned int*)(real_addr) & 0xFF000000) == 0x48000000){
            return 0;
        }
    }

    return real_addr;
}
