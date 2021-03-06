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
 Copyright (C) Intel Corp.  2006.  All Rights Reserved.
 Intel funded Tungsten Graphics (http://www.tungstengraphics.com) to
 develop this 3D driver.

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice (including the
 next paragraph) shall be included in all copies or substantial
 portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 **********************************************************************/
 /*
  * Authors:
  *   Keith Whitwell <keith@tungstengraphics.com>
  */

#include "backend/gen_encoder.hpp"
#include <cstring>

namespace gbe
{
  //////////////////////////////////////////////////////////////////////////
  // Some helper functions to encode
  //////////////////////////////////////////////////////////////////////////
  INLINE bool isVectorOfBytes(GenRegister reg) {
    if (reg.hstride != GEN_HORIZONTAL_STRIDE_0 &&
        (reg.type == GEN_TYPE_UB || reg.type == GEN_TYPE_B))
      return true;
    else
      return false;
  }

  INLINE bool needToSplitAlu1(GenEncoder *p, GenRegister dst, GenRegister src) {
    if (p->curr.execWidth != 16) return false;
    if (isVectorOfBytes(dst) == true) return true;
    if (isVectorOfBytes(src) == true) return true;
    return false;
  }

  INLINE bool needToSplitAlu2(GenEncoder *p, GenRegister dst, GenRegister src0, GenRegister src1) {
    if (p->curr.execWidth != 16) return false;
    if (isVectorOfBytes(dst) == true) return true;
    if (isVectorOfBytes(src0) == true) return true;
    if (isVectorOfBytes(src1) == true) return true;
    return false;
  }

  INLINE bool needToSplitCmp(GenEncoder *p, GenRegister src0, GenRegister src1) {
    if (p->curr.execWidth != 16) return false;
    if (isVectorOfBytes(src0) == true) return true;
    if (isVectorOfBytes(src1) == true) return true;
    if (src0.type == GEN_TYPE_D || src0.type == GEN_TYPE_UD || src0.type == GEN_TYPE_F)
      return true;
    if (src1.type == GEN_TYPE_D || src1.type == GEN_TYPE_UD || src1.type == GEN_TYPE_F)
      return true;
    return false;
  }

  static void setMessageDescriptor(GenEncoder *p,
                                   GenInstruction *inst,
                                   enum GenMessageTarget sfid,
                                   unsigned msg_length,
                                   unsigned response_length,
                                   bool header_present = false,
                                   bool end_of_thread = false)
  {
     p->setSrc1(inst, GenRegister::immd(0));
     inst->bits3.generic_gen5.header_present = header_present;
     inst->bits3.generic_gen5.response_length = response_length;
     inst->bits3.generic_gen5.msg_length = msg_length;
     inst->bits3.generic_gen5.end_of_thread = end_of_thread;
     inst->header.destreg_or_condmod = sfid;
  }

  static void setDPUntypedRW(GenEncoder *p,
                             GenInstruction *insn,
                             uint32_t bti,
                             uint32_t rgba,
                             uint32_t msg_type,
                             uint32_t msg_length,
                             uint32_t response_length)
  {
    const GenMessageTarget sfid = GEN_SFID_DATAPORT_DATA_CACHE;
    setMessageDescriptor(p, insn, sfid, msg_length, response_length);
    insn->bits3.gen7_untyped_rw.msg_type = msg_type;
    insn->bits3.gen7_untyped_rw.bti = bti;
    insn->bits3.gen7_untyped_rw.rgba = rgba;
    if (p->curr.execWidth == 8)
      insn->bits3.gen7_untyped_rw.simd_mode = GEN_UNTYPED_SIMD8;
    else if (p->curr.execWidth == 16)
      insn->bits3.gen7_untyped_rw.simd_mode = GEN_UNTYPED_SIMD16;
    else
      NOT_SUPPORTED;
  }

  static void setDPByteScatterGather(GenEncoder *p,
                                     GenInstruction *insn,
                                     uint32_t bti,
                                     uint32_t elem_size,
                                     uint32_t msg_type,
                                     uint32_t msg_length,
                                     uint32_t response_length)
  {
    const GenMessageTarget sfid = GEN_SFID_DATAPORT_DATA_CACHE;
    setMessageDescriptor(p, insn, sfid, msg_length, response_length);
    insn->bits3.gen7_byte_rw.msg_type = msg_type;
    insn->bits3.gen7_byte_rw.bti = bti;
    insn->bits3.gen7_byte_rw.data_size = elem_size;
    if (p->curr.execWidth == 8)
      insn->bits3.gen7_byte_rw.simd_mode = GEN_BYTE_SCATTER_SIMD8;
    else if (p->curr.execWidth == 16)
      insn->bits3.gen7_byte_rw.simd_mode = GEN_BYTE_SCATTER_SIMD16;
    else
      NOT_SUPPORTED;
  }
#if 0
  static void setOBlockRW(GenEncoder *p,
                          GenInstruction *insn,
                          uint32_t bti,
                          uint32_t size,
                          uint32_t msg_type,
                          uint32_t msg_length,
                          uint32_t response_length)
  {
    const GenMessageTarget sfid = GEN_SFID_DATAPORT_DATA_CACHE;
    setMessageDescriptor(p, insn, sfid, msg_length, response_length);
    assert(size == 2 || size == 4);
    insn->bits3.gen7_oblock_rw.msg_type = msg_type;
    insn->bits3.gen7_oblock_rw.bti = bti;
    insn->bits3.gen7_oblock_rw.block_size = size == 2 ? 2 : 3;
    insn->bits3.gen7_oblock_rw.header_present = 1;
  }
#endif

  static void setSamplerMessage(GenEncoder *p,
                                GenInstruction *insn,
                                unsigned char bti,
                                unsigned char sampler,
                                uint32_t msg_type,
                                uint32_t response_length,
                                uint32_t msg_length,
                                bool header_present,
                                uint32_t simd_mode,
                                uint32_t return_format)
  {
     const GenMessageTarget sfid = GEN_SFID_SAMPLER;
     setMessageDescriptor(p, insn, sfid, msg_length, response_length);
     insn->bits3.sampler_gen7.bti = bti;
     insn->bits3.sampler_gen7.sampler = sampler;
     insn->bits3.sampler_gen7.msg_type = msg_type;
     insn->bits3.sampler_gen7.simd_mode = simd_mode;
  }


  static void setTypedWriteMessage(GenEncoder *p,
                                   GenInstruction *insn,
                                   unsigned char bti,
                                   unsigned char msg_type,
                                   uint32_t msg_length,
                                   bool header_present)
  {
     const GenMessageTarget sfid = GEN6_SFID_DATAPORT_RENDER_CACHE;
     setMessageDescriptor(p, insn, sfid, msg_length, 0, header_present);
     insn->bits3.gen7_typed_rw.bti = bti;
     insn->bits3.gen7_typed_rw.msg_type = msg_type;
  }
  static void setDWordScatterMessgae(GenEncoder *p,
                                     GenInstruction *insn,
                                     uint32_t bti,
                                     uint32_t block_size,
                                     uint32_t msg_type,
                                     uint32_t msg_length,
                                     uint32_t response_length)
  {
    const GenMessageTarget sfid = GEN6_SFID_DATAPORT_CONSTANT_CACHE;
    setMessageDescriptor(p, insn, sfid, msg_length, response_length);
    insn->bits3.gen7_dword_rw.msg_type = msg_type;
    insn->bits3.gen7_dword_rw.bti = bti;
    insn->bits3.gen7_dword_rw.block_size = block_size;
    insn->bits3.gen7_dword_rw.invalidate_after_read = 0;
  }
  //////////////////////////////////////////////////////////////////////////
  // Gen Emitter encoding class
  //////////////////////////////////////////////////////////////////////////
  GenEncoder::GenEncoder(uint32_t simdWidth, uint32_t gen) :
    stateNum(0), gen(gen)
  {
    this->curr.execWidth = simdWidth;
    this->curr.quarterControl = GEN_COMPRESSION_Q1;
    this->curr.noMask = 0;
    this->curr.flag = 0;
    this->curr.subFlag = 0;
    this->curr.predicate = GEN_PREDICATE_NORMAL;
    this->curr.inversePredicate = 0;
  }

  void GenEncoder::push(void) {
    assert(stateNum < MAX_STATE_NUM);
    stack[stateNum++] = curr;
  }

  void GenEncoder::pop(void) {
    assert(stateNum > 0);
    curr = stack[--stateNum];
  }

  void GenEncoder::setHeader(GenInstruction *insn) {
    if (this->curr.execWidth == 8)
      insn->header.execution_size = GEN_WIDTH_8;
    else if (this->curr.execWidth == 16)
      insn->header.execution_size = GEN_WIDTH_16;
    else if (this->curr.execWidth == 1)
      insn->header.execution_size = GEN_WIDTH_1;
    else
      NOT_IMPLEMENTED;
    insn->header.acc_wr_control = this->curr.accWrEnable;
    insn->header.quarter_control = this->curr.quarterControl;
    insn->bits1.ia1.nib_ctrl = this->curr.nibControl;
    insn->header.mask_control = this->curr.noMask;
    insn->bits2.ia1.flag_reg_nr = this->curr.flag;
    insn->bits2.ia1.flag_sub_reg_nr = this->curr.subFlag;
    if (this->curr.predicate != GEN_PREDICATE_NONE) {
      insn->header.predicate_control = this->curr.predicate;
      insn->header.predicate_inverse = this->curr.inversePredicate;
    }
    insn->header.saturate = this->curr.saturate;
  }

  void GenEncoder::setDst(GenInstruction *insn, GenRegister dest) {
     if (dest.file != GEN_ARCHITECTURE_REGISTER_FILE)
        assert(dest.nr < 128);

     insn->bits1.da1.dest_reg_file = dest.file;
     insn->bits1.da1.dest_reg_type = dest.type;
     insn->bits1.da1.dest_address_mode = dest.address_mode;
     insn->bits1.da1.dest_reg_nr = dest.nr;
     insn->bits1.da1.dest_subreg_nr = dest.subnr;
     if (dest.hstride == GEN_HORIZONTAL_STRIDE_0)
       dest.hstride = GEN_HORIZONTAL_STRIDE_1;
     insn->bits1.da1.dest_horiz_stride = dest.hstride;
  }

  void GenEncoder::setSrc0(GenInstruction *insn, GenRegister reg) {
     if (reg.file != GEN_ARCHITECTURE_REGISTER_FILE)
        assert(reg.nr < 128);

     if (reg.address_mode == GEN_ADDRESS_DIRECT) {
       insn->bits1.da1.src0_reg_file = reg.file;
       insn->bits1.da1.src0_reg_type = reg.type;
       insn->bits2.da1.src0_abs = reg.absolute;
       insn->bits2.da1.src0_negate = reg.negation;
       insn->bits2.da1.src0_address_mode = reg.address_mode;

       if (reg.file == GEN_IMMEDIATE_VALUE) {
          insn->bits3.ud = reg.value.ud;

          /* Required to set some fields in src1 as well: */
          insn->bits1.da1.src1_reg_file = 0; /* arf */
          insn->bits1.da1.src1_reg_type = reg.type;
       }
       else {
         if (insn->header.access_mode == GEN_ALIGN_1) {
           insn->bits2.da1.src0_subreg_nr = reg.subnr;
           insn->bits2.da1.src0_reg_nr = reg.nr;
         } else {
           insn->bits2.da16.src0_subreg_nr = reg.subnr / 16;
           insn->bits2.da16.src0_reg_nr = reg.nr;
         }

         if (reg.width == GEN_WIDTH_1 &&
             insn->header.execution_size == GEN_WIDTH_1) {
           insn->bits2.da1.src0_horiz_stride = GEN_HORIZONTAL_STRIDE_0;
           insn->bits2.da1.src0_width = GEN_WIDTH_1;
           insn->bits2.da1.src0_vert_stride = GEN_VERTICAL_STRIDE_0;
         }
         else {
           insn->bits2.da1.src0_horiz_stride = reg.hstride;
           insn->bits2.da1.src0_width = reg.width;
           insn->bits2.da1.src0_vert_stride = reg.vstride;
         }
       }
    } else {
       insn->bits1.ia1.src0_reg_file = GEN_GENERAL_REGISTER_FILE;
       insn->bits1.ia1.src0_reg_type = reg.type;
       insn->bits2.ia1.src0_subreg_nr = 0;
       insn->bits2.ia1.src0_indirect_offset = 0;
       insn->bits2.ia1.src0_abs = 0;
       insn->bits2.ia1.src0_negate = 0;
       insn->bits2.ia1.src0_address_mode = reg.address_mode;
       insn->bits2.ia1.src0_horiz_stride = GEN_HORIZONTAL_STRIDE_0;
       insn->bits2.ia1.src0_width = GEN_WIDTH_1;
       insn->bits2.ia1.src0_vert_stride = GEN_VERTICAL_STRIDE_ONE_DIMENSIONAL;
    }
  }

  void GenEncoder::setSrc1(GenInstruction *insn, GenRegister reg) {
     assert(reg.nr < 128);
     assert(reg.file != GEN_ARCHITECTURE_REGISTER_FILE || reg.nr == 0);

     insn->bits1.da1.src1_reg_file = reg.file;
     insn->bits1.da1.src1_reg_type = reg.type;
     insn->bits3.da1.src1_abs = reg.absolute;
     insn->bits3.da1.src1_negate = reg.negation;

     assert(insn->bits1.da1.src0_reg_file != GEN_IMMEDIATE_VALUE);

     if (reg.file == GEN_IMMEDIATE_VALUE)
       insn->bits3.ud = reg.value.ud;
     else {
       assert (reg.address_mode == GEN_ADDRESS_DIRECT);
       if (insn->header.access_mode == GEN_ALIGN_1) {
         insn->bits3.da1.src1_subreg_nr = reg.subnr;
         insn->bits3.da1.src1_reg_nr = reg.nr;
       } else {
         insn->bits3.da16.src1_subreg_nr = reg.subnr / 16;
         insn->bits3.da16.src1_reg_nr = reg.nr;
       }

       if (reg.width == GEN_WIDTH_1 &&
           insn->header.execution_size == GEN_WIDTH_1) {
         insn->bits3.da1.src1_horiz_stride = GEN_HORIZONTAL_STRIDE_0;
         insn->bits3.da1.src1_width = GEN_WIDTH_1;
         insn->bits3.da1.src1_vert_stride = GEN_VERTICAL_STRIDE_0;
       } else {
         insn->bits3.da1.src1_horiz_stride = reg.hstride;
         insn->bits3.da1.src1_width = reg.width;
         insn->bits3.da1.src1_vert_stride = reg.vstride;
       }
     }
  }

  static const uint32_t untypedRWMask[] = {
    GEN_UNTYPED_ALPHA|GEN_UNTYPED_BLUE|GEN_UNTYPED_GREEN|GEN_UNTYPED_RED,
    GEN_UNTYPED_ALPHA|GEN_UNTYPED_BLUE|GEN_UNTYPED_GREEN,
    GEN_UNTYPED_ALPHA|GEN_UNTYPED_BLUE,
    GEN_UNTYPED_ALPHA,
    0
  };

  void GenEncoder::READ64(GenRegister dst, GenRegister tmp, GenRegister addr, GenRegister src, uint32_t bti, uint32_t elemNum) {
    GenRegister dst32 = GenRegister::retype(dst, GEN_TYPE_UD);
    src = GenRegister::retype(src, GEN_TYPE_UD);
    addr = GenRegister::retype(addr, GEN_TYPE_UD);
    tmp = GenRegister::retype(tmp, GEN_TYPE_UD);
    uint32_t originSimdWidth = curr.execWidth;
    uint32_t originPredicate = curr.predicate;
    uint32_t originMask = curr.noMask;
    push();
    for ( uint32_t channels = 0, currQuarter = GEN_COMPRESSION_Q1;
          channels < originSimdWidth; channels += 8, currQuarter++) {
      curr.predicate = GEN_PREDICATE_NONE;
      curr.noMask = GEN_MASK_DISABLE;
      curr.execWidth = 8;
      /* XXX The following instruction is illegal, but it works as SIMD 1*4 mode
         which is what we want here. */
      MOV(GenRegister::h2(addr), GenRegister::suboffset(src, channels));
      ADD(GenRegister::h2(GenRegister::suboffset(addr, 1)), GenRegister::suboffset(src, channels), GenRegister::immd(4));
      MOV(GenRegister::h2(GenRegister::suboffset(addr, 8)), GenRegister::suboffset(src, channels + 4));
      ADD(GenRegister::h2(GenRegister::suboffset(addr, 9)), GenRegister::suboffset(src, channels + 4), GenRegister::immd(4));
      // Let's use SIMD16 to read all bytes for 8 doubles data at one time.
      curr.execWidth = 16;
      this->UNTYPED_READ(tmp, addr, bti, elemNum);
      if (originSimdWidth == 16)
        curr.quarterControl = currQuarter;
      curr.predicate = originPredicate;
      curr.noMask = originMask;
      // Back to simd8 for correct predication flag.
      curr.execWidth = 8;
      MOV(GenRegister::retype(GenRegister::suboffset(dst32, channels * 2), GEN_TYPE_DF), GenRegister::retype(tmp, GEN_TYPE_DF));
    }
    pop();
  }

  void GenEncoder::WRITE64(GenRegister msg, GenRegister data, uint32_t bti, uint32_t elemNum, bool is_scalar) {
    GenRegister data32 = GenRegister::retype(data, GEN_TYPE_UD);
    GenRegister unpacked;
    msg = GenRegister::retype(msg, GEN_TYPE_UD);
    int originSimdWidth = curr.execWidth;
    int originPredicate = curr.predicate;
    int originMask = curr.noMask;
    push();
    for (uint32_t half = 0; half < 2; half++) {
      curr.predicate = GEN_PREDICATE_NONE;
      curr.noMask = GEN_MASK_DISABLE;
      curr.execWidth = 8;
      if (is_scalar) {
        unpacked = data32;
        unpacked.subnr += half * 4;
      } else
        unpacked = GenRegister::unpacked_ud(data32.nr, data32.subnr + half);
      MOV(GenRegister::suboffset(msg, originSimdWidth), unpacked);
      if (originSimdWidth == 16) {
        if (is_scalar) {
          unpacked = data32;
          unpacked.subnr += half * 4;
        } else
          unpacked = GenRegister::unpacked_ud(data32.nr + 2, data32.subnr + half);
        MOV(GenRegister::suboffset(msg, originSimdWidth + 8), unpacked);
        curr.execWidth = 16;
      }
      if (half == 1)
        ADD(GenRegister::retype(msg, GEN_TYPE_UD), GenRegister::retype(msg, GEN_TYPE_UD), GenRegister::immd(4));
      curr.predicate = originPredicate;
      curr.noMask = originMask;
      this->UNTYPED_WRITE(msg, bti, elemNum);
    }
    pop();
  }

  void GenEncoder::UNTYPED_READ(GenRegister dst, GenRegister src, uint32_t bti, uint32_t elemNum) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    assert(elemNum >= 1 || elemNum <= 4);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;
    if (this->curr.execWidth == 8) {
      msg_length = 1;
      response_length = elemNum;
    } else if (this->curr.execWidth == 16) {
      msg_length = 2;
      response_length = 2*elemNum;
    } else
      NOT_IMPLEMENTED;

    this->setHeader(insn);
    this->setDst(insn,  GenRegister::uw16grf(dst.nr, 0));
    this->setSrc0(insn, GenRegister::ud8grf(src.nr, 0));
    this->setSrc1(insn, GenRegister::immud(0));
    setDPUntypedRW(this,
                   insn,
                   bti,
                   untypedRWMask[elemNum],
                   GEN_UNTYPED_READ,
                   msg_length,
                   response_length);
  }

  void GenEncoder::UNTYPED_WRITE(GenRegister msg, uint32_t bti, uint32_t elemNum) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    assert(elemNum >= 1 || elemNum <= 4);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;
    this->setHeader(insn);
    if (this->curr.execWidth == 8) {
      this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UD));
      msg_length = 1+elemNum;
    } else if (this->curr.execWidth == 16) {
      this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UW));
      msg_length = 2*(1+elemNum);
    }
    else
      NOT_IMPLEMENTED;
    this->setSrc0(insn, GenRegister::ud8grf(msg.nr, 0));
    this->setSrc1(insn, GenRegister::immud(0));
    setDPUntypedRW(this,
                   insn,
                   bti,
                   untypedRWMask[elemNum],
                   GEN_UNTYPED_WRITE,
                   msg_length,
                   response_length);
  }

  void GenEncoder::BYTE_GATHER(GenRegister dst, GenRegister src, uint32_t bti, uint32_t elemSize) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;
    if (this->curr.execWidth == 8) {
      msg_length = 1;
      response_length = 1;
    } else if (this->curr.execWidth == 16) {
      msg_length = 2;
      response_length = 2;
    } else
      NOT_IMPLEMENTED;

    this->setHeader(insn);
    this->setDst(insn, GenRegister::uw16grf(dst.nr, 0));
    this->setSrc0(insn, GenRegister::ud8grf(src.nr, 0));
    this->setSrc1(insn, GenRegister::immud(0));
    setDPByteScatterGather(this,
                           insn,
                           bti,
                           elemSize,
                           GEN_BYTE_GATHER,
                           msg_length,
                           response_length);
  }

  void GenEncoder::BYTE_SCATTER(GenRegister msg, uint32_t bti, uint32_t elemSize) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;
    this->setHeader(insn);
    if (this->curr.execWidth == 8) {
      this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UD));
      msg_length = 2;
    } else if (this->curr.execWidth == 16) {
      this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UW));
      msg_length = 4;
    } else
      NOT_IMPLEMENTED;
    this->setSrc0(insn, GenRegister::ud8grf(msg.nr, 0));
    this->setSrc1(insn, GenRegister::immud(0));
    setDPByteScatterGather(this,
                           insn,
                           bti,
                           elemSize,
                           GEN_BYTE_SCATTER,
                           msg_length,
                           response_length);
  }

  void GenEncoder::DWORD_GATHER(GenRegister dst, GenRegister src, uint32_t bti) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;
    uint32_t block_size = 0;
    if (this->curr.execWidth == 8) {
      msg_length = 1;
      response_length = 1;
      block_size = GEN_DWORD_SCATTER_8_DWORDS;
    } else if (this->curr.execWidth == 16) {
      msg_length = 2;
      response_length = 2;
      block_size = GEN_DWORD_SCATTER_16_DWORDS;
    } else
      NOT_IMPLEMENTED;

    this->setHeader(insn);
    this->setDst(insn, dst);
    this->setSrc0(insn, src);
    this->setSrc1(insn, GenRegister::immud(0));
    setDWordScatterMessgae(this,
                           insn,
                           bti,
                           block_size,
                           GEN_DWORD_GATHER,
                           msg_length,
                           response_length);

  }

  void GenEncoder::ATOMIC(GenRegister dst, uint32_t function, GenRegister src, uint32_t bti, uint32_t srcNum) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    uint32_t msg_length = 0;
    uint32_t response_length = 0;

    if (this->curr.execWidth == 8) {
      msg_length = srcNum;
      response_length = 1;
    } else if (this->curr.execWidth == 16) {
      msg_length = 2*srcNum;
      response_length = 2;
    } else
      NOT_IMPLEMENTED;

    this->setHeader(insn);
    this->setDst(insn, GenRegister::uw16grf(dst.nr, 0));
    this->setSrc0(insn, GenRegister::ud8grf(src.nr, 0));
    this->setSrc1(insn, GenRegister::immud(0));

    const GenMessageTarget sfid = GEN_SFID_DATAPORT_DATA_CACHE;
    setMessageDescriptor(this, insn, sfid, msg_length, response_length);
    insn->bits3.gen7_atomic_op.msg_type = GEN_UNTYPED_ATOMIC_READ;
    insn->bits3.gen7_atomic_op.bti = bti;
    insn->bits3.gen7_atomic_op.return_data = 1;
    insn->bits3.gen7_atomic_op.aop_type = function;

    if (this->curr.execWidth == 8)
      insn->bits3.gen7_atomic_op.simd_mode = GEN_ATOMIC_SIMD8;
    else if (this->curr.execWidth == 16)
      insn->bits3.gen7_atomic_op.simd_mode = GEN_ATOMIC_SIMD16;
    else
      NOT_SUPPORTED;

  }

  GenInstruction *GenEncoder::next(uint32_t opcode) {
     GenInstruction insn;
     std::memset(&insn, 0, sizeof(GenInstruction));
     insn.header.opcode = opcode;
     this->store.push_back(insn);
     return &this->store.back();
  }

  INLINE void _handleDouble(GenEncoder *p, uint32_t opcode, GenRegister dst,
                            GenRegister src0, GenRegister src1 = GenRegister::null()) {
       int w = p->curr.execWidth;
       p->push();
       p->curr.nibControl = 0;
       GenInstruction *insn = p->next(opcode);
       p->setHeader(insn);
       p->setDst(insn, dst);
       p->setSrc0(insn, src0);
       if (!GenRegister::isNull(src1))
         p->setSrc1(insn, src1);
       if (w == 8)
         p->curr.nibControl = 1; // second 1/8 mask
       insn = p->next(opcode);
       p->setHeader(insn);
       p->setDst(insn, GenRegister::suboffset(dst, w / 2));
       p->setSrc0(insn, GenRegister::suboffset(src0, w / 2));
       if (!GenRegister::isNull(src1))
         p->setSrc1(insn, GenRegister::suboffset(src1, w / 2));
       p->pop();
  }

  // Double register accessing is a little special,
  // Per Gen spec, then only supported mode is SIMD8 and, it only
  // handles four doubles each time.
  // We need to lower down SIMD16 to two SIMD8 and lower down SIMD8
  // to two SIMD1x4.
  INLINE void handleDouble(GenEncoder *p, uint32_t opcode, GenRegister dst,
                           GenRegister src0, GenRegister src1 = GenRegister::null()) {
      if (p->curr.execWidth == 8)
        _handleDouble(p, opcode, dst, src0, src1);
      else if (p->curr.execWidth == 16) {
        p->push();
        p->curr.execWidth = 8;
        p->curr.quarterControl = GEN_COMPRESSION_Q1;
        _handleDouble(p, opcode, dst, src0, src1);
        p->curr.quarterControl = GEN_COMPRESSION_Q2;
        if (!GenRegister::isNull(src1))
          src1 = GenRegister::offset(src1, 2);
        _handleDouble(p, opcode, GenRegister::offset(dst, 2), GenRegister::offset(src0, 2), src1);
        p->pop();
      }
  }

  INLINE void alu1(GenEncoder *p, uint32_t opcode, GenRegister dst, GenRegister src) {
     if (dst.isdf() && src.isdf()) {
       handleDouble(p, opcode, dst, src);
     } else if (dst.isint64() && src.isint64()) { // handle int64
       int execWidth = p->curr.execWidth;
       p->push();
       p->curr.execWidth = 8;
       for (int nib = 0; nib < execWidth / 4; nib ++) {
         p->curr.chooseNib(nib);
         p->MOV(dst.bottom_half(), src.bottom_half());
         p->MOV(dst.top_half(), src.top_half());
         dst = GenRegister::suboffset(dst, 4);
         src = GenRegister::suboffset(src, 4);
       }
       p->pop();
     } else if (needToSplitAlu1(p, dst, src) == false) {
       GenInstruction *insn = p->next(opcode);
       p->setHeader(insn);
       p->setDst(insn, dst);
       p->setSrc0(insn, src);
     } else {
       GenInstruction *insnQ1, *insnQ2;

       // Instruction for the first quarter
       insnQ1 = p->next(opcode);
       p->setHeader(insnQ1);
       insnQ1->header.quarter_control = GEN_COMPRESSION_Q1;
       insnQ1->header.execution_size = GEN_WIDTH_8;
       p->setDst(insnQ1, dst);
       p->setSrc0(insnQ1, src);

       // Instruction for the second quarter
       insnQ2 = p->next(opcode);
       p->setHeader(insnQ2);
       insnQ2->header.quarter_control = GEN_COMPRESSION_Q2;
       insnQ2->header.execution_size = GEN_WIDTH_8;
       p->setDst(insnQ2, GenRegister::Qn(dst, 1));
       p->setSrc0(insnQ2, GenRegister::Qn(src, 1));
     }
  }

  INLINE void alu2(GenEncoder *p,
                   uint32_t opcode,
                   GenRegister dst,
                   GenRegister src0,
                   GenRegister src1)
  {
    if (dst.isdf() && src0.isdf() && src1.isdf()) {
       handleDouble(p, opcode, dst, src0, src1);
    } else if (needToSplitAlu2(p, dst, src0, src1) == false) {
       GenInstruction *insn = p->next(opcode);
       p->setHeader(insn);
       p->setDst(insn, dst);
       p->setSrc0(insn, src0);
       p->setSrc1(insn, src1);
    } else {
       GenInstruction *insnQ1, *insnQ2;

       // Instruction for the first quarter
       insnQ1 = p->next(opcode);
       p->setHeader(insnQ1);
       insnQ1->header.quarter_control = GEN_COMPRESSION_Q1;
       insnQ1->header.execution_size = GEN_WIDTH_8;
       p->setDst(insnQ1, dst);
       p->setSrc0(insnQ1, src0);
       p->setSrc1(insnQ1, src1);

       // Instruction for the second quarter
       insnQ2 = p->next(opcode);
       p->setHeader(insnQ2);
       insnQ2->header.quarter_control = GEN_COMPRESSION_Q2;
       insnQ2->header.execution_size = GEN_WIDTH_8;
       p->setDst(insnQ2, GenRegister::Qn(dst, 1));
       p->setSrc0(insnQ2, GenRegister::Qn(src0, 1));
       p->setSrc1(insnQ2, GenRegister::Qn(src1, 1));
    }
  }

