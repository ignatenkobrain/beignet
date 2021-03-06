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
 *         Alexei Soupikov <alexei.soupikov@intel.com>
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>

#include "intel/intel_gpgpu.h"
#include "intel/intel_defines.h"
#include "intel/intel_structs.h"
#include "intel/intel_batchbuffer.h"
#include "intel/intel_driver.h"

#include "cl_alloc.h"
#include "cl_utils.h"
#include "cl_sampler.h"

#ifndef CL_VERSION_1_2
#define CL_MEM_OBJECT_IMAGE1D                       0x10F4
#define CL_MEM_OBJECT_IMAGE1D_ARRAY                 0x10F5
#define CL_MEM_OBJECT_IMAGE1D_BUFFER                0x10F6
#define CL_MEM_OBJECT_IMAGE2D_ARRAY                 0x10F3
#endif

#define GEN_CMD_MEDIA_OBJECT  (0x71000000)
#define MO_TS_BIT             (1 << 24)
#define MO_RETAIN_BIT         (1 << 28)
#define SAMPLER_STATE_SIZE    (16)

#define TIMESTAMP_ADDR        0x2358

/* Stores both binding tables and surface states */
typedef struct surface_heap {
  uint32_t binding_table[256];
  char surface[256][sizeof(gen6_surface_state_t)];
} surface_heap_t;

typedef struct intel_event {
  intel_batchbuffer_t *batch;
  drm_intel_bo* buffer;
  drm_intel_bo* ts_buf;
  int status;
} intel_event_t;

#define MAX_IF_DESC    32

/* We can bind only a limited number of buffers */
enum { max_buf_n = 128 };

enum { max_img_n = 128};

enum {max_sampler_n = 16 };

/* Handle GPGPU state */
struct intel_gpgpu
{
  intel_driver_t *drv;
  intel_batchbuffer_t *batch;
  cl_gpgpu_kernel *ker;
  drm_intel_bo *binded_buf[max_buf_n];  /* all buffers binded for the call */
  uint32_t target_buf_offset[max_buf_n];/* internal offset for buffers binded for the call */
  uint32_t binded_offset[max_buf_n];    /* their offsets in the curbe buffer */
  uint32_t binded_n;                    /* number of buffers binded */

  unsigned long img_bitmap;              /* image usage bitmap. */
  unsigned int img_index_base;          /* base index for image surface.*/
  drm_intel_bo *binded_img[max_img_n];  /* all images binded for the call */

  unsigned long sampler_bitmap;          /* sampler usage bitmap. */

  struct { drm_intel_bo *bo; } stack_b;
  struct { drm_intel_bo *bo; } idrt_b;
  struct { drm_intel_bo *bo; } surface_heap_b;
  struct { drm_intel_bo *bo; } vfe_state_b;
  struct { drm_intel_bo *bo; } curbe_b;
  struct { drm_intel_bo *bo; } sampler_state_b;
  struct { drm_intel_bo *bo; } sampler_border_color_state_b;
  struct { drm_intel_bo *bo; } perf_b;
  struct { drm_intel_bo *bo; } scratch_b;
  struct { drm_intel_bo *bo; } constant_b;
  struct { drm_intel_bo *bo; } time_stamp_b;  /* time stamp buffer */

  uint32_t per_thread_scratch;
  struct {
    uint32_t num_cs_entries;
    uint32_t size_cs_entry;  /* size of one entry in 512bit elements */
  } urb;

  uint32_t max_threads;      /* max threads requested by the user */
};

typedef struct intel_gpgpu intel_gpgpu_t;


static void
intel_gpgpu_sync(void *buf)
{
  if (buf)
    drm_intel_bo_wait_rendering((drm_intel_bo *)buf);
}

static void *intel_gpgpu_ref_batch_buf(intel_gpgpu_t *gpgpu)
{
  if (gpgpu->batch->last_bo)
    drm_intel_bo_reference(gpgpu->batch->last_bo);

  return gpgpu->batch->last_bo;
}

static void intel_gpgpu_unref_batch_buf(void *buf)
{
  if (buf)
    drm_intel_bo_unreference((drm_intel_bo *)buf);
}

static void
intel_gpgpu_delete(intel_gpgpu_t *gpgpu)
{
  if (gpgpu == NULL)
    return;
  if(gpgpu->time_stamp_b.bo)
    drm_intel_bo_unreference(gpgpu->time_stamp_b.bo);
  if (gpgpu->surface_heap_b.bo)
    drm_intel_bo_unreference(gpgpu->surface_heap_b.bo);
  if (gpgpu->idrt_b.bo)
    drm_intel_bo_unreference(gpgpu->idrt_b.bo);
  if (gpgpu->vfe_state_b.bo)
    drm_intel_bo_unreference(gpgpu->vfe_state_b.bo);
  if (gpgpu->curbe_b.bo)
    drm_intel_bo_unreference(gpgpu->curbe_b.bo);
  if (gpgpu->sampler_state_b.bo)
    drm_intel_bo_unreference(gpgpu->sampler_state_b.bo);
  if (gpgpu->sampler_border_color_state_b.bo)
    drm_intel_bo_unreference(gpgpu->sampler_border_color_state_b.bo);
  if (gpgpu->perf_b.bo)
    drm_intel_bo_unreference(gpgpu->perf_b.bo);
  if (gpgpu->stack_b.bo)
    drm_intel_bo_unreference(gpgpu->stack_b.bo);
  if (gpgpu->scratch_b.bo)
    drm_intel_bo_unreference(gpgpu->scratch_b.bo);

  if(gpgpu->constant_b.bo)
    drm_intel_bo_unreference(gpgpu->constant_b.bo);

  intel_batchbuffer_delete(gpgpu->batch);
  cl_free(gpgpu);
}

static intel_gpgpu_t*
intel_gpgpu_new(intel_driver_t *drv)
{
  intel_gpgpu_t *state = NULL;

  TRY_ALLOC_NO_ERR (state, CALLOC(intel_gpgpu_t));
  state->drv = drv;
  state->batch = intel_batchbuffer_new(state->drv);
  assert(state->batch);

exit:
  return state;
error:
  intel_gpgpu_delete(state);
  state = NULL;
  goto exit;
}

static void
intel_gpgpu_select_pipeline(intel_gpgpu_t *gpgpu)
{
  BEGIN_BATCH(gpgpu->batch, 1);
  OUT_BATCH(gpgpu->batch, CMD_PIPELINE_SELECT | PIPELINE_SELECT_MEDIA);
  ADVANCE_BATCH(gpgpu->batch);
}

