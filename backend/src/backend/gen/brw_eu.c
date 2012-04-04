/* 
 * Copyright © 2012 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Benjamin Segovia <benjamin.segovia@intel.com>
 */

 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */
  

// #include "brw_context.h"
#include "brw_defines.h"
#include "brw_eu.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

// #include "glsl/ralloc.h"

/* Returns the corresponding conditional mod for swapping src0 and
 * src1 in e.g. CMP.
 */
uint32_t
brw_swap_cmod(uint32_t cmod)
{
   switch (cmod) {
   case BRW_CONDITIONAL_Z:
   case BRW_CONDITIONAL_NZ:
      return cmod;
   case BRW_CONDITIONAL_G:
      return BRW_CONDITIONAL_LE;
   case BRW_CONDITIONAL_GE:
      return BRW_CONDITIONAL_L;
   case BRW_CONDITIONAL_L:
      return BRW_CONDITIONAL_GE;
   case BRW_CONDITIONAL_LE:
      return BRW_CONDITIONAL_G;
   default:
      return ~0;
   }
}


/* How does predicate control work when execution_size != 8?  Do I
 * need to test/set for 0xffff when execution_size is 16?
 */
void brw_set_predicate_control_flag_value(struct brw_compile *p, uint32_t value)
{
//   p->current->header.predicate_control = BRW_PREDICATE_NONE;

   if (value != 0xff) {
      if (value != p->flag_value) {
         brw_push_insn_state(p);
         brw_MOV(p, brw_flag_reg(), brw_imm_uw(value));
         p->flag_value = value;
         brw_pop_insn_state(p);
      }

//      p->current->header.predicate_control = BRW_PREDICATE_NORMAL;
   }   
}

void brw_set_predicate_control(struct brw_compile *p, uint32_t pc)
{
  // p->current->header.predicate_control = pc;
}

void brw_set_predicate_inverse(struct brw_compile *p, bool predicate_inverse)
{
  // p->current->header.predicate_inverse = predicate_inverse;
}

void brw_set_conditionalmod(struct brw_compile *p, uint32_t conditional)
{
 //  p->current->header.destreg__conditionalmod = conditional;
}

void brw_set_access_mode(struct brw_compile *p, uint32_t access_mode)
{
  // p->current->header.access_mode = access_mode;
}

void
brw_set_compression_control(struct brw_compile *p,
                            enum brw_compression compression_control)
{
#if 0
   p->compressed = (compression_control == BRW_COMPRESSION_COMPRESSED);

   if (p->gen >= 6) {
      /* Since we don't use the 32-wide support in gen6, we translate
       * the pre-gen6 compression control here.
       */
      switch (compression_control) {
      case BRW_COMPRESSION_NONE:
         /* This is the "use the first set of bits of dmask/vmask/arf
          * according to execsize" option.
          */
         p->current->header.compression_control = GEN6_COMPRESSION_1Q;
         break;
      case BRW_COMPRESSION_2NDHALF:
         /* For 8-wide, this is "use the second set of 8 bits." */
         p->current->header.compression_control = GEN6_COMPRESSION_2Q;
         break;
      case BRW_COMPRESSION_COMPRESSED:
         /* For 16-wide instruction compression, use the first set of 16 bits
          * since we don't do 32-wide dispatch.
          */
         p->current->header.compression_control = GEN6_COMPRESSION_1H;
         break;
      default:
         assert(!"not reached");
         p->current->header.compression_control = GEN6_COMPRESSION_1H;
         break;
      }
   } else {
      p->current->header.compression_control = compression_control;
   }
#endif
}

void brw_set_mask_control(struct brw_compile *p, uint32_t value)
{
//   p->current->header.mask_control = value;
}

void brw_set_saturate(struct brw_compile *p, uint32_t value)
{
//   p->current->header.saturate = value;
}

#if 0
void brw_set_acc_write_control(struct brw_compile *p, uint32_t value)
{
   if (p->brw->intel.gen >= 6)
      p->current->header.acc_wr_control = value;
}
#endif

void brw_push_insn_state(struct brw_compile *p)
{
#if 0
   assert(p->current != &p->stack[BRW_EU_MAX_INSN_STACK-1]);
   memcpy(p->current+1, p->current, sizeof(struct brw_instruction));
   p->compressed_stack[p->current - p->stack] = p->compressed;
   p->current++;   
#endif
}

void brw_pop_insn_state(struct brw_compile *p)
{
#if 0
   assert(p->current != p->stack);
   p->current--;
   p->compressed = p->compressed_stack[p->current - p->stack];
#endif
}


#if 0
/***********************************************************************
 */
