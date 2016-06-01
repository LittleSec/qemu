/*
 * TCG plugin for QEMU: count the number of executed instructions per
 *                      CPU.
 *
 * Copyright (C) 2011 STMicroelectronics
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>

#include "tcg-op.h"
#include "exec/def-helper.h"
#include "tcg-plugin.h"

#ifndef CONFIG_CAPSTONE
void tpi_init(TCGPluginInterface *tpi)
{
    TPI_INIT_VERSION(*tpi);
    fprintf(tpi->output,
            "# WARNING: dyncount plugin disabled.\n"
            "#          capstone was not found or forced no at qemu configure time.\n");
}
#else

#include <capstone.h>

/* Check compatibility with capstone 3.x. */
#if CS_API_MAJOR < 3
#error "dyncount plugin required capstone library >= 3.x. Please install from http://www.capstone-engine.org/."
#endif

/* Undef this for DEBUGGING plugin. */
/*#define DEBUG_PLUGIN 1*/

#define MAX_PRINT_SIZE 128

static csh cs_handle;
static FILE * output;
static cs_insn *insn;

static void write_str(uint64_t str_intptr)
{
    char *str = (char *)(intptr_t)str_intptr;

    fwrite(str, sizeof(char), strlen(str), output);
    fflush(output);
}

static void gen_printf_insn(const TCGPluginInterface *tpi, cs_insn *insn)
{
    int sizemask = 0;
    TCGArg args[1];

    TCGv_i64 tcgv_str;

    size_t size = MAX_PRINT_SIZE*sizeof(char);
    size_t left;
    char *str = g_malloc(size);
    char *ptr;
    size_t count;
    int i;

    ptr = str;
    left = size;
    count = snprintf(ptr, left,
                     "0x%"PRIx64":\t %s\t %s\t //",
                     insn->address,
                     insn->mnemonic,
                     insn->op_str);
    for (i = 0; i < insn->size && count < left; i++) {
        left -= count;
        ptr += count;
        count = snprintf(ptr, left, " %02x", insn->bytes[i]);
    }
    if (count < left) {
        left -= count;
        ptr += count;
        snprintf(ptr, left, "\n");
    }
    snprintf(str + size - 5, 5, "...\n");

    tcgv_str = tcg_const_i64((uint64_t)(intptr_t)str);

    args[0] = GET_TCGV_I64(tcgv_str);

    dh_sizemask(void, 0);
    dh_sizemask(i64, 1);

    tcg_gen_helperN(write_str, 0, sizemask, TCG_CALL_DUMMY_ARG, 1, args);

    tcg_temp_free_i64(tcgv_str);
}

static void decode_instr(const TCGPluginInterface *tpi, uint64_t pc)
{
    int decoded;
    const uint8_t *code = (const uint8_t *)(intptr_t)pc;
    size_t size = 4096;
    uint64_t address = pc;
    
    decoded = cs_disasm_iter(cs_handle,
                             &code, &size, &address, insn);
    if (decoded) {
#ifdef DEBUG_PLUGIN
        fprintf(output, "Decode: 0x%"PRIx64":\t%s\t\t%s\n",
                insn->address,
                insn->mnemonic,
                insn->op_str);
#endif
        gen_printf_insn(tpi, insn);
    } else {
        fprintf(output, "tcg/plugins/dyntrace: unable to disassemble instruction at PC 0x%"PRIx64"\n", pc);
    }
}

static void cpus_stopped(const TCGPluginInterface *tpi)
{
    fflush(output);
    cs_free(insn, 1);
    cs_close(&cs_handle);
}

void tpi_init(TCGPluginInterface *tpi)
{
    TPI_INIT_VERSION(*tpi);

#if defined(TARGET_X86_64)
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &cs_handle) != CS_ERR_OK)
        abort();
#elif defined(TARGET_I386)
    if (cs_open(CS_ARCH_X86, CS_MODE_32, &cs_handle) != CS_ERR_OK)
        abort();
#else
#error "dyncount plugin currently works only for: TARGET_x86_64/TARGET_i386"
#endif

    cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);

    output = tpi->output;
    insn = cs_malloc(cs_handle);

    tpi->decode_instr  = decode_instr;
    tpi->cpus_stopped  = cpus_stopped;
}

#endif /* CONFIG_CAPSTONE */