static void
intel_gpgpu_set_base_address(intel_gpgpu_t *gpgpu)
{
  const uint32_t def_cc = cc_llc_l3; /* default Cache Control value */
  BEGIN_BATCH(gpgpu->batch, 10);
  OUT_BATCH(gpgpu->batch, CMD_STATE_BASE_ADDRESS | 8);
  /* 0, Gen State Mem Obj CC, Stateless Mem Obj CC, Stateless Access Write Back */
  OUT_BATCH(gpgpu->batch, 0 | (def_cc << 8) | (def_cc << 4) | (0 << 3)| BASE_ADDRESS_MODIFY);    /* General State Base Addr   */
  /* 0, State Mem Obj CC */
  /* We use a state base address for the surface heap since IVB clamp the
   * binding table pointer at 11 bits. So, we cannot use pointers directly while
   * using the surface heap
   */
  OUT_RELOC(gpgpu->batch, gpgpu->surface_heap_b.bo,
            I915_GEM_DOMAIN_INSTRUCTION,
            I915_GEM_DOMAIN_INSTRUCTION,
            0 | (def_cc << 8) | (def_cc << 4) | (0 << 3)| BASE_ADDRESS_MODIFY);
  OUT_BATCH(gpgpu->batch, 0 | (def_cc << 8) | BASE_ADDRESS_MODIFY); /* Dynamic State Base Addr */
  OUT_BATCH(gpgpu->batch, 0 | (def_cc << 8) | BASE_ADDRESS_MODIFY); /* Indirect Obj Base Addr */
  OUT_BATCH(gpgpu->batch, 0 | (def_cc << 8) | BASE_ADDRESS_MODIFY); /* Instruction Base Addr  */
  /* If we output an AUB file, we limit the total size to 64MB */
#if USE_FULSIM
  OUT_BATCH(gpgpu->batch, 0x04000000 | BASE_ADDRESS_MODIFY); /* General State Access Upper Bound */
  OUT_BATCH(gpgpu->batch, 0x04000000 | BASE_ADDRESS_MODIFY); /* Dynamic State Access Upper Bound */
  OUT_BATCH(gpgpu->batch, 0x04000000 | BASE_ADDRESS_MODIFY); /* Indirect Obj Access Upper Bound */
  OUT_BATCH(gpgpu->batch, 0x04000000 | BASE_ADDRESS_MODIFY); /* Instruction Access Upper Bound */
#else
  OUT_BATCH(gpgpu->batch, 0 | BASE_ADDRESS_MODIFY);
  /* According to mesa i965 driver code, we must set the dynamic state access upper bound
   * to a valid bound value, otherwise, the border color pointer may be rejected and you
   * may get incorrect border color. This is a known hardware bug. */
  OUT_BATCH(gpgpu->batch, 0xfffff000 | BASE_ADDRESS_MODIFY);
  OUT_BATCH(gpgpu->batch, 0 | BASE_ADDRESS_MODIFY);
  OUT_BATCH(gpgpu->batch, 0 | BASE_ADDRESS_MODIFY);
#endif /* USE_FULSIM */
  ADVANCE_BATCH(gpgpu->batch);
}

static void
intel_gpgpu_load_vfe_state(intel_gpgpu_t *gpgpu)
{
  BEGIN_BATCH(gpgpu->batch, 8);
  OUT_BATCH(gpgpu->batch, CMD_MEDIA_STATE_POINTERS | (8-2));

  if(gpgpu->per_thread_scratch > 0) {
    OUT_RELOC(gpgpu->batch, gpgpu->scratch_b.bo,
              I915_GEM_DOMAIN_RENDER,
              I915_GEM_DOMAIN_RENDER,
              gpgpu->per_thread_scratch/1024 - 1);
  }
  else {
    OUT_BATCH(gpgpu->batch, 0);
  }
  /* max_thread | urb entries | (reset_gateway|bypass_gate_way | gpgpu_mode) */
  OUT_BATCH(gpgpu->batch, 0 | ((gpgpu->max_threads - 1) << 16) | (64 << 8) | 0xc4);
  OUT_BATCH(gpgpu->batch, 0);
  /* curbe_size */
  OUT_BATCH(gpgpu->batch, 480);
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, 0);
  ADVANCE_BATCH(gpgpu->batch);
}

static void
intel_gpgpu_load_curbe_buffer(intel_gpgpu_t *gpgpu)
{
  BEGIN_BATCH(gpgpu->batch, 4);
  OUT_BATCH(gpgpu->batch, CMD(2,0,1) | (4 - 2));  /* length-2 */
  OUT_BATCH(gpgpu->batch, 0);                     /* mbz */
// XXX
#if 1
  OUT_BATCH(gpgpu->batch,
            gpgpu->urb.size_cs_entry*
            gpgpu->urb.num_cs_entries*32);
#else
  OUT_BATCH(gpgpu->batch, 5120);
#endif
  OUT_RELOC(gpgpu->batch, gpgpu->curbe_b.bo, I915_GEM_DOMAIN_INSTRUCTION, 0, 0);
  ADVANCE_BATCH(gpgpu->batch);
}

static void
intel_gpgpu_load_idrt(intel_gpgpu_t *gpgpu)
{
  BEGIN_BATCH(gpgpu->batch, 4);
  OUT_BATCH(gpgpu->batch, CMD(2,0,2) | (4 - 2)); /* length-2 */
  OUT_BATCH(gpgpu->batch, 0);                    /* mbz */
  OUT_BATCH(gpgpu->batch, 1 << 5);
  OUT_RELOC(gpgpu->batch, gpgpu->idrt_b.bo, I915_GEM_DOMAIN_INSTRUCTION, 0, 0);
  ADVANCE_BATCH(gpgpu->batch);
}

static const uint32_t gpgpu_l3_config_reg1[] = {
  0x00080040, 0x02040040, 0x00800040, 0x01000038,
  0x02000030, 0x01000038, 0x00000038, 0x00000040,
  0x0A140091, 0x09100091, 0x08900091, 0x08900091
};

static const uint32_t gpgpu_l3_config_reg2[] = {
  0x00000000, 0x00000000, 0x00080410, 0x00080410,
  0x00040410, 0x00040420, 0x00080420, 0x00080020,
  0x00204080, 0x00244890, 0x00284490, 0x002444A0
};

