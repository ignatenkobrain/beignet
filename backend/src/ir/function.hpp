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

/**
 * \file function.hpp
 * \author Benjamin Segovia <benjamin.segovia@intel.com>
 */
#ifndef __GBE_IR_FUNCTION_HPP__
#define __GBE_IR_FUNCTION_HPP__

#include "ir/value.hpp"
#include "ir/register.hpp"
#include "ir/instruction.hpp"
#include "ir/profile.hpp"
#include "sys/vector.hpp"
#include "sys/list.hpp"
#include "sys/alloc.hpp"

#include <ostream>

namespace gbe {
namespace ir {

  /*! Function basic blocks really belong to a function since:
   *  1 - registers used in the basic blocks belongs to the function register
   *      file
   *  2 - branches point to basic blocks of the same function
   */
  class BasicBlock : public NonCopyable
  {
  public:
    /*! Empty basic block */
    BasicBlock(Function &fn);
    /*! Releases all the instructions */
    ~BasicBlock(void);
    /*! Append a new instruction in the stream */
    void append(Instruction &insn) { instructions.push_back(&insn); }
    /*! Return the number of instruction in the block */
    INLINE uint32_t insnNum(void) { return instructions.size(); }
    /*! Apply the given functor on all instructions */
    template <typename T>
    INLINE void apply(const T &functor) const {
      for (auto it = instructions.begin(); it != instructions.end(); ++it)
        functor(**it);
    }
  private:
    friend class Function;           //!< Owns the basic blocks
    list<Instruction*> instructions; //!< Sequence of instructions in the block
    Function &fn;                    //!< Function the block belongs to
    GBE_CLASS(BasicBlock);
  };

  /*! A function is no more that a set of declared registers and a set of
   *  basic blocks
   */
  class Function : public NonCopyable
  {
  public:
    /*! Create an empty function */
    Function(const std::string &name, Profile profile = PROFILE_OCL);
    /*! Release everything *including* the basic block pointers */
    ~Function(void);
    /*! Get the function profile */
    INLINE Profile getProfile(void) const { return profile; }
    /*! Get a new valid register */
    INLINE Register newRegister(RegisterData::Family family) {
      return this->file.append(family);
    }
    /*! Get the function name */
    const std::string &getName(void) const { return name; }
    /*! Extract the register from the register file */
    INLINE RegisterData getRegisterData(Register ID) const { return file.get(ID); }
    /*! Get the register family from the register itself */
    INLINE RegisterData::Family getRegisterFamiy(Register ID) const {
      return this->getRegisterData(ID).family;
    }
    /*! Get the register index from the tuple vector */
    INLINE Register getRegister(Tuple ID, uint32_t which) const {
      return file.get(ID, which);
    }
    /*! Get the register file */
    INLINE const RegisterFile &getRegisterFile(void) const { return file; }
    /*! Get the given value ie immediate from the function */
    INLINE Immediate getImmediate(ImmediateIndex ID) const {
      GBE_ASSERT(ID < immediateNum());
      return immediates[ID];
    }
    /*! Create a new immediate and returns its index */
    INLINE ImmediateIndex newImmediate(const Immediate &imm) {
      const ImmediateIndex index(this->immediateNum());
      this->immediates.push_back(imm);
      return index;
    }
    /*! Allocate a new instruction (with the growing pool) */
    INLINE Instruction *newInstruction(void) {
      return new (insnPool.allocate()) Instruction();
    }
    /*! Deallocate an instruction (with the growing pool) */
    INLINE void deleteInstruction(Instruction *insn) {
      insnPool.deallocate(insn);
    }
    /*! Get input register */
    INLINE Register getInput(uint32_t ID) const {
      GBE_ASSERT(ID < inputNum());
      return inputs[ID];
    }
    /*! Get output register */
    INLINE Register getOutput(uint32_t ID) const {
      GBE_ASSERT(ID < outputNum());
      return outputs[ID];
    }
    /*! Get block ID */
    INLINE const BasicBlock &getBlock(uint32_t ID) const {
      GBE_ASSERT(ID < blockNum());
      GBE_ASSERT(blocks[ID] != NULL);
      return *blocks[ID];
    }
    /*! Function returns a structure by pointer (see ptx32 ABI) */
    INLINE void setStructReturned(bool isReturned) { structReturned = isReturned; }
    /*! Indicate if a structure is returned from the function */
    INLINE bool isStructReturned(void) const { return structReturned; }
    /*! Create a new label (still not bound to a basic block) */
    LabelIndex newLabel(void);
    /*! Number of registers in the register file */
    INLINE uint32_t regNum(void) const { return file.regNum(); }
    /*! Number of register tuples in the register file */
    INLINE uint32_t tupleNum(void) const { return file.tupleNum(); }
    /*! Number of labels in the function */
    INLINE uint32_t labelNum(void) const { return labels.size(); }
    /*! Number of immediate values in the function */
    INLINE uint32_t immediateNum(void) const { return immediates.size(); }
    /*! Get the number of input register */
    INLINE uint32_t inputNum(void) const { return inputs.size(); }
    /*! Get the number of output register */
    INLINE uint32_t outputNum(void) const { return outputs.size(); }
    /*! Number of blocks in the function */
    INLINE uint32_t blockNum(void) const { return blocks.size(); }
    /*! Output an immediate value in a stream */
    void outImmediate(std::ostream &out, ImmediateIndex index) const;
  private:
    friend class Context;         //!< Can freely modify a function
    std::string name;             //!< Function name
    vector<Register> inputs;      //!< Input registers of the function
    vector<Register> outputs;     //!< Output registers of the function
    vector<BasicBlock*> labels;   //!< Each label points to a basic block
    vector<Immediate> immediates; //!< All immediate values in the function
    vector<BasicBlock*> blocks;   //!< All chained basic blocks
    RegisterFile file;            //!< RegisterDatas used by the instructions
    GrowingPool<Instruction> insnPool; //!< For fast instruction allocation
    bool structReturned;               //!< First argument is pointer to struct
    Profile profile;                   //!< Current function profile
    GBE_CLASS(Function);
  };

  /*! Output the function string in the given stream */
  std::ostream &operator<< (std::ostream &out, const Function &fn);

} /* namespace ir */
} /* namespace gbe */

#endif /* __GBE_IR_FUNCTION_HPP__ */