#define NO_SWIZZLE ((0<<0) | (1<<2) | (2<<4) | (3<<6))

  static GenInstruction *alu3(GenEncoder *p,
                              uint32_t opcode,
                              GenRegister dest,
                              GenRegister src0,
                              GenRegister src1,
                              GenRegister src2)
  {
     GenInstruction *insn = p->next(opcode);

     assert(dest.file == GEN_GENERAL_REGISTER_FILE);
     assert(dest.nr < 128);
     assert(dest.address_mode == GEN_ADDRESS_DIRECT);
     assert(dest.type = GEN_TYPE_F);
     insn->bits1.da3src.dest_reg_file = 0;
     insn->bits1.da3src.dest_reg_nr = dest.nr;
     insn->bits1.da3src.dest_subreg_nr = dest.subnr / 16;
     insn->bits1.da3src.dest_writemask = 0xf;
     p->setHeader(insn);
     insn->header.access_mode = GEN_ALIGN_16;
     insn->header.execution_size = GEN_WIDTH_8;

     assert(src0.file == GEN_GENERAL_REGISTER_FILE);
     assert(src0.address_mode == GEN_ADDRESS_DIRECT);
     assert(src0.nr < 128);
     assert(src0.type == GEN_TYPE_F);
     insn->bits2.da3src.src0_swizzle = NO_SWIZZLE;
     insn->bits2.da3src.src0_subreg_nr = src0.subnr / 4 ;
     insn->bits2.da3src.src0_reg_nr = src0.nr;
     insn->bits1.da3src.src0_abs = src0.absolute;
     insn->bits1.da3src.src0_negate = src0.negation;
     insn->bits2.da3src.src0_rep_ctrl = src0.vstride == GEN_VERTICAL_STRIDE_0;

     assert(src1.file == GEN_GENERAL_REGISTER_FILE);
     assert(src1.address_mode == GEN_ADDRESS_DIRECT);
     assert(src1.nr < 128);
     assert(src1.type == GEN_TYPE_F);
     insn->bits2.da3src.src1_swizzle = NO_SWIZZLE;
     insn->bits2.da3src.src1_subreg_nr_low = (src1.subnr / 4) & 0x3;
     insn->bits3.da3src.src1_subreg_nr_high = (src1.subnr / 4) >> 2;
     insn->bits2.da3src.src1_rep_ctrl = src1.vstride == GEN_VERTICAL_STRIDE_0;
     insn->bits3.da3src.src1_reg_nr = src1.nr;
     insn->bits1.da3src.src1_abs = src1.absolute;
     insn->bits1.da3src.src1_negate = src1.negation;

     assert(src2.file == GEN_GENERAL_REGISTER_FILE);
     assert(src2.address_mode == GEN_ADDRESS_DIRECT);
     assert(src2.nr < 128);
     assert(src2.type == GEN_TYPE_F);
     insn->bits3.da3src.src2_swizzle = NO_SWIZZLE;
     insn->bits3.da3src.src2_subreg_nr = src2.subnr / 4;
     insn->bits3.da3src.src2_rep_ctrl = src2.vstride == GEN_VERTICAL_STRIDE_0;
     insn->bits3.da3src.src2_reg_nr = src2.nr;
     insn->bits1.da3src.src2_abs = src2.absolute;
     insn->bits1.da3src.src2_negate = src2.negation;

     // Emit second half of the instruction
     if (p->curr.execWidth == 16) {
      GenInstruction q1Insn = *insn;
      insn = p->next(opcode);
      *insn = q1Insn;
      insn->header.quarter_control = GEN_COMPRESSION_Q2;
      insn->bits1.da3src.dest_reg_nr++;
      if (insn->bits2.da3src.src0_rep_ctrl == 0)
        insn->bits2.da3src.src0_reg_nr++;
      if (insn->bits2.da3src.src1_rep_ctrl == 0)
        insn->bits3.da3src.src1_reg_nr++;
      if (insn->bits3.da3src.src2_rep_ctrl == 0)
        insn->bits3.da3src.src2_reg_nr++;
     }

     return insn;
  }