/* Emit PIPE_CONTROLs to write the current GPU timestamp into a buffer. */
static void
intel_gpgpu_write_timestamp(intel_gpgpu_t *gpgpu, int idx)
{
  BEGIN_BATCH(gpgpu->batch, 5);
  OUT_BATCH(gpgpu->batch, CMD_PIPE_CONTROL | (5-2));
  OUT_BATCH(gpgpu->batch, GEN7_PIPE_CONTROL_WRITE_TIMESTAMP);
  OUT_RELOC(gpgpu->batch, gpgpu->time_stamp_b.bo,
          I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION,
          GEN7_PIPE_CONTROL_GLOBAL_GTT_WRITE | idx * sizeof(uint64_t));
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, 0);
  ADVANCE_BATCH();
}

static void
intel_gpgpu_pipe_control(intel_gpgpu_t *gpgpu)
{
  BEGIN_BATCH(gpgpu->batch, SIZEOF32(gen6_pipe_control_t));
  gen6_pipe_control_t* pc = (gen6_pipe_control_t*)
    intel_batchbuffer_alloc_space(gpgpu->batch, 0);
  memset(pc, 0, sizeof(*pc));
  pc->dw0.length = SIZEOF32(gen6_pipe_control_t) - 2;
  pc->dw0.instruction_subopcode = GEN7_PIPE_CONTROL_SUBOPCODE_3D_CONTROL;
  pc->dw0.instruction_opcode = GEN7_PIPE_CONTROL_OPCODE_3D_CONTROL;
  pc->dw0.instruction_pipeline = GEN7_PIPE_CONTROL_3D;
  pc->dw0.instruction_type = GEN7_PIPE_CONTROL_INSTRUCTION_GFX;
  pc->dw1.render_target_cache_flush_enable = 1;
  pc->dw1.texture_cache_invalidation_enable = 1;
  pc->dw1.cs_stall = 1;
  pc->dw1.dc_flush_enable = 1;
  ADVANCE_BATCH(gpgpu->batch);
}

static void
intel_gpgpu_set_L3(intel_gpgpu_t *gpgpu, uint32_t use_slm)
{
  BEGIN_BATCH(gpgpu->batch, 6);
  OUT_BATCH(gpgpu->batch, CMD_LOAD_REGISTER_IMM | 1); /* length - 2 */
  OUT_BATCH(gpgpu->batch, GEN7_L3_CNTL_REG2_ADDRESS_OFFSET);
  if (use_slm)
    OUT_BATCH(gpgpu->batch, gpgpu_l3_config_reg1[8]);
  else
    OUT_BATCH(gpgpu->batch, gpgpu_l3_config_reg1[4]);

  OUT_BATCH(gpgpu->batch, CMD_LOAD_REGISTER_IMM | 1); /* length - 2 */
  OUT_BATCH(gpgpu->batch, GEN7_L3_CNTL_REG3_ADDRESS_OFFSET);
  if (use_slm)
    OUT_BATCH(gpgpu->batch, gpgpu_l3_config_reg2[8]);
  else
    OUT_BATCH(gpgpu->batch, gpgpu_l3_config_reg2[4]);
  ADVANCE_BATCH(gpgpu->batch);
  intel_gpgpu_pipe_control(gpgpu);
}

static void
intel_gpgpu_batch_start(intel_gpgpu_t *gpgpu)
{
  intel_batchbuffer_start_atomic(gpgpu->batch, 256);
  intel_gpgpu_pipe_control(gpgpu);
  intel_gpgpu_set_L3(gpgpu, gpgpu->ker->use_slm);
  intel_gpgpu_select_pipeline(gpgpu);
  intel_gpgpu_set_base_address(gpgpu);
  intel_gpgpu_load_vfe_state(gpgpu);
  intel_gpgpu_load_curbe_buffer(gpgpu);
  intel_gpgpu_load_idrt(gpgpu);

  if (gpgpu->perf_b.bo) {
    BEGIN_BATCH(gpgpu->batch, 3);
    OUT_BATCH(gpgpu->batch,
              (0x28 << 23) | /* MI_REPORT_PERF_COUNT */
              (3 - 2));      /* length-2 */
    OUT_RELOC(gpgpu->batch, gpgpu->perf_b.bo,
              I915_GEM_DOMAIN_RENDER,
              I915_GEM_DOMAIN_RENDER,
              0 |  /* Offset for the start "counters" */
              1);  /* Use GTT and not PGTT */
    OUT_BATCH(gpgpu->batch, 0);
    ADVANCE_BATCH(gpgpu->batch);
  }

  /* Insert PIPE_CONTROL for time stamp of start*/
  if (gpgpu->time_stamp_b.bo)
    intel_gpgpu_write_timestamp(gpgpu, 0);
}

static void
intel_gpgpu_batch_end(intel_gpgpu_t *gpgpu, int32_t flush_mode)
{
  /* Insert PIPE_CONTROL for time stamp of end*/
  if (gpgpu->time_stamp_b.bo)
    intel_gpgpu_write_timestamp(gpgpu, 1);

  /* Insert the performance counter command */
  if (gpgpu->perf_b.bo) {
    BEGIN_BATCH(gpgpu->batch, 3);
    OUT_BATCH(gpgpu->batch,
              (0x28 << 23) | /* MI_REPORT_PERF_COUNT */
              (3 - 2));      /* length-2 */
    OUT_RELOC(gpgpu->batch, gpgpu->perf_b.bo,
              I915_GEM_DOMAIN_RENDER,
              I915_GEM_DOMAIN_RENDER,
              512 |  /* Offset for the end "counters" */
              1);    /* Use GTT and not PGTT */
    OUT_BATCH(gpgpu->batch, 0);
    ADVANCE_BATCH(gpgpu->batch);
  }

  if(flush_mode) intel_gpgpu_pipe_control(gpgpu);
  intel_batchbuffer_end_atomic(gpgpu->batch);
}

static void
intel_gpgpu_batch_reset(intel_gpgpu_t *gpgpu, size_t sz)
{
  intel_batchbuffer_reset(gpgpu->batch, sz);
}
/* check we do not get a 0 starting address for binded buf */
static void
intel_gpgpu_check_binded_buf_address(intel_gpgpu_t *gpgpu)
{
  uint32_t i;
  for (i = 0; i < gpgpu->binded_n; ++i)
    assert(gpgpu->binded_buf[i]->offset != 0);
}

static void
intel_gpgpu_flush(intel_gpgpu_t *gpgpu)
{
  intel_batchbuffer_emit_mi_flush(gpgpu->batch);
  intel_batchbuffer_flush(gpgpu->batch);
  intel_gpgpu_check_binded_buf_address(gpgpu);
}

