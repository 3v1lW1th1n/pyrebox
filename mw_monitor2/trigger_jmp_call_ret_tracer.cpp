/*-------------------------------------------------------------------------------

   Copyright (C) 2018 Cisco Talos Security Intelligence and Research Group

   PyREBox: Python scriptable Reverse Engineering Sandbox 
   Author: Xabier Ugarte-Pedrero 
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.
   
-------------------------------------------------------------------------------*/

#include <stdio.h>
#include <map>
#include <unordered_set>
#include <set>
#include <list>
#include <map>
#include <string>
#include <Python.h>
extern "C"{
    #include "qemu_glue.h"
}
#include "callbacks.h"
#include "trigger_helpers.h"

using namespace std;

extern "C"{
    int function_declared = 0;
    pyrebox_target_ulong target_pgd = 0;
    pyrebox_target_ulong enable_ff_jmp = 0;

    // Define trigger type. This type is checked when trigger is loaded
    callback_type_t get_type(){
        return OPCODE_RANGE_CB;
    }

    void set_opcode_var(callback_handle_t handle, uint8_t opcode){
        PyObject* new_var = Py_BuildValue("i", opcode);
        PyObject* old_var = (PyObject*)get_var(handle,"opcode");
        if (old_var != 0)
        {
            //Delete the variable without freeing its memory.
            delete_var(handle,"opcode",0);
        }
        //Now, we can set the var
        set_var(handle,"opcode",new_var);
    }

    // Trigger, return 1 if event should be passed to python callback 
    int trigger(callback_handle_t handle, callback_params_t params){
        if (function_declared == 0){
            target_pgd = *((pyrebox_target_ulong*) get_var(handle,"pgd"));
            enable_ff_jmp = *((pyrebox_target_ulong*) get_var(handle, "enable_ff_jmp"));
            function_declared = 1;
        }

#if TARGET_LONG_SIZE == 4
        pyrebox_target_ulong system_space_limit = 0x80000000;
#elif TARGET_LONG_SIZE == 8
        pyrebox_target_ulong system_space_limit = 0xFFFF080000000000;
#else
#error TARGET_LONG_SIZE undefined
#endif
        uint8_t buffer[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

        uint8_t opcodes[] = {0xff, 0xe8, 0x9a, 0xc3, 0xcb, 0xc2, 0xca, 0xe9, 0xea, 0xeb};

        pyrebox_target_ulong cur_pgd = get_pgd(params.opcode_range_params.cpu);

        pyrebox_target_ulong insn_size = params.opcode_range_params.insn_size;
        if (insn_size > 15){
            insn_size = 15;
        }
        
        // Only user-space -> user-space transitions
        if (cur_pgd == target_pgd && params.opcode_range_params.cur_pc < system_space_limit && params.opcode_range_params.next_pc < system_space_limit){
            // Double check the instruction that produced the callback
            // Read insn_size bytes from cur_pc
            if (qemu_virtual_memory_rw_with_pgd(cur_pgd, params.opcode_range_params.cur_pc, (uint8_t*) buffer, insn_size, 0) == 0){
                // Skip the prefixes and find the opcode
                int found = -1;
                for (unsigned int i = 0; (i < insn_size) && found == -1; ++i){
                    for (unsigned int j = 0; j < sizeof(opcodes) && found == -1; ++j){
                        if (buffer[i] == opcodes[j]){
                            found = i;
                        }
                    }
                }
                if(found != -1){
                    if (buffer[found] == 0xff){
                        if (found <= (int)(insn_size - 2)){
                            // reg is the opcode extension of the mod/rm byte
                            // mod/rm byte:  00   111  000  == 0x38
                            //               mod  reg  r/m
                            // the reg parameter is used as opcode extension
                            // for the ff instruction:
                            //
                            // 010 and 011 for CALL instructions
                            // 100 and 101 for JMP instructions
                            uint8_t reg = buffer[found+1] & 0x38;
                            if (reg == 0x10 || reg == 0x18){
                                //buffer[found] 
                                // CALL
                                set_opcode_var(handle, buffer[found]);
                                return 1;
                            } else if ((reg == 0x20 || reg == 0x28) && enable_ff_jmp > 0) {
                                //buffer[found]
                                // JMP
                                set_opcode_var(handle, buffer[found]);
                                return 1;
                            } else {
                                return 0;
                            }
                        } else {
                            // if (found <= (insn_size - 2){
                            return 0;
                        }
                    }
                    else{
                        // if (buffer[found] == 0xff){
                        // In order to understand the reference count, etc, read trigger_getset_var_example.cpp
                        if (buffer[found] == 0xe8 || buffer[found] == 0x9a){
                            // CALL
                            set_opcode_var(handle, buffer[found]);
                            return 1;
                        } else if (buffer[found] == 0xc3 || buffer[found] == 0xcb || buffer[found] == 0xca || buffer[found] == 0xc2){
                            // RET
                            set_opcode_var(handle, buffer[found]);
                            return 1;
                        } else if (buffer[found] == 0xe9 || buffer[found] == 0xea || buffer[found] == 0xeb){
                            // JMP
                            set_opcode_var(handle, buffer[found]);
                            return 1;
                        } else {
                            return 0;
                        }
                    }
                } else {
                    // if(found != -1){
                    return 0;
                }
            } else {
                //if (qemu_virtual_memory_rw_with_pgd(cur_pgd, params.opcode_range_params.cur_pc, (uint8_t*) buffer, insn_size, 0) == 0){)
                return 0;
            }
        } else {
            // if (cur_pgd == *target_pgd && params.opcode_range_params.cur_pc < system_space_limit && params.opcode_range_params.next_pc < system_space_limit){
            return 0;
        }
    }
    void clean(callback_handle_t handle)
    {
        PyObject* old_var = (PyObject*)get_var(handle,"opcode");
        if (old_var != 0)
        {
            //Delete the variable without freeing its memory.
            delete_var(handle,"opcode",0);
        }

        //This call will iterate all the variables created, and for those pointing
        //to some memory, it will free the memory. It will erase completely the list
        //of variables.
        erase_trigger_vars(handle); 
    }
}
