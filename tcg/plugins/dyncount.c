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

#include "tcg-plugin.h"
#include "disas/disas.h"

#ifdef CONFIG_CAPSTONE
#include <capstone.h>
/* Check compatibility with capstone 3.x. */
#if CS_API_MAJOR < 3
#error "dyncount plugin required capstone library >= 3.x. Please install from http://www.capstone-engine.org/."
#endif

#if defined(TARGET_I386)
#define CS_ARCH CS_ARCH_X86
#define CS_MODE CS_MODE_32
#define CS_GROUPS_NAME "x86"
#define CS_INS_COUNT X86_INS_ENDING
#define CS_GRP_COUNT X86_GRP_ENDING
#elif defined(TARGET_X86_64)
#define CS_ARCH CS_ARCH_X86
#define CS_MODE CS_MODE_64
#define CS_GROUPS_NAME "x86"
#define CS_INS_COUNT X86_INS_ENDING
#define CS_GRP_COUNT X86_GRP_ENDING
#elif defined(TARGET_AARCH64)
#define CS_ARCH CS_ARCH_ARM64
#define CS_MODE 0
#define CS_GROUPS_NAME "arm64"
#define CS_INS_COUNT ARM64_INS_ENDING
#define CS_GRP_COUNT ARM64_GRP_ENDING
#elif defined(TARGET_ARM)
#define CS_ARCH CS_ARCH_ARM
#define CS_MODE CS_MODE_ARM
#define CS_GROUPS_NAME "arm"
#define CS_INS_COUNT ARM_INS_ENDING
#define CS_GRP_COUNT ARM_GRP_ENDING
#else
#define CS_GROUPS_NAME ""
#endif

#endif /* CONFIG_CAPSTONE */

#if !defined(CONFIG_CAPSTONE) || !defined(CS_ARCH)
void tpi_init(TCGPluginInterface *tpi)
{
    TPI_INIT_VERSION_GENERIC(tpi);
#if !defined(CONFIG_CAPSTONE)
    fprintf(tpi->output,
            "# WARNING: dyncount plugin disabled.\n"
            "#          capstone library >= 3.x was not found when configuring QEMU.\n"
            "#          Install capstone from http://www.capstone-engine.org/\n"
            "#          and reconfigure/recompile QEMU.\n"
        );
#elif !defined(CS_ARCH)
    fprintf(tpi->output,
            "# WARNING: dyncount plugin disabled.\n"
            "           This plugin is not available for target " TARGET_NAME ".\n"
        );
#endif
}
#else

/* Global capstone handle. Not synchronized. */
static csh cs_handle;

/* Shared globals. Must be synchrionized. */
static uint64_t *group_count;
static uint64_t *op_count;
static uint64_t icount_total;
static uint64_t ld_bytes;
static uint64_t st_bytes;

static void cpus_stopped(const TCGPluginInterface *tpi)
{
    unsigned int i;

    fprintf(tpi->output, "\nmnemonics_count:\n");
    for (i = 0; i < CS_INS_COUNT; i++) {
        if (op_count[i] > 0) {
            if (i == 0) {
                fprintf(tpi->output,
                        "  _unknown_: %"PRIu64"\n",
                        op_count[i]);
            } else {
                fprintf(tpi->output,
                        "  %s: %"PRIu64"\n",
                        cs_insn_name(cs_handle, i),
                        op_count[i]);
            }
        }
    }
    fprintf(tpi->output, "\ngroups_count:\n");
    for (i = 0; i < CS_GRP_COUNT; i++) {
        if (group_count[i] > 0) {
            if (i == 0) {
                fprintf(tpi->output,
                        "  nogroup: %"PRIu64"\n",
                        group_count[i]);
            } else {
                fprintf(tpi->output,
                        "  cs.grp.%s.%s: %"PRIu64"\n",
                        i < 128 ? "gen": CS_GROUPS_NAME,
                        cs_group_name(cs_handle, i),
                        group_count[i]);
            }
        }
    }
    fprintf(tpi->output, "\nloaded_bytes: %"PRIu64"\n", ld_bytes);
    fprintf(tpi->output, "\nstored_bytes: %"PRIu64"\n", st_bytes);
    fprintf(tpi->output, "\ninstructions_total: %"PRIu64"\n", icount_total);
}

static void update_counters(uint64_t counter_ptr, uint64_t count,
                            uint64_t counter_ptr2)
{
    atomic_add((uint64_t *)counter_ptr, count);
    if (counter_ptr2 != 0) atomic_add((uint64_t *)counter_ptr2, count);
}