static void
intel_gpgpu_state_init(intel_gpgpu_t *gpgpu,
                       uint32_t max_threads,
                       uint32_t size_cs_entry,
                       int profiling)
{
  drm_intel_bufmgr *bufmgr = gpgpu->drv->bufmgr;
  drm_intel_bo *bo;

  /* Binded buffers */
  gpgpu->binded_n = 0;
  gpgpu->img_bitmap = 0;
  gpgpu->img_index_base = 3;
  gpgpu->sampler_bitmap = ~((1 << max_sampler_n) - 1);

  /* URB */
  gpgpu->urb.num_cs_entries = 64;
  gpgpu->urb.size_cs_entry = size_cs_entry;
  gpgpu->max_threads = max_threads;

  /* Set the profile buffer*/
  if(gpgpu->time_stamp_b.bo)
    dri_bo_unreference(gpgpu->time_stamp_b.bo);
  gpgpu->time_stamp_b.bo = NULL;
  if (profiling) {
    bo = dri_bo_alloc(gpgpu->drv->bufmgr, "timestamp query", 4096, 4096);
    assert(bo);
    gpgpu->time_stamp_b.bo = bo;
  }

  /* Constant URB  buffer */
  if(gpgpu->curbe_b.bo)
    dri_bo_unreference(gpgpu->curbe_b.bo);
  uint32_t size_cb = gpgpu->urb.num_cs_entries * gpgpu->urb.size_cs_entry * 64;
  size_cb = ALIGN(size_cb, 4096);
  bo = dri_bo_alloc(gpgpu->drv->bufmgr, "CURBE_BUFFER", size_cb, 64);
  assert(bo);
  gpgpu->curbe_b.bo = bo;

  /* surface state */
  if(gpgpu->surface_heap_b.bo)
    dri_bo_unreference(gpgpu->surface_heap_b.bo);
  bo = dri_bo_alloc(bufmgr,
                    "SURFACE_HEAP",
                    sizeof(surface_heap_t),
                    32);
  assert(bo);
  dri_bo_map(bo, 1);
  memset(bo->virtual, 0, sizeof(surface_heap_t));
  gpgpu->surface_heap_b.bo = bo;

  /* Interface descriptor remap table */
  if(gpgpu->idrt_b.bo)
    dri_bo_unreference(gpgpu->idrt_b.bo);
  bo = dri_bo_alloc(bufmgr,
                    "IDRT",
                    MAX_IF_DESC * sizeof(struct gen6_interface_descriptor),
                    32);
  assert(bo);
  gpgpu->idrt_b.bo = bo;

  /* vfe state */
  if(gpgpu->vfe_state_b.bo)
    dri_bo_unreference(gpgpu->vfe_state_b.bo);
  gpgpu->vfe_state_b.bo = NULL;

  /* sampler state */
  if (gpgpu->sampler_state_b.bo)
    dri_bo_unreference(gpgpu->sampler_state_b.bo);
  bo = dri_bo_alloc(gpgpu->drv->bufmgr,
                    "SAMPLER_STATE",
                    GEN_MAX_SAMPLERS * sizeof(gen6_sampler_state_t),
                    32);
  assert(bo);
  dri_bo_map(bo, 1);
  memset(bo->virtual, 0, sizeof(gen6_sampler_state_t) * GEN_MAX_SAMPLERS);
  gpgpu->sampler_state_b.bo = bo;

  /* sampler border color state */
  if (gpgpu->sampler_border_color_state_b.bo)
    dri_bo_unreference(gpgpu->sampler_border_color_state_b.bo);
  bo = dri_bo_alloc(gpgpu->drv->bufmgr,
                    "SAMPLER_BORDER_COLOR_STATE",
                    sizeof(gen7_sampler_border_color_t),
                    32);
  assert(bo);
  dri_bo_map(bo, 1);
  memset(bo->virtual, 0, sizeof(gen7_sampler_border_color_t));
  gpgpu->sampler_border_color_state_b.bo = bo;

  /* stack */
  if (gpgpu->stack_b.bo)
    dri_bo_unreference(gpgpu->stack_b.bo);
  gpgpu->stack_b.bo = NULL;
}

static void
intel_gpgpu_set_buf_reloc_gen7(intel_gpgpu_t *gpgpu, int32_t index, dri_bo* obj_bo, uint32_t obj_bo_offset)
{
  surface_heap_t *heap = gpgpu->surface_heap_b.bo->virtual;
  heap->binding_table[index] = offsetof(surface_heap_t, surface) +
                               index * sizeof(gen7_surface_state_t);
  dri_bo_emit_reloc(gpgpu->surface_heap_b.bo,
                    I915_GEM_DOMAIN_RENDER,
                    I915_GEM_DOMAIN_RENDER,
                    obj_bo_offset,
                    heap->binding_table[index] +
                    offsetof(gen7_surface_state_t, ss1),
                    obj_bo);
}

static dri_bo*
intel_gpgpu_alloc_constant_buffer(intel_gpgpu_t *gpgpu, uint32_t size)
{
  uint32_t s = size - 1;
  assert(size != 0);

  surface_heap_t *heap = gpgpu->surface_heap_b.bo->virtual;
  gen7_surface_state_t *ss2 = (gen7_surface_state_t *) heap->surface[2];
  memset(ss2, 0, sizeof(gen7_surface_state_t));
  ss2->ss0.surface_type = I965_SURFACE_BUFFER;
  ss2->ss0.surface_format = I965_SURFACEFORMAT_RAW;
  ss2->ss2.width  = s & 0x7f;            /* bits 6:0 of sz */
  ss2->ss2.height = (s >> 7) & 0x3fff;   /* bits 20:7 of sz */
  ss2->ss3.depth  = (s >> 21) & 0x3ff;   /* bits 30:21 of sz */
  ss2->ss5.cache_control = cc_llc_l3;
  heap->binding_table[2] = offsetof(surface_heap_t, surface) + 2* sizeof(gen7_surface_state_t);

  if(gpgpu->constant_b.bo)
    dri_bo_unreference(gpgpu->constant_b.bo);
  gpgpu->constant_b.bo = drm_intel_bo_alloc(gpgpu->drv->bufmgr, "CONSTANT_BUFFER", s, 64);
  assert(gpgpu->constant_b.bo);
  ss2->ss1.base_addr = gpgpu->constant_b.bo->offset;
  dri_bo_emit_reloc(gpgpu->surface_heap_b.bo,
                      I915_GEM_DOMAIN_RENDER,
                      I915_GEM_DOMAIN_RENDER,
                      0,
                      heap->binding_table[2] +
                      offsetof(gen7_surface_state_t, ss1),
                      gpgpu->constant_b.bo);
  return gpgpu->constant_b.bo;
}