void
brw_init_compile(struct brw_context *brw, struct brw_compile *p, void *mem_ctx)
{
   p->brw = brw;
   /*
    * Set the initial instruction store array size to 1024, if found that
    * isn't enough, then it will double the store size at brw_next_insn()
    * until out of memory.
    */
   p->store_size = 1024;
   p->store = rzalloc_array(mem_ctx, struct brw_instruction, p->store_size);
   p->nr_insn = 0;
   p->current = p->stack;
   p->compressed = false;
   memset(p->current, 0, sizeof(p->current[0]));

   p->mem_ctx = mem_ctx;

   /* Some defaults?
    */
   brw_set_mask_control(p, BRW_MASK_ENABLE); /* what does this do? */
   brw_set_saturate(p, 0);
   brw_set_compression_control(p, BRW_COMPRESSION_NONE);
   brw_set_predicate_control_flag_value(p, 0xff); 

   /* Set up control flow stack */
   p->if_stack_depth = 0;
   p->if_stack_array_size = 16;
   p->if_stack = rzalloc_array(mem_ctx, int, p->if_stack_array_size);

   p->loop_stack_depth = 0;
   p->loop_stack_array_size = 16;
   p->loop_stack = rzalloc_array(mem_ctx, int, p->loop_stack_array_size);
   p->if_depth_in_loop = rzalloc_array(mem_ctx, int, p->loop_stack_array_size);
}


const uint32_t *brw_get_program(struct brw_compile *p,
                               uint32_t *sz)
{
   uint32_t i;

   for (i = 0; i < 8; i++)
      brw_NOP(p);

   *sz = p->nr_insn * sizeof(struct brw_instruction);
   return (const uint32_t *)p->store;
}



/**
 * Subroutine calls require special attention.
 * Mesa instructions may be expanded into multiple hardware instructions
 * so the prog_instruction::BranchTarget field can't be used as an index
 * into the hardware instructions.
 *
 * The BranchTarget field isn't needed, however.  Mesa's GLSL compiler
 * emits CAL and BGNSUB instructions with labels that can be used to map
 * subroutine calls to actual subroutine code blocks.
 *
 * The structures and function here implement patching of CAL instructions
 * so they jump to the right subroutine code...
 */


/**
 * For each OPCODE_BGNSUB we create one of these.
 */
struct brw_glsl_label
{
   const char *name; /**< the label string */
   uint32_t position;  /**< the position of the brw instruction for this label */
   struct brw_glsl_label *next;  /**< next in linked list */
};


/**
 * For each OPCODE_CAL we create one of these.
 */
struct brw_glsl_call
{
   uint32_t call_inst_pos;  /**< location of the CAL instruction */
   const char *sub_name;  /**< name of subroutine to call */
   struct brw_glsl_call *next;  /**< next in linked list */
};


/**
 * Called for each OPCODE_BGNSUB.
 */
void
brw_save_label(struct brw_compile *c, const char *name, uint32_t position)
{
   struct brw_glsl_label *label = CALLOC_STRUCT(brw_glsl_label);
   label->name = name;
   label->position = position;
   label->next = c->first_label;
   c->first_label = label;
}


/**
 * Called for each OPCODE_CAL.
 */
void
brw_save_call(struct brw_compile *c, const char *name, uint32_t call_pos)
{
   struct brw_glsl_call *call = CALLOC_STRUCT(brw_glsl_call);
   call->call_inst_pos = call_pos;
   call->sub_name = name;
   call->next = c->first_call;
   c->first_call = call;
}


/**
 * Lookup a label, return label's position/offset.
 */
static uint32_t
brw_lookup_label(struct brw_compile *c, const char *name)
{
   const struct brw_glsl_label *label;
   for (label = c->first_label; label; label = label->next) {
      if (strcmp(name, label->name) == 0) {
         return label->position;
      }
   }
   abort();  /* should never happen */
   return ~0;
}


/**
 * When we're done generating code, this function is called to resolve
 * subroutine calls.
 */
void
brw_resolve_cals(struct brw_compile *c)
{
    const struct brw_glsl_call *call;

    for (call = c->first_call; call; call = call->next) {
        const uint32_t sub_loc = brw_lookup_label(c, call->sub_name);
        struct brw_instruction *brw_call_inst = &c->store[call->call_inst_pos];
        struct brw_instruction *brw_sub_inst = &c->store[sub_loc];
        GLint offset = brw_sub_inst - brw_call_inst;

        /* patch brw_inst1 to point to brw_inst2 */
        brw_set_src1(c, brw_call_inst, brw_imm_d(offset * 16));
    }

    /* free linked list of calls */
    {
        struct brw_glsl_call *call, *next;
        for (call = c->first_call; call; call = next) {
            next = call->next;
            free(call);
        }
        c->first_call = NULL;
    }

    /* free linked list of labels */
    {
        struct brw_glsl_label *label, *next;
        for (label = c->first_label; label; label = next) {
            next = label->next;
            free(label);
        }
        c->first_label = NULL;
    }
}
#endif