#undef NO_SWIZZLE

#define ALU1(OP) \
  void GenEncoder::OP(GenRegister dest, GenRegister src0) { \
    alu1(this, GEN_OPCODE_##OP, dest, src0); \
  }

#define ALU2(OP) \
  void GenEncoder::OP(GenRegister dest, GenRegister src0, GenRegister src1) { \
    alu2(this, GEN_OPCODE_##OP, dest, src0, src1); \
  }

#define ALU3(OP) \
  void GenEncoder::OP(GenRegister dest, GenRegister src0, GenRegister src1, GenRegister src2) { \
    alu3(this, GEN_OPCODE_##OP, dest, src0, src1, src2); \
  }

  void GenEncoder::LOAD_DF_IMM(GenRegister dest, GenRegister tmp, double value) {
    union { double d; unsigned u[2]; } u;
    u.d = value;
    GenRegister r = GenRegister::retype(tmp, GEN_TYPE_UD);
    push();
    curr.predicate = GEN_PREDICATE_NONE;
    curr.execWidth = 1;
    MOV(r, GenRegister::immud(u.u[1]));
    MOV(GenRegister::suboffset(r, 1), GenRegister::immud(u.u[0]));
    pop();
    r.type = GEN_TYPE_DF;
    r.vstride = GEN_VERTICAL_STRIDE_0;
    r.width = GEN_WIDTH_1;
    r.hstride = GEN_HORIZONTAL_STRIDE_0;
    push();
    uint32_t width = curr.execWidth;
    curr.execWidth = 8;
    curr.predicate = GEN_PREDICATE_NONE;
    curr.noMask = 1;
    curr.quarterControl = GEN_COMPRESSION_Q1;
    MOV(dest, r);
    if (width == 16) {
      curr.quarterControl = GEN_COMPRESSION_Q2;
      MOV(GenRegister::offset(dest, 2), r);
    }
    pop();
  }

  void GenEncoder::UPSAMPLE_SHORT(GenRegister dest, GenRegister src0, GenRegister src1) {
    dest.type = GEN_TYPE_B;
    dest.hstride = GEN_HORIZONTAL_STRIDE_2;
    src0.type = GEN_TYPE_B;
    src0.hstride = GEN_HORIZONTAL_STRIDE_2;
    src1.type = GEN_TYPE_B;
    src1.hstride = GEN_HORIZONTAL_STRIDE_2;
    MOV(dest, src1);
    dest.subnr ++;
    MOV(dest, src0);
  }

  void GenEncoder::UPSAMPLE_INT(GenRegister dest, GenRegister src0, GenRegister src1) {
    dest.type = GEN_TYPE_W;
    dest.hstride = GEN_HORIZONTAL_STRIDE_2;
    src0.type = GEN_TYPE_W;
    src0.hstride = GEN_HORIZONTAL_STRIDE_2;
    src1.type = GEN_TYPE_W;
    src1.hstride = GEN_HORIZONTAL_STRIDE_2;
    MOV(dest, src1);
    dest.subnr += 2;
    MOV(dest, src0);
  }

  void GenEncoder::LOAD_INT64_IMM(GenRegister dest, int64_t value) {
    GenRegister u0 = GenRegister::immd((int)value), u1 = GenRegister::immd(value >> 32);
    int execWidth = curr.execWidth;
    push();
    curr.execWidth = 8;
    for(int nib = 0; nib < execWidth/4; nib ++) {
      curr.chooseNib(nib);
      MOV(dest.top_half(), u1);
      MOV(dest.bottom_half(), u0);
      dest = GenRegister::suboffset(dest, 4);
    }
    pop();
  }

  void GenEncoder::MOV_DF(GenRegister dest, GenRegister src0, GenRegister r) {
    int w = curr.execWidth;
    if (src0.isdf()) {
      GBE_ASSERT(0); // MOV DF is called from convert instruction,
                     // We should never convert a df to a df.
    } else {
      GenRegister r0 = GenRegister::h2(r);
      push();
      curr.execWidth = 8;
      curr.predicate = GEN_PREDICATE_NONE;
      MOV(r0, src0);
      MOV(GenRegister::suboffset(r0, 4), GenRegister::suboffset(src0, 4));
      curr.predicate = GEN_PREDICATE_NORMAL;
      curr.quarterControl = 0;
      curr.nibControl = 0;
      MOV(dest, r);
      curr.nibControl = 1;
      MOV(GenRegister::suboffset(dest, 4), GenRegister::suboffset(r, 8));
      pop();
      if (w == 16) {
        push();
        curr.execWidth = 8;
        curr.predicate = GEN_PREDICATE_NONE;
        MOV(r0, GenRegister::suboffset(src0, 8));
        MOV(GenRegister::suboffset(r0, 4), GenRegister::suboffset(src0, 12));
        curr.predicate = GEN_PREDICATE_NORMAL;
        curr.quarterControl = 1;
        curr.nibControl = 0;
        MOV(GenRegister::suboffset(dest, 8), r);
        curr.nibControl = 1;
        MOV(GenRegister::suboffset(dest, 12), GenRegister::suboffset(r, 8));
        pop();
      }
    }
  }

  ALU1(MOV)
  ALU1(RNDZ)
  ALU1(RNDE)
  ALU1(RNDD)
  ALU1(RNDU)
  ALU1(FBH)
  ALU1(FBL)
  ALU1(F16TO32)
  ALU1(F32TO16)
  ALU2(SEL)
  ALU1(NOT)
  ALU2(AND)
  ALU2(OR)
  ALU2(XOR)
  ALU2(SHR)
  ALU2(SHL)
  ALU2(RSR)
  ALU2(RSL)
  ALU2(ASR)
  ALU1(FRC)
  ALU2(MAC)
  ALU1(LZD)
  ALU2(LINE)
  ALU2(PLN)
  ALU2(MACH)
  ALU3(MAD)

  void GenEncoder::SUBB(GenRegister dest, GenRegister src0, GenRegister src1) {
    push();
    curr.accWrEnable = 1;
    alu2(this, GEN_OPCODE_SUBB, dest, src0, src1);
    pop();
  }

  void GenEncoder::ADDC(GenRegister dest, GenRegister src0, GenRegister src1) {
    push();
    curr.accWrEnable = 1;
    alu2(this, GEN_OPCODE_ADDC, dest, src0, src1);
    pop();
  }

  void GenEncoder::ADD(GenRegister dest, GenRegister src0, GenRegister src1) {
     if (src0.type == GEN_TYPE_F ||
         (src0.file == GEN_IMMEDIATE_VALUE &&
          src0.type == GEN_TYPE_VF)) {
        assert(src1.type != GEN_TYPE_UD);
        assert(src1.type != GEN_TYPE_D);
     }

     if (src1.type == GEN_TYPE_F ||
         (src1.file == GEN_IMMEDIATE_VALUE &&
          src1.type == GEN_TYPE_VF)) {
        assert(src0.type != GEN_TYPE_UD);
        assert(src0.type != GEN_TYPE_D);
     }

     alu2(this, GEN_OPCODE_ADD, dest, src0, src1);
  }

  void GenEncoder::MUL(GenRegister dest, GenRegister src0, GenRegister src1) {
     if (src0.type == GEN_TYPE_D ||
         src0.type == GEN_TYPE_UD ||
         src1.type == GEN_TYPE_D ||
         src1.type == GEN_TYPE_UD)
        assert(dest.type != GEN_TYPE_F);

     if (src0.type == GEN_TYPE_F ||
         (src0.file == GEN_IMMEDIATE_VALUE &&
          src0.type == GEN_TYPE_VF)) {
        assert(src1.type != GEN_TYPE_UD);
        assert(src1.type != GEN_TYPE_D);
     }

     if (src1.type == GEN_TYPE_F ||
         (src1.file == GEN_IMMEDIATE_VALUE &&
          src1.type == GEN_TYPE_VF)) {
        assert(src0.type != GEN_TYPE_UD);
        assert(src0.type != GEN_TYPE_D);
     }

     assert(src0.file != GEN_ARCHITECTURE_REGISTER_FILE ||
            src0.nr != GEN_ARF_ACCUMULATOR);
     assert(src1.file != GEN_ARCHITECTURE_REGISTER_FILE ||
            src1.nr != GEN_ARF_ACCUMULATOR);

     alu2(this, GEN_OPCODE_MUL, dest, src0, src1);
  }


  void GenEncoder::NOP(void) {
    GenInstruction *insn = this->next(GEN_OPCODE_NOP);
    this->setDst(insn, GenRegister::retype(GenRegister::f4grf(0,0), GEN_TYPE_UD));
    this->setSrc0(insn, GenRegister::retype(GenRegister::f4grf(0,0), GEN_TYPE_UD));
    this->setSrc1(insn, GenRegister::immud(0x0));
  }

  void GenEncoder::BARRIER(GenRegister src) {
     GenInstruction *insn = this->next(GEN_OPCODE_SEND);
     this->setHeader(insn);
     this->setDst(insn, GenRegister::null());
     this->setSrc0(insn, src);
     setMessageDescriptor(this, insn, GEN_SFID_MESSAGE_GATEWAY, 1, 0);
     insn->bits3.msg_gateway.sub_function_id = GEN_BARRIER_MSG;
     insn->bits3.msg_gateway.notify = 0x1;
  }
  void GenEncoder::FENCE(GenRegister dst) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    this->setHeader(insn);
    this->setDst(insn, dst);
    this->setSrc0(insn, dst);
    setMessageDescriptor(this, insn, GEN_SFID_DATAPORT_DATA_CACHE, 1, 1, 1);
    insn->bits3.gen7_memory_fence.msg_type = GEN_MEM_FENCE;
    insn->bits3.gen7_memory_fence.commit_enable = 0x1;
  }

  void GenEncoder::JMPI(GenRegister src) {
    alu2(this, GEN_OPCODE_JMPI, GenRegister::ip(), GenRegister::ip(), src);
    NOP();
  }

  void GenEncoder::patchJMPI(uint32_t insnID, int32_t jumpDistance) {
    GenInstruction &insn = this->store[insnID];
    GBE_ASSERT(insnID < this->store.size());
    GBE_ASSERT(insn.header.opcode == GEN_OPCODE_JMPI);
    if ( jumpDistance > -32769 && jumpDistance < 32768 ) {
        this->setSrc1(&insn, GenRegister::immd(jumpDistance));
    } else if ( insn.header.predicate_control == GEN_PREDICATE_NONE ) {
      // For the conditional jump distance out of S15 range, we need to use an
      // inverted jmp followed by a add ip, ip, distance to implement.
      // A little hacky as we need to change the nop instruction to add
      // instruction manually.
      // If this is a unconditional jump, we just need to add the IP directly.
      // FIXME there is an optimization method which we can insert a
      // ADD instruction on demand. But that will need some extra analysis
      // for all the branching instruction. And need to adjust the distance
      // for those branch instruction's start point and end point contains
      // this instruction.
      insn.header.opcode = GEN_OPCODE_ADD;
      this->setDst(&insn, GenRegister::ip());
      this->setSrc0(&insn, GenRegister::ip());
      this->setSrc1(&insn, GenRegister::immd((jumpDistance + 2) * 8));
    } else {
      insn.header.predicate_inverse ^= 1;
      this->setSrc1(&insn, GenRegister::immd(2));
      GenInstruction &insn2 = this->store[insnID+1];
      GBE_ASSERT(insn2.header.opcode == GEN_OPCODE_NOP);
      GBE_ASSERT(insnID < this->store.size());
      insn2.header.predicate_control = GEN_PREDICATE_NONE;
      insn2.header.opcode = GEN_OPCODE_ADD;
      this->setDst(&insn2, GenRegister::ip());
      this->setSrc0(&insn2, GenRegister::ip());
      this->setSrc1(&insn2, GenRegister::immd(jumpDistance * 8));
    }
  }

  void GenEncoder::CMP(uint32_t conditional, GenRegister src0, GenRegister src1) {
    if (needToSplitCmp(this, src0, src1) == false) {
      GenInstruction *insn = this->next(GEN_OPCODE_CMP);
      this->setHeader(insn);
      insn->header.destreg_or_condmod = conditional;
      this->setDst(insn, GenRegister::null());
      this->setSrc0(insn, src0);
      this->setSrc1(insn, src1);
    } else {
      GenInstruction *insnQ1, *insnQ2;

      // Instruction for the first quarter
      insnQ1 = this->next(GEN_OPCODE_CMP);
      this->setHeader(insnQ1);
      insnQ1->header.quarter_control = GEN_COMPRESSION_Q1;
      insnQ1->header.execution_size = GEN_WIDTH_8;
      insnQ1->header.destreg_or_condmod = conditional;
      this->setDst(insnQ1, GenRegister::null());
      this->setSrc0(insnQ1, src0);
      this->setSrc1(insnQ1, src1);

      // Instruction for the second quarter
      insnQ2 = this->next(GEN_OPCODE_CMP);
      this->setHeader(insnQ2);
      insnQ2->header.quarter_control = GEN_COMPRESSION_Q2;
      insnQ2->header.execution_size = GEN_WIDTH_8;
      insnQ2->header.destreg_or_condmod = conditional;
      this->setDst(insnQ2, GenRegister::null());
      this->setSrc0(insnQ2, GenRegister::Qn(src0, 1));
      this->setSrc1(insnQ2, GenRegister::Qn(src1, 1));
    }
  }

  void GenEncoder::SEL_CMP(uint32_t conditional,
                           GenRegister dst,
                           GenRegister src0,
                           GenRegister src1)
  {
    GenInstruction *insn = this->next(GEN_OPCODE_SEL);
    GBE_ASSERT(curr.predicate == GEN_PREDICATE_NONE);
    this->setHeader(insn);
    insn->header.destreg_or_condmod = conditional;
    this->setDst(insn, dst);
    this->setSrc0(insn, src0);
    this->setSrc1(insn, src1);
  }

  void GenEncoder::WAIT(void) {
     GenInstruction *insn = this->next(GEN_OPCODE_WAIT);
     GenRegister src = GenRegister::notification1();
     this->setDst(insn, GenRegister::null());
     this->setSrc0(insn, src);
     this->setSrc1(insn, GenRegister::null());
     insn->header.execution_size = 0; /* must */
     insn->header.predicate_control = 0;
     insn->header.quarter_control = 0;
  }

  void GenEncoder::MATH(GenRegister dst, uint32_t function, GenRegister src0, GenRegister src1) {
     GenInstruction *insn = this->next(GEN_OPCODE_MATH);
     assert(dst.file == GEN_GENERAL_REGISTER_FILE);
     assert(src0.file == GEN_GENERAL_REGISTER_FILE);
     assert(src1.file == GEN_GENERAL_REGISTER_FILE);
     assert(dst.hstride == GEN_HORIZONTAL_STRIDE_1);

     if (function == GEN_MATH_FUNCTION_INT_DIV_QUOTIENT ||
         function == GEN_MATH_FUNCTION_INT_DIV_REMAINDER ||
         function == GEN_MATH_FUNCTION_INT_DIV_QUOTIENT_AND_REMAINDER) {
        assert(src0.type != GEN_TYPE_F);
        assert(src1.type != GEN_TYPE_F);
     } else {
        assert(src0.type == GEN_TYPE_F);
        assert(src1.type == GEN_TYPE_F);
     }

     insn->header.destreg_or_condmod = function;
     this->setHeader(insn);
     this->setDst(insn, dst);
     this->setSrc0(insn, src0);
     this->setSrc1(insn, src1);

     if (function == GEN_MATH_FUNCTION_INT_DIV_QUOTIENT ||
         function == GEN_MATH_FUNCTION_INT_DIV_REMAINDER) {
        insn->header.execution_size = GEN_WIDTH_8;
        insn->header.quarter_control = GEN_COMPRESSION_Q1;

        if(this->curr.execWidth == 16) {
          GenInstruction *insn2 = this->next(GEN_OPCODE_MATH);
          GenRegister new_dest, new_src0, new_src1;
          new_dest = GenRegister::QnPhysical(dst, 1);
          new_src0 = GenRegister::QnPhysical(src0, 1);
          new_src1 = GenRegister::QnPhysical(src1, 1);
          insn2->header.destreg_or_condmod = function;
          this->setHeader(insn2);
          insn2->header.execution_size = GEN_WIDTH_8;
          insn2->header.quarter_control = GEN_COMPRESSION_Q2;
          this->setDst(insn2, new_dest);
          this->setSrc0(insn2, new_src0);
          this->setSrc1(insn2, new_src1);
        }

     }
  }

  void GenEncoder::MATH(GenRegister dst, uint32_t function, GenRegister src) {
     GenInstruction *insn = this->next(GEN_OPCODE_MATH);
     assert(dst.file == GEN_GENERAL_REGISTER_FILE);
     assert(src.file == GEN_GENERAL_REGISTER_FILE);
     assert(dst.hstride == GEN_HORIZONTAL_STRIDE_1);
     assert(src.type == GEN_TYPE_F);

     insn->header.destreg_or_condmod = function;
     this->setHeader(insn);
     this->setDst(insn, dst);
     this->setSrc0(insn, src);
  }

  void GenEncoder::SAMPLE(GenRegister dest,
                          GenRegister msg,
                          bool header_present,
                          unsigned char bti,
                          unsigned char sampler,
                          unsigned int coord_cnt,
                          uint32_t simdWidth,
                          uint32_t writemask,
                          uint32_t return_format)
  {
     if (writemask == 0) return;
     uint32_t msg_type =  (simdWidth == 16) ?
                            GEN_SAMPLER_MESSAGE_SIMD16_SAMPLE : GEN_SAMPLER_MESSAGE_SIMD8_SAMPLE;
     uint32_t response_length = (4 * (simdWidth / 8));
     uint32_t msg_length = (coord_cnt * (simdWidth / 8));
     if (header_present)
       msg_length++;
     uint32_t simd_mode = (simdWidth == 16) ?
                            GEN_SAMPLER_SIMD_MODE_SIMD16 : GEN_SAMPLER_SIMD_MODE_SIMD8;
     GenInstruction *insn = this->next(GEN_OPCODE_SEND);
     this->setHeader(insn);
     this->setDst(insn, dest);
     this->setSrc0(insn, msg);
     setSamplerMessage(this, insn, bti, sampler, msg_type,
                       response_length, msg_length,
                       header_present,
                       simd_mode, return_format);
  }

  void GenEncoder::TYPED_WRITE(GenRegister msg, bool header_present, unsigned char bti)
  {
     GenInstruction *insn = this->next(GEN_OPCODE_SEND);
     uint32_t msg_type = GEN_TYPED_WRITE;
     uint32_t msg_length = header_present ? 9 : 8;
     this->setHeader(insn);
     this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UD));
     this->setSrc0(insn, msg);
     setTypedWriteMessage(this, insn, bti, msg_type, msg_length, header_present);
  }
  static void setScratchMessage(GenEncoder *p,
                                   GenInstruction *insn,
                                   uint32_t offset,
                                   uint32_t block_size,
                                   uint32_t channel_mode,
                                   uint32_t msg_type,
                                   uint32_t msg_length,
                                   uint32_t response_length)
  {
     const GenMessageTarget sfid = GEN_SFID_DATAPORT_DATA_CACHE;
     setMessageDescriptor(p, insn, sfid, msg_length, response_length, true);
     insn->bits3.gen7_scratch_rw.block_size = block_size;
     insn->bits3.gen7_scratch_rw.msg_type = msg_type;
     insn->bits3.gen7_scratch_rw.channel_mode = channel_mode;
     insn->bits3.gen7_scratch_rw.offset = offset;
     insn->bits3.gen7_scratch_rw.category = 1;
  }

  void GenEncoder::SCRATCH_WRITE(GenRegister msg, uint32_t offset, uint32_t size, uint32_t src_num, uint32_t channel_mode)
  {
     assert(src_num == 1 || src_num ==2);
     uint32_t block_size = src_num == 1 ? GEN_SCRATCH_BLOCK_SIZE_1 : GEN_SCRATCH_BLOCK_SIZE_2;
     GenInstruction *insn = this->next(GEN_OPCODE_SEND);
     this->setHeader(insn);
     this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UD));
     this->setSrc0(insn, msg);
     this->setSrc1(insn, GenRegister::immud(0));
     // here src_num means register that will be write out: in terms of 32byte register number
     setScratchMessage(this, insn, offset, block_size, channel_mode, GEN_SCRATCH_WRITE, src_num+1, 0);
  }

  void GenEncoder::SCRATCH_READ(GenRegister dst, GenRegister src, uint32_t offset, uint32_t size, uint32_t dst_num, uint32_t channel_mode)
  {
     assert(dst_num == 1 || dst_num ==2);
     uint32_t block_size = dst_num == 1 ? GEN_SCRATCH_BLOCK_SIZE_1 : GEN_SCRATCH_BLOCK_SIZE_2;
     GenInstruction *insn = this->next(GEN_OPCODE_SEND);
     this->setHeader(insn);
     this->setDst(insn, dst);
     this->setSrc0(insn, src);
     this->setSrc1(insn, GenRegister::immud(0));
      // here dst_num is the register that will be write-back: in terms of 32byte register
     setScratchMessage(this, insn, offset, block_size, channel_mode, GEN_SCRATCH_READ, 1, dst_num);
  }

  void GenEncoder::EOT(uint32_t msg) {
    GenInstruction *insn = this->next(GEN_OPCODE_SEND);
    this->setDst(insn, GenRegister::retype(GenRegister::null(), GEN_TYPE_UD));
    this->setSrc0(insn, GenRegister::ud8grf(msg,0));
    this->setSrc1(insn, GenRegister::immud(0));
    insn->header.execution_size = GEN_WIDTH_8;
    insn->bits3.spawner_gen5.resource = GEN_DO_NOT_DEREFERENCE_URB;
    insn->bits3.spawner_gen5.msg_length = 1;
    insn->bits3.spawner_gen5.end_of_thread = 1;
    insn->header.destreg_or_condmod = GEN_SFID_THREAD_SPAWNER;
  }
} /* namespace gbe */