/* Map address space with two 2GB surfaces. One surface for untyped message and
 * one surface for byte scatters / gathers. Actually the HW does not require two
 * surfaces but Fulsim complains
 */
static void
intel_gpgpu_map_address_space(intel_gpgpu_t *gpgpu)
{
  surface_heap_t *heap = gpgpu->surface_heap_b.bo->virtual;
  gen7_surface_state_t *ss0 = (gen7_surface_state_t *) heap->surface[0];
  gen7_surface_state_t *ss1 = (gen7_surface_state_t *) heap->surface[1];
  memset(ss0, 0, sizeof(gen7_surface_state_t));
  memset(ss1, 0, sizeof(gen7_surface_state_t));
  ss1->ss0.surface_type = ss0->ss0.surface_type = I965_SURFACE_BUFFER;
  ss1->ss0.surface_format = ss0->ss0.surface_format = I965_SURFACEFORMAT_RAW;
  ss1->ss2.width  = ss0->ss2.width  = 127;   /* bits 6:0 of sz */
  ss1->ss2.height = ss0->ss2.height = 16383; /* bits 20:7 of sz */
  ss0->ss3.depth  = 1023; /* bits 30:21 of sz */
  ss1->ss3.depth  = 1023;  /* bits 30:21 of sz */
  ss1->ss5.cache_control = ss0->ss5.cache_control = cc_llc_l3;
  heap->binding_table[0] = offsetof(surface_heap_t, surface);
  heap->binding_table[1] = sizeof(gen7_surface_state_t) + offsetof(surface_heap_t, surface);
}

static int
intel_get_surface_type(cl_mem_object_type type)
{
  switch (type) {
  case CL_MEM_OBJECT_IMAGE1D: return I965_SURFACE_1D;
  case CL_MEM_OBJECT_IMAGE2D: return I965_SURFACE_2D;
  case CL_MEM_OBJECT_IMAGE3D: return I965_SURFACE_3D;
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
    NOT_IMPLEMENTED;
    break;
  default:
      assert(0);
  }
  return 0;
}

static void
intel_gpgpu_bind_image_gen7(intel_gpgpu_t *gpgpu,
                              uint32_t index,
                              dri_bo* obj_bo,
                              uint32_t obj_bo_offset,
                              uint32_t format,
                              cl_mem_object_type type,
                              int32_t w,
                              int32_t h,
                              int32_t depth,
                              int32_t pitch,
                              int32_t tiling)
{
  surface_heap_t *heap = gpgpu->surface_heap_b.bo->virtual;
  gen7_surface_state_t *ss = (gen7_surface_state_t *) heap->surface[index];

  memset(ss, 0, sizeof(*ss));

  ss->ss0.surface_type = intel_get_surface_type(type);
  ss->ss0.surface_format = format;
  ss->ss1.base_addr = obj_bo->offset;
  ss->ss2.width = w - 1;
  ss->ss2.height = h - 1;
  ss->ss3.depth = depth - 1;
  ss->ss4.not_str_buf.rt_view_extent = depth - 1;
  ss->ss4.not_str_buf.min_array_element = 0;
  ss->ss3.pitch = pitch - 1;
  ss->ss5.cache_control = cc_llc_l3;
  if (tiling == GPGPU_TILE_X) {
    ss->ss0.tiled_surface = 1;
    ss->ss0.tile_walk = I965_TILEWALK_XMAJOR;
  } else if (tiling == GPGPU_TILE_Y) {
    ss->ss0.tiled_surface = 1;
    ss->ss0.tile_walk = I965_TILEWALK_YMAJOR;
  }
  ss->ss0.render_cache_rw_mode = 1; /* XXX do we need to set it? */
  intel_gpgpu_set_buf_reloc_gen7(gpgpu, index, obj_bo, obj_bo_offset);
  gpgpu->binded_img[index - gpgpu->img_index_base] = obj_bo;
}

static void
intel_gpgpu_bind_buf(intel_gpgpu_t *gpgpu, drm_intel_bo *buf, uint32_t offset,
                     uint32_t internal_offset, uint32_t cchint)
{
  assert(gpgpu->binded_n < max_buf_n);
  gpgpu->binded_buf[gpgpu->binded_n] = buf;
  gpgpu->target_buf_offset[gpgpu->binded_n] = internal_offset;
  gpgpu->binded_offset[gpgpu->binded_n] = offset;
  gpgpu->binded_n++;
}

static void
intel_gpgpu_set_scratch(intel_gpgpu_t * gpgpu, uint32_t per_thread_size)
{
  drm_intel_bufmgr *bufmgr = gpgpu->drv->bufmgr;
  drm_intel_bo* old = gpgpu->scratch_b.bo;
  uint32_t total = per_thread_size * gpgpu->max_threads;

  gpgpu->per_thread_scratch = per_thread_size;

  if(old && old->size < total) {
    drm_intel_bo_unreference(old);
    old = NULL;
  }

  if(!old)
    gpgpu->scratch_b.bo = drm_intel_bo_alloc(bufmgr, "SCRATCH_BO", total, 4096);
}
static void
intel_gpgpu_set_stack(intel_gpgpu_t *gpgpu, uint32_t offset, uint32_t size, uint32_t cchint)
{
  drm_intel_bufmgr *bufmgr = gpgpu->drv->bufmgr;
  gpgpu->stack_b.bo = drm_intel_bo_alloc(bufmgr, "STACK", size, 64);
  intel_gpgpu_bind_buf(gpgpu, gpgpu->stack_b.bo, offset, 0, cchint);
}

static void
intel_gpgpu_bind_image(intel_gpgpu_t *gpgpu,
                       uint32_t index,
                       cl_buffer *obj_bo,
                       uint32_t obj_bo_offset,
                       uint32_t format,
                       cl_mem_object_type type,
                       int32_t w,
                       int32_t h,
                       int32_t depth,
                       int32_t pitch,
                       cl_gpgpu_tiling tiling)
{
  intel_gpgpu_bind_image_gen7(gpgpu, index, (drm_intel_bo*) obj_bo, obj_bo_offset, format, type, w, h, depth, pitch, tiling);
  assert(index < GEN_MAX_SURFACES);
}