static void gen_update_counters(const TCGPluginInterface *tpi, uint64_t *counter_ptr, uint64_t count, uint64_t *counter_ptr2)
{
    TCGArg args[3];
    TCGv_i64 tcgv_counter_ptr;
    TCGv_i64 tcgv_count;
    TCGv_i64 tcgv_counter_ptr2;

    tcgv_counter_ptr = tcg_const_i64((uint64_t)(intptr_t)counter_ptr);
    tcgv_count = tcg_const_i64(1);
    tcgv_counter_ptr2 = tcg_const_i64((uint64_t)(intptr_t)counter_ptr2);

    args[0] = GET_TCGV_I64(tcgv_counter_ptr);
    args[1] = GET_TCGV_I64(tcgv_count);
    args[2] = GET_TCGV_I64(tcgv_counter_ptr2);

    tcg_gen_callN(tpi->tcg_ctx, update_counters, TCG_CALL_DUMMY_ARG, 3, args);

    tcg_temp_free_i64(tcgv_counter_ptr2);
    tcg_temp_free_i64(tcgv_counter_ptr);
    tcg_temp_free_i64(tcgv_count);
}

static void after_gen_opc(const TCGPluginInterface *tpi, const TPIOpCode *tpi_opcode)
{
    size_t count;
    cs_insn *insns;

    switch(tpi_opcode->operator) {
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_ld_i64:
        gen_update_counters(tpi, &ld_bytes,
                            1 << (get_memop(tpi_opcode->opargs[2]) & MO_SIZE),
                            NULL);
        return;

    case INDEX_op_qemu_st_i32:
    case INDEX_op_qemu_st_i64:
        gen_update_counters(tpi, &st_bytes,
                            1 << (get_memop(tpi_opcode->opargs[2]) & MO_SIZE),
                            NULL);
        return;
    case INDEX_op_insn_start:
        break;
    default:
        return;
    }

    count = cs_disasm(cs_handle, (void *)(intptr_t)tpi_guest_ptr(tpi, tpi_opcode->pc), 16,
                      tpi_opcode->pc, 1, &insns);
    if (count > 0) {
        cs_insn *insn = &insns[0];
        cs_detail *detail = insn->detail;
        if (insn->id == 0 || insn->id >= CS_INS_COUNT) {
            /* We may get an instruction id out of range, force it to 0 (unknown). */
            gen_update_counters(tpi, &op_count[0], 1, &icount_total);
        } else {
            gen_update_counters(tpi, &op_count[insn->id], 1, &icount_total);
        }
        if (detail->groups_count > 0) {
            int n;
            for (n = 0; n < detail->groups_count; n++) {
                int group = detail->groups[n];
                assert(group < CS_GRP_COUNT);
                gen_update_counters(tpi, &group_count[group], 1, NULL);
            }
        } else {
            /* If not in any group, add to group 0 (nogroup). */
            gen_update_counters(tpi, &group_count[0], 1, NULL);
        }
        cs_free(insn, count);
    } else {
        const char *symbol, *filename;
        uint64_t address;
        lookup_symbol3(tpi_opcode->pc, &symbol, &filename, &address);
        fprintf(tpi->output, "# WARNING: tcg/plugins/dyncount: unable to disassemble instruction at PC 0x%"PRIx64" (%s: %s + 0x%"PRIx64")\n", tpi_opcode->pc, filename, symbol, tpi_opcode->pc - address);
        gen_update_counters(tpi, &op_count[0], 1, &icount_total);
        gen_update_counters(tpi, &group_count[0], 1, NULL);
    }
}

void tpi_init(TCGPluginInterface *tpi)
{
    TPI_INIT_VERSION_GENERIC(tpi);
    TPI_DECL_FUNC_3(tpi, update_counters, void, i64, i64, i64);

    if (cs_open(CS_ARCH, CS_MODE, &cs_handle) != CS_ERR_OK)
        abort();

    cs_option(cs_handle, CS_OPT_DETAIL, CS_OPT_ON);
    
    tpi->cpus_stopped = cpus_stopped;
    tpi->after_gen_opc = after_gen_opc;

    group_count = g_malloc0(CS_GRP_COUNT * sizeof(uint64_t));
    op_count = g_malloc0(CS_INS_COUNT * sizeof(uint64_t));
}

#endif /* CONFIG_CAPSTONE */