static void
intel_gpgpu_build_idrt(intel_gpgpu_t *gpgpu, cl_gpgpu_kernel *kernel)
{
  gen6_interface_descriptor_t *desc;
  drm_intel_bo *bo = NULL, *ker_bo = NULL;

  bo = gpgpu->idrt_b.bo;
  dri_bo_map(bo, 1);
  assert(bo->virtual);
  desc = (gen6_interface_descriptor_t*) bo->virtual;

  memset(desc, 0, sizeof(*desc));
  ker_bo = (drm_intel_bo *) kernel->bo;
  desc->desc0.kernel_start_pointer = ker_bo->offset >> 6; /* reloc */
  desc->desc1.single_program_flow = 1;
  desc->desc1.floating_point_mode = 0; /* use IEEE-754 rule */
  desc->desc5.rounding_mode = 0; /* round to nearest even */
  desc->desc2.sampler_state_pointer = gpgpu->sampler_state_b.bo->offset >> 5;
  desc->desc3.binding_table_entry_count = 0; /* no prefetch */
  desc->desc3.binding_table_pointer = 0;
  desc->desc4.curbe_read_len = kernel->curbe_sz / 32;
  desc->desc4.curbe_read_offset = 0;

  /* Barriers / SLM are automatically handled on Gen7+ */
  if (gpgpu->drv->gen_ver == 7 || gpgpu->drv->gen_ver == 75) {
    size_t slm_sz = kernel->slm_sz;
    desc->desc5.group_threads_num = kernel->use_slm ? kernel->thread_n : 0;
    desc->desc5.barrier_enable = kernel->use_slm;
    if (slm_sz <= 4*KB)
      slm_sz = 4*KB;
    else if (slm_sz <= 8*KB)
      slm_sz = 8*KB;
    else if (slm_sz <= 16*KB)
      slm_sz = 16*KB;
    else if (slm_sz <= 32*KB)
      slm_sz = 32*KB;
    else
      slm_sz = 64*KB;
    slm_sz = slm_sz >> 12;
    desc->desc5.slm_sz = slm_sz;
  }
  else
    desc->desc5.group_threads_num = kernel->barrierID; /* BarrierID on GEN6 */

  dri_bo_emit_reloc(bo,
                    I915_GEM_DOMAIN_INSTRUCTION, 0,
                    0,
                    offsetof(gen6_interface_descriptor_t, desc0),
                    ker_bo);

  dri_bo_emit_reloc(bo,
                    I915_GEM_DOMAIN_SAMPLER, 0,
                    0,
                    offsetof(gen6_interface_descriptor_t, desc2),
                    gpgpu->sampler_state_b.bo);
  dri_bo_unmap(bo);
}

static void
intel_gpgpu_upload_curbes(intel_gpgpu_t *gpgpu, const void* data, uint32_t size)
{
  unsigned char *curbe = NULL;
  cl_gpgpu_kernel *k = gpgpu->ker;
  uint32_t i, j;

  /* Upload the data first */
  dri_bo_map(gpgpu->curbe_b.bo, 1);
  assert(gpgpu->curbe_b.bo->virtual);
  curbe = (unsigned char *) gpgpu->curbe_b.bo->virtual;
  memcpy(curbe, data, size);

  /* Now put all the relocations for our flat address space */
  for (i = 0; i < k->thread_n; ++i)
    for (j = 0; j < gpgpu->binded_n; ++j) {
      *(uint32_t*)(curbe + gpgpu->binded_offset[j]+i*k->curbe_sz) = gpgpu->binded_buf[j]->offset + gpgpu->target_buf_offset[j];
      drm_intel_bo_emit_reloc(gpgpu->curbe_b.bo,
                              gpgpu->binded_offset[j]+i*k->curbe_sz,
                              gpgpu->binded_buf[j],
                              gpgpu->target_buf_offset[j],
                              I915_GEM_DOMAIN_RENDER,
                              I915_GEM_DOMAIN_RENDER);
    }
  dri_bo_unmap(gpgpu->curbe_b.bo);
}

static void
intel_gpgpu_upload_samplers(intel_gpgpu_t *gpgpu, const void *data, uint32_t n)
{
  if (n) {
    const size_t sz = n * sizeof(gen6_sampler_state_t);
    memcpy(gpgpu->sampler_state_b.bo->virtual, data, sz);
  }
}

int translate_wrap_mode(uint32_t cl_address_mode, int using_nearest)
{
   switch( cl_address_mode ) {
   case CLK_ADDRESS_NONE:
   case CLK_ADDRESS_REPEAT:
      return GEN_TEXCOORDMODE_WRAP;
   case CLK_ADDRESS_CLAMP:
      return GEN_TEXCOORDMODE_CLAMP_BORDER;
   case CLK_ADDRESS_CLAMP_TO_EDGE:
      return GEN_TEXCOORDMODE_CLAMP;
   case CLK_ADDRESS_MIRRORED_REPEAT:
      return GEN_TEXCOORDMODE_MIRROR;
   default:
      return GEN_TEXCOORDMODE_WRAP;
   }
}

static void
intel_gpgpu_insert_sampler(intel_gpgpu_t *gpgpu, uint32_t index, uint32_t clk_sampler)
{
  int using_nearest = 0;
  uint32_t wrap_mode;
  gen7_sampler_state_t *sampler;

  sampler = (gen7_sampler_state_t *)(gpgpu->sampler_state_b.bo->virtual)  + index;
  memset(sampler, 0, sizeof(*sampler));
  sampler->ss2.default_color_pointer = (gpgpu->sampler_border_color_state_b.bo->offset) >> 5;
  if ((clk_sampler & __CLK_NORMALIZED_MASK) == CLK_NORMALIZED_COORDS_FALSE)
    sampler->ss3.non_normalized_coord = 1;
  else
    sampler->ss3.non_normalized_coord = 0;

  switch (clk_sampler & __CLK_FILTER_MASK) {
  case CLK_FILTER_NEAREST:
    sampler->ss0.min_filter = GEN_MAPFILTER_NEAREST;
    sampler->ss0.mip_filter = GEN_MIPFILTER_NONE;
    sampler->ss0.mag_filter = GEN_MAPFILTER_NEAREST;
    using_nearest = 1;
    break;
  case CLK_FILTER_LINEAR:
    sampler->ss0.min_filter = GEN_MAPFILTER_LINEAR;
    sampler->ss0.mip_filter = GEN_MIPFILTER_NONE;
    sampler->ss0.mag_filter = GEN_MAPFILTER_LINEAR;
    break;
  }

  wrap_mode = translate_wrap_mode(clk_sampler & __CLK_ADDRESS_MASK, using_nearest);
  sampler->ss3.s_wrap_mode = wrap_mode;
  /* XXX mesa i965 driver code point out that if the surface is a 1D surface, we may need
   * to set t_wrap_mode to GEN_TEXCOORDMODE_WRAP. */
  sampler->ss3.t_wrap_mode = wrap_mode;
  sampler->ss3.r_wrap_mode = wrap_mode;

  sampler->ss0.lod_preclamp = 1; /* OpenGL mode */
  sampler->ss0.default_color_mode = 0; /* OpenGL/DX10 mode */

  sampler->ss0.base_level = 0;

  sampler->ss1.max_lod = 0;
  sampler->ss1.min_lod = 0;

  if (sampler->ss0.min_filter != GEN_MAPFILTER_NEAREST)
     sampler->ss3.address_round |= GEN_ADDRESS_ROUNDING_ENABLE_U_MIN |
                                   GEN_ADDRESS_ROUNDING_ENABLE_V_MIN |
                                   GEN_ADDRESS_ROUNDING_ENABLE_R_MIN;
  if (sampler->ss0.mag_filter != GEN_MAPFILTER_NEAREST)
     sampler->ss3.address_round |= GEN_ADDRESS_ROUNDING_ENABLE_U_MAG |
                                   GEN_ADDRESS_ROUNDING_ENABLE_V_MAG |
                                   GEN_ADDRESS_ROUNDING_ENABLE_R_MAG;

  dri_bo_emit_reloc(gpgpu->sampler_state_b.bo,
                    I915_GEM_DOMAIN_SAMPLER, 0,
                    0,
                    index * sizeof(gen7_sampler_state_t) +
                    offsetof(gen7_sampler_state_t, ss2),
                    gpgpu->sampler_border_color_state_b.bo);

}

static void
intel_gpgpu_bind_sampler(intel_gpgpu_t *gpgpu, uint32_t *samplers, size_t sampler_sz)
{
  int index;
#ifdef GEN7_SAMPLER_CLAMP_BORDER_WORKAROUND
  //assert(sampler_sz <= GEN_MAX_SAMPLERS/2);
#else
  assert(sampler_sz <= GEN_MAX_SAMPLERS);
#endif
  for(index = 0; index < sampler_sz; index++) {
    intel_gpgpu_insert_sampler(gpgpu, index, samplers[index]);
#ifdef GEN7_SAMPLER_CLAMP_BORDER_WORKAROUND
    /* Duplicate the sampler to 8 + index and fixup the address mode
     * to repeat.*/
    if ((samplers[index] & __CLK_ADDRESS_MASK) == CLK_ADDRESS_CLAMP) {
      intel_gpgpu_insert_sampler(gpgpu, index + 8,
                                 (samplers[index] & ~__CLK_ADDRESS_MASK) | CLK_ADDRESS_CLAMP_TO_EDGE);
    }
#endif
  }
}

static void
intel_gpgpu_states_setup(intel_gpgpu_t *gpgpu, cl_gpgpu_kernel *kernel)
{
  gpgpu->ker = kernel;
  intel_gpgpu_build_idrt(gpgpu, kernel);
  intel_gpgpu_map_address_space(gpgpu);
  dri_bo_unmap(gpgpu->surface_heap_b.bo);
  dri_bo_unmap(gpgpu->sampler_state_b.bo);
  dri_bo_unmap(gpgpu->sampler_border_color_state_b.bo);
}

static void
intel_gpgpu_set_perf_counters(intel_gpgpu_t *gpgpu, cl_buffer *perf)
{
  if (gpgpu->perf_b.bo)
    drm_intel_bo_unreference(gpgpu->perf_b.bo);
  drm_intel_bo_reference((drm_intel_bo*) perf);
  gpgpu->perf_b.bo = (drm_intel_bo*) perf;
}

static void
intel_gpgpu_walker(intel_gpgpu_t *gpgpu,
                   uint32_t simd_sz,
                   uint32_t thread_n,
                   const size_t global_wk_off[3],
                   const size_t global_wk_sz[3],
                   const size_t local_wk_sz[3])
{
  const uint32_t global_wk_dim[3] = {
    global_wk_sz[0] / local_wk_sz[0],
    global_wk_sz[1] / local_wk_sz[1],
    global_wk_sz[2] / local_wk_sz[2]
  };
  uint32_t right_mask = ~0x0;
  size_t group_sz = local_wk_sz[0] * local_wk_sz[1] * local_wk_sz[2];

  assert(simd_sz == 8 || simd_sz == 16);

  uint32_t shift = (group_sz & (simd_sz - 1));
  shift = (shift == 0) ? simd_sz : shift;
  right_mask = (1 << shift) - 1;

  BEGIN_BATCH(gpgpu->batch, 11);
  OUT_BATCH(gpgpu->batch, CMD_GPGPU_WALKER | 9);
  OUT_BATCH(gpgpu->batch, 0);                        /* kernel index == 0 */
  if (simd_sz == 16)
    OUT_BATCH(gpgpu->batch, (1 << 30) | (thread_n-1)); /* SIMD16 | thread max */
  else
    OUT_BATCH(gpgpu->batch, (0 << 30) | (thread_n-1)); /* SIMD8  | thread max */
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, global_wk_dim[0]);
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, global_wk_dim[1]);
  OUT_BATCH(gpgpu->batch, 0);
  OUT_BATCH(gpgpu->batch, global_wk_dim[2]);
  OUT_BATCH(gpgpu->batch, right_mask);
  OUT_BATCH(gpgpu->batch, ~0x0);                     /* we always set height as 1, so set bottom mask as all 1*/
  ADVANCE_BATCH(gpgpu->batch);

  BEGIN_BATCH(gpgpu->batch, 2);
  OUT_BATCH(gpgpu->batch, CMD_MEDIA_STATE_FLUSH | 0);
  OUT_BATCH(gpgpu->batch, 0);                        /* kernel index == 0 */
  ADVANCE_BATCH(gpgpu->batch);
}

static intel_event_t*
intel_gpgpu_event_new(intel_gpgpu_t *gpgpu)
{
  intel_event_t *event = NULL;
  TRY_ALLOC_NO_ERR (event, CALLOC(intel_event_t));

  event->status = command_queued;
  event->batch = NULL;
  event->buffer = gpgpu->batch->buffer;
  if(event->buffer != NULL)
    drm_intel_bo_reference(event->buffer);

  if(gpgpu->time_stamp_b.bo) {
    event->ts_buf = gpgpu->time_stamp_b.bo;
    drm_intel_bo_reference(event->ts_buf);
  }

exit:
  return event;
error:
  cl_free(event);
  event = NULL;
  goto exit;
}

static int
intel_gpgpu_event_update_status(intel_event_t *event, int wait)
{
  if(event->status == command_complete)
    return event->status;

  if (event->buffer &&
      event->batch == NULL &&        //have flushed
      !drm_intel_bo_busy(event->buffer)) {
    event->status = command_complete;
    drm_intel_bo_unreference(event->buffer);
    event->buffer = NULL;
    return event->status;
  }

  if(wait == 0)
    return event->status;

  if (event->buffer) {
    drm_intel_bo_wait_rendering(event->buffer);
    event->status = command_complete;
    drm_intel_bo_unreference(event->buffer);
    event->buffer = NULL;
  }
  return event->status;
}

static void
intel_gpgpu_event_pending(intel_gpgpu_t *gpgpu, intel_event_t *event)
{
  assert(event->buffer);           //This is gpu enqueue command
  assert(event->batch == NULL);    //This command haven't pengding.
  event->batch = intel_batchbuffer_new(gpgpu->drv);
  assert(event->batch);
  *event->batch = *gpgpu->batch;
  if(event->batch->buffer)
    drm_intel_bo_reference(event->batch->buffer);
}

static void
intel_gpgpu_event_resume(intel_event_t *event)
{
  assert(event->batch);           //This command have pending.
  intel_batchbuffer_flush(event->batch);
  intel_batchbuffer_delete(event->batch);
  event->batch = NULL;
}

static void
intel_gpgpu_event_delete(intel_event_t *event)
{
  assert(event->batch == NULL);   //This command must have been flushed.
  if(event->buffer)
    drm_intel_bo_unreference(event->buffer);
  if(event->ts_buf)
    drm_intel_bo_unreference(event->ts_buf);
  cl_free(event);
}

/* We want to get the current time of GPU. */
static void
intel_gpgpu_event_get_gpu_cur_timestamp(intel_gpgpu_t* gpgpu, uint64_t* ret_ts)
{
  uint64_t result = 0;
  drm_intel_bufmgr *bufmgr = gpgpu->drv->bufmgr;

  drm_intel_reg_read(bufmgr, TIMESTAMP_ADDR, &result);
  result = result & 0xFFFFFFFFF0000000;
  result = result >> 28;
  result *= 80;

  *ret_ts = result;
  return;
}

/* Get the GPU execute time. */
static void
intel_gpgpu_event_get_exec_timestamp(intel_event_t *event,
                                int index, uint64_t* ret_ts)
{
  uint64_t result = 0;

  assert(event->ts_buf != NULL);
  assert(index == 0 || index == 1);
  drm_intel_gem_bo_map_gtt(event->ts_buf);
  uint64_t* ptr = event->ts_buf->virtual;
  result = ptr[index];

  /* According to BSpec, the timestamp counter should be 36 bits,
     but comparing to the timestamp counter from IO control reading,
     we find the first 4 bits seems to be fake. In order to keep the
     timestamp counter conformable, we just skip the first 4 bits. */
  result = ((result & 0x0FFFFFFFF) << 4) * 80; //convert to nanoseconds
  *ret_ts = result;

  drm_intel_gem_bo_unmap_gtt(event->ts_buf);
}

LOCAL void
intel_set_gpgpu_callbacks(void)
{
  cl_gpgpu_new = (cl_gpgpu_new_cb *) intel_gpgpu_new;
  cl_gpgpu_delete = (cl_gpgpu_delete_cb *) intel_gpgpu_delete;
  cl_gpgpu_sync = (cl_gpgpu_sync_cb *) intel_gpgpu_sync;
  cl_gpgpu_bind_image = (cl_gpgpu_bind_image_cb *) intel_gpgpu_bind_image;
  cl_gpgpu_bind_buf = (cl_gpgpu_bind_buf_cb *) intel_gpgpu_bind_buf;
  cl_gpgpu_set_stack = (cl_gpgpu_set_stack_cb *) intel_gpgpu_set_stack;
  cl_gpgpu_state_init = (cl_gpgpu_state_init_cb *) intel_gpgpu_state_init;
  cl_gpgpu_set_perf_counters = (cl_gpgpu_set_perf_counters_cb *) intel_gpgpu_set_perf_counters;
  cl_gpgpu_upload_curbes = (cl_gpgpu_upload_curbes_cb *) intel_gpgpu_upload_curbes;
  cl_gpgpu_alloc_constant_buffer  = (cl_gpgpu_alloc_constant_buffer_cb *) intel_gpgpu_alloc_constant_buffer;
  cl_gpgpu_states_setup = (cl_gpgpu_states_setup_cb *) intel_gpgpu_states_setup;
  cl_gpgpu_upload_samplers = (cl_gpgpu_upload_samplers_cb *) intel_gpgpu_upload_samplers;
  cl_gpgpu_batch_reset = (cl_gpgpu_batch_reset_cb *) intel_gpgpu_batch_reset;
  cl_gpgpu_batch_start = (cl_gpgpu_batch_start_cb *) intel_gpgpu_batch_start;
  cl_gpgpu_batch_end = (cl_gpgpu_batch_end_cb *) intel_gpgpu_batch_end;
  cl_gpgpu_flush = (cl_gpgpu_flush_cb *) intel_gpgpu_flush;
  cl_gpgpu_walker = (cl_gpgpu_walker_cb *) intel_gpgpu_walker;
  cl_gpgpu_bind_sampler = (cl_gpgpu_bind_sampler_cb *) intel_gpgpu_bind_sampler;
  cl_gpgpu_set_scratch = (cl_gpgpu_set_scratch_cb *) intel_gpgpu_set_scratch;
  cl_gpgpu_event_new = (cl_gpgpu_event_new_cb *)intel_gpgpu_event_new;
  cl_gpgpu_event_update_status = (cl_gpgpu_event_update_status_cb *)intel_gpgpu_event_update_status;
  cl_gpgpu_event_pending = (cl_gpgpu_event_pending_cb *)intel_gpgpu_event_pending;
  cl_gpgpu_event_resume = (cl_gpgpu_event_resume_cb *)intel_gpgpu_event_resume;
  cl_gpgpu_event_delete = (cl_gpgpu_event_delete_cb *)intel_gpgpu_event_delete;
  cl_gpgpu_event_get_exec_timestamp = (cl_gpgpu_event_get_exec_timestamp_cb *)intel_gpgpu_event_get_exec_timestamp;
  cl_gpgpu_event_get_gpu_cur_timestamp = (cl_gpgpu_event_get_gpu_cur_timestamp_cb *)intel_gpgpu_event_get_gpu_cur_timestamp;
  cl_gpgpu_ref_batch_buf = (cl_gpgpu_ref_batch_buf_cb *)intel_gpgpu_ref_batch_buf;
  cl_gpgpu_unref_batch_buf = (cl_gpgpu_unref_batch_buf_cb *)intel_gpgpu_unref_batch_buf;
}

