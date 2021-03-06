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

#include "cl_mem.h"
#include "cl_image.h"
#include "cl_context.h"
#include "cl_utils.h"
#include "cl_alloc.h"
#include "cl_device_id.h"
#include "cl_driver.h"
#include "cl_khr_icd.h"
#include "cl_kernel.h"
#include "cl_command_queue.h"

#include "CL/cl.h"
#include "CL/cl_intel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FIELD_SIZE(CASE,TYPE)               \
  case JOIN(CL_,CASE):                      \
    if(param_value_size_ret)                \
      *param_value_size_ret = sizeof(TYPE); \
    if(!param_value)                        \
      return CL_SUCCESS;                    \
    if(param_value_size < sizeof(TYPE))     \
      return CL_INVALID_VALUE;              \
    break;

#define CL_MEM_OBJECT_BUFFER                        0x10F0
#define CL_MEM_OBJECT_IMAGE2D                       0x10F1
#define CL_MEM_OBJECT_IMAGE3D                       0x10F2

static cl_mem_object_type
cl_get_mem_object_type(cl_mem mem)
{
  switch (mem->type) {
    case CL_MEM_BUFFER_TYPE:
    case CL_MEM_SUBBUFFER_TYPE:
      return CL_MEM_OBJECT_BUFFER;
    case CL_MEM_IMAGE_TYPE:
    case CL_MEM_GL_IMAGE_TYPE:
    {
      struct _cl_mem_image *image = cl_mem_image(mem);
      return image->image_type;
    }
    default:
      return CL_MEM_OBJECT_BUFFER;
  }
}

LOCAL cl_int
cl_get_mem_object_info(cl_mem mem,
                cl_mem_info param_name,
                size_t param_value_size,
                void *param_value,
                size_t *param_value_size_ret)
{
  switch(param_name)
  {
    FIELD_SIZE(MEM_TYPE, cl_mem_object_type);
    FIELD_SIZE(MEM_FLAGS, cl_mem_flags);
    FIELD_SIZE(MEM_SIZE, size_t);
    FIELD_SIZE(MEM_HOST_PTR, void *);
    FIELD_SIZE(MEM_MAP_COUNT, cl_uint);
    FIELD_SIZE(MEM_REFERENCE_COUNT, cl_uint);
    FIELD_SIZE(MEM_CONTEXT, cl_context);
    FIELD_SIZE(MEM_ASSOCIATED_MEMOBJECT, cl_mem);
    FIELD_SIZE(MEM_OFFSET, size_t);
  default:
    return CL_INVALID_VALUE;
  }

  switch(param_name)
  {
  case CL_MEM_TYPE:
    *((cl_mem_object_type *)param_value) = cl_get_mem_object_type(mem);
    break;
  case CL_MEM_FLAGS:
    *((cl_mem_flags *)param_value) = mem->flags;
    break;
  case CL_MEM_SIZE:
    *((size_t *)param_value) = mem->size;
    break;
  case CL_MEM_HOST_PTR:
    *((size_t *)param_value) = (size_t)mem->host_ptr;
    break;
  case CL_MEM_MAP_COUNT:
    *((cl_uint *)param_value) = mem->map_ref;
    break;
  case CL_MEM_REFERENCE_COUNT:
    *((cl_uint *)param_value) = mem->ref_n;
    break;
  case CL_MEM_CONTEXT:
    *((cl_context *)param_value) = mem->ctx;
    break;
  case CL_MEM_ASSOCIATED_MEMOBJECT:
    if(mem->type != CL_MEM_SUBBUFFER_TYPE) {
      *((cl_mem *)param_value) = NULL;
    } else {
      struct _cl_mem_buffer* buf = (struct _cl_mem_buffer*)mem;
      *((cl_mem *)param_value) = (cl_mem)(buf->parent);
    }
    break;
  case CL_MEM_OFFSET:
    if(mem->type != CL_MEM_SUBBUFFER_TYPE) {
      *((size_t *)param_value) = 0;
    } else {
      struct _cl_mem_buffer* buf = (struct _cl_mem_buffer*)mem;
      *((size_t *)param_value) = buf->sub_offset;
    }
    break;
  }

  return CL_SUCCESS;
}

LOCAL cl_int
cl_get_image_info(cl_mem mem,
                  cl_image_info param_name,
                  size_t param_value_size,
                  void *param_value,
                  size_t *param_value_size_ret)
{
  int err;
  CHECK_IMAGE(mem, image);

  switch(param_name)
  {
    FIELD_SIZE(IMAGE_FORMAT, cl_image_format);
    FIELD_SIZE(IMAGE_ELEMENT_SIZE, size_t);
    FIELD_SIZE(IMAGE_ROW_PITCH, size_t);
    FIELD_SIZE(IMAGE_SLICE_PITCH, size_t);
    FIELD_SIZE(IMAGE_WIDTH, size_t);
    FIELD_SIZE(IMAGE_HEIGHT, size_t);
    FIELD_SIZE(IMAGE_DEPTH, size_t);
  default:
    return CL_INVALID_VALUE;
  }

  switch(param_name)
  {
  case CL_IMAGE_FORMAT:
    *(cl_image_format *)param_value = image->fmt;
    break;
  case CL_IMAGE_ELEMENT_SIZE:
    *(size_t *)param_value = image->bpp;
    break;
  case CL_IMAGE_ROW_PITCH:
    *(size_t *)param_value = image->row_pitch;
    break;
  case CL_IMAGE_SLICE_PITCH:
    *(size_t *)param_value = image->slice_pitch;
    break;
  case CL_IMAGE_WIDTH:
    *(size_t *)param_value = image->w;
    break;
  case CL_IMAGE_HEIGHT:
    *(size_t *)param_value = image->h;
    break;
  case CL_IMAGE_DEPTH:
    *(size_t *)param_value = image->depth;
    break;
  }

  return CL_SUCCESS;

error:
    return err;
}

#undef FIELD_SIZE

LOCAL cl_mem
cl_mem_allocate(enum cl_mem_type type,
                cl_context ctx,
                cl_mem_flags flags,
                size_t sz,
                cl_int is_tiled,
                cl_int *errcode)
{
  cl_buffer_mgr bufmgr = NULL;
  cl_mem mem = NULL;
  cl_int err = CL_SUCCESS;
  size_t alignment = 64;

  assert(ctx);

  /* Allocate and inialize the structure itself */
  if (type == CL_MEM_IMAGE_TYPE) {
    struct _cl_mem_image *image = NULL;
    TRY_ALLOC (image, CALLOC(struct _cl_mem_image));
    mem = &image->base;
  } else if (type == CL_MEM_GL_IMAGE_TYPE ) {
    struct _cl_mem_gl_image *gl_image = NULL;
    TRY_ALLOC (gl_image, CALLOC(struct _cl_mem_gl_image));
    mem = &gl_image->base.base;
  } else {
    struct _cl_mem_buffer *buffer = NULL;
    TRY_ALLOC (buffer, CALLOC(struct _cl_mem_buffer));
    mem = &buffer->base;
  }
  mem->type = type;
  SET_ICD(mem->dispatch)
  mem->ref_n = 1;
  mem->magic = CL_MAGIC_MEM_HEADER;
  mem->flags = flags;

  if (sz != 0) {
    /* Pinning will require stricter alignment rules */
    if ((flags & CL_MEM_PINNABLE) || is_tiled)
      alignment = 4096;

    /* Allocate space in memory */
    bufmgr = cl_context_get_bufmgr(ctx);
    assert(bufmgr);
    mem->bo = cl_buffer_alloc(bufmgr, "CL memory object", sz, alignment);
    if (UNLIKELY(mem->bo == NULL)) {
      err = CL_MEM_OBJECT_ALLOCATION_FAILURE;
      goto error;
    }
    mem->size = sz;
  }

  cl_context_add_ref(ctx);
  mem->ctx = ctx;
    /* Append the buffer in the context buffer list */
  pthread_mutex_lock(&ctx->buffer_lock);
  mem->next = ctx->buffers;
  if (ctx->buffers != NULL)
    ctx->buffers->prev = mem;
  ctx->buffers = mem;
  pthread_mutex_unlock(&ctx->buffer_lock);

exit:
  if (errcode)
    *errcode = err;
  return mem;
error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;

}

LOCAL cl_mem
cl_mem_new_buffer(cl_context ctx,
                  cl_mem_flags flags,
                  size_t sz,
                  void *data,
                  cl_int *errcode_ret)
{
  /* Possible mem type combination:
       CL_MEM_ALLOC_HOST_PTR
       CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR
       CL_MEM_USE_HOST_PTR
       CL_MEM_COPY_HOST_PTR   */

  cl_int err = CL_SUCCESS;
  cl_mem mem = NULL;
  cl_ulong max_mem_size;

  if (UNLIKELY(sz == 0)) {
    err = CL_INVALID_BUFFER_SIZE;
    goto error;
  }

  if (UNLIKELY(((flags & CL_MEM_READ_WRITE)
                  && (flags & (CL_MEM_READ_ONLY | CL_MEM_WRITE_ONLY)))
		      || ((flags & CL_MEM_READ_ONLY) && (flags & (CL_MEM_WRITE_ONLY)))
              || ((flags & CL_MEM_ALLOC_HOST_PTR) && (flags & CL_MEM_USE_HOST_PTR))
              || ((flags & CL_MEM_COPY_HOST_PTR) && (flags & CL_MEM_USE_HOST_PTR))
              || ((flags & (~(CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY
                        | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR
                        | CL_MEM_USE_HOST_PTR))) != 0))) {
    err = CL_INVALID_VALUE;
    goto error;
  }

  /* This flag is valid only if host_ptr is not NULL */
  if (UNLIKELY((((flags & CL_MEM_COPY_HOST_PTR) ||
                (flags & CL_MEM_USE_HOST_PTR)) &&
                data == NULL))
               || (!(flags & (CL_MEM_COPY_HOST_PTR
                            |CL_MEM_USE_HOST_PTR))
                    && (data != NULL))) {
    err = CL_INVALID_HOST_PTR;
    goto error;
  }

  /* CL_MEM_ALLOC_HOST_PTR and CL_MEM_USE_HOST_PTR
     are mutually exclusive. */
  if (UNLIKELY(flags & CL_MEM_ALLOC_HOST_PTR &&
               flags & CL_MEM_USE_HOST_PTR)) {
    err = CL_INVALID_HOST_PTR;
    goto error;
  }

  /* CL_MEM_COPY_HOST_PTR and CL_MEM_USE_HOST_PTR
     are mutually exclusive. */
  if (UNLIKELY(flags & CL_MEM_COPY_HOST_PTR &&
               flags & CL_MEM_USE_HOST_PTR)) {
    err = CL_INVALID_HOST_PTR;
    goto error;
  }

  if ((err = cl_get_device_info(ctx->device,
                                CL_DEVICE_MAX_MEM_ALLOC_SIZE,
                                sizeof(max_mem_size),
                                &max_mem_size,
                                NULL)) != CL_SUCCESS) {
    goto error;
  }

  if (UNLIKELY(sz > max_mem_size)) {
    err = CL_INVALID_BUFFER_SIZE;
    goto error;
  }

  /* Create the buffer in video memory */
  mem = cl_mem_allocate(CL_MEM_BUFFER_TYPE, ctx, flags, sz, CL_FALSE, &err);
  if (mem == NULL || err != CL_SUCCESS)
    goto error;

  /* Copy the data if required */
  if (flags & CL_MEM_COPY_HOST_PTR || flags & CL_MEM_USE_HOST_PTR)
    cl_buffer_subdata(mem->bo, 0, sz, data);

  if (flags & CL_MEM_USE_HOST_PTR || flags & CL_MEM_COPY_HOST_PTR)
    mem->host_ptr = data;

exit:
  if (errcode_ret)
    *errcode_ret = err;
  return mem;
error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;
}

LOCAL cl_mem
cl_mem_new_sub_buffer(cl_mem buffer,
                      cl_mem_flags flags,
                      cl_buffer_create_type create_type,
                      const void *create_info,
                      cl_int *errcode_ret)
{
  cl_int err = CL_SUCCESS;
  cl_mem mem = NULL;
  struct _cl_mem_buffer *sub_buf = NULL;

  if (buffer->type != CL_MEM_BUFFER_TYPE) {
    err = CL_INVALID_MEM_OBJECT;
    goto error;
  }

  if (flags && (((buffer->flags & CL_MEM_WRITE_ONLY) && (flags & (CL_MEM_READ_WRITE|CL_MEM_READ_ONLY)))
          || ((buffer->flags & CL_MEM_READ_ONLY) && (flags & (CL_MEM_READ_WRITE|CL_MEM_WRITE_ONLY)))
          || (flags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR)))) {
    err = CL_INVALID_VALUE;
    goto error;
  }

  if (create_type != CL_BUFFER_CREATE_TYPE_REGION) {
    err = CL_INVALID_VALUE;
    goto error;
  }

  if (!create_info) {
    err = CL_INVALID_VALUE;
    goto error;
  }

  cl_buffer_region *info = (cl_buffer_region *)create_info;

  if (!info->size) {
    err = CL_INVALID_BUFFER_SIZE;
    goto error;
  }

  if (info->origin > buffer->size || info->origin + info->size > buffer->size) {
    err = CL_INVALID_VALUE;
    goto error;
  }

  if (info->origin & (buffer->ctx->device->mem_base_addr_align - 1)) {
    err = CL_MISALIGNED_SUB_BUFFER_OFFSET;
    goto error;
  }

  /* Now create the sub buffer and link it to the buffer. */
  TRY_ALLOC (sub_buf, CALLOC(struct _cl_mem_buffer));
  mem = &sub_buf->base;
  mem->type = CL_MEM_SUBBUFFER_TYPE;
  SET_ICD(mem->dispatch)
  mem->ref_n = 1;
  mem->magic = CL_MAGIC_MEM_HEADER;
  mem->flags = flags;
  sub_buf->parent = (struct _cl_mem_buffer*)buffer;

  cl_mem_add_ref(buffer);
  /* Append the buffer in the parent buffer list */
  pthread_mutex_lock(&((struct _cl_mem_buffer*)buffer)->sub_lock);
  sub_buf->sub_next = ((struct _cl_mem_buffer*)buffer)->subs;
  if (((struct _cl_mem_buffer*)buffer)->subs != NULL)
    ((struct _cl_mem_buffer*)buffer)->subs->sub_prev = sub_buf;
  ((struct _cl_mem_buffer*)buffer)->subs = sub_buf;
  pthread_mutex_unlock(&((struct _cl_mem_buffer*)buffer)->sub_lock);

  mem->bo = buffer->bo;
  mem->size = info->size;
  sub_buf->sub_offset = info->origin;
  if (buffer->flags & CL_MEM_USE_HOST_PTR || buffer->flags & CL_MEM_COPY_HOST_PTR) {
    mem->host_ptr = buffer->host_ptr;
  }

  cl_context_add_ref(buffer->ctx);
  mem->ctx = buffer->ctx;
  /* Append the buffer in the context buffer list */
  pthread_mutex_lock(&buffer->ctx->buffer_lock);
  mem->next = buffer->ctx->buffers;
  if (buffer->ctx->buffers != NULL)
    buffer->ctx->buffers->prev = mem;
  buffer->ctx->buffers = mem;
  pthread_mutex_unlock(&buffer->ctx->buffer_lock);

exit:
  if (errcode_ret)
    *errcode_ret = err;
  return mem;
error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;
}

void
cl_mem_copy_image_region(const size_t *origin, const size_t *region,
                         void *dst, size_t dst_row_pitch, size_t dst_slice_pitch,
                         const void *src, size_t src_row_pitch, size_t src_slice_pitch,
                         const struct _cl_mem_image *image)
{
  size_t offset = image->bpp * origin[0] + dst_row_pitch * origin[1] + dst_slice_pitch * origin[2];
  dst = (char*)dst + offset;
  if (!origin[0] && region[0] == image->w && dst_row_pitch == src_row_pitch &&
      (region[2] == 1 || (!origin[1] && region[1] == image->h && dst_slice_pitch == src_slice_pitch)))
  {
    memcpy(dst, src, region[2] == 1 ? src_row_pitch*region[1] : src_slice_pitch*region[2]);
  }
  else {
    cl_uint y, z;
    for (z = 0; z < region[2]; z++) {
      const char* src_ptr = src;
      char* dst_ptr = dst;
      for (y = 0; y < region[1]; y++) {
        memcpy(dst_ptr, src_ptr, image->bpp*region[0]);
        src_ptr += src_row_pitch;
        dst_ptr += dst_row_pitch;
      }
      src = (char*)src + src_slice_pitch;
      dst = (char*)dst + dst_slice_pitch;
    }
  }
}

static void
cl_mem_copy_image(struct _cl_mem_image *image,
		  size_t row_pitch,
		  size_t slice_pitch,
		  void* host_ptr)
{
  char* dst_ptr = cl_mem_map_auto((cl_mem)image);
  size_t origin[3] = {0, 0, 0};
  size_t region[3] = {image->w, image->h, image->depth};

  cl_mem_copy_image_region(origin, region, dst_ptr, image->row_pitch, image->slice_pitch,
                           host_ptr, row_pitch, slice_pitch, image);
  cl_mem_unmap_auto((cl_mem)image);
}

static const uint32_t tile_sz = 4096; /* 4KB per tile */
static const uint32_t tilex_w = 512;  /* tileX width in bytes */
static const uint32_t tilex_h = 8;    /* tileX height in number of rows */
static const uint32_t tiley_w = 128;  /* tileY width in bytes */
static const uint32_t tiley_h = 32;   /* tileY height in number of rows */

static cl_mem
_cl_mem_new_image(cl_context ctx,
                  cl_mem_flags flags,
                  const cl_image_format *fmt,
                  const cl_mem_object_type image_type,
                  size_t w,
                  size_t h,
                  size_t depth,
                  size_t pitch,
                  size_t slice_pitch,
                  void *data,
                  cl_int *errcode_ret)
{
  cl_int err = CL_SUCCESS;
  cl_mem mem = NULL;
  uint32_t bpp = 0, intel_fmt = INTEL_UNSUPPORTED_FORMAT;
  size_t sz = 0, aligned_pitch = 0, aligned_slice_pitch = 0, aligned_h;
  cl_image_tiling_t tiling = CL_NO_TILE;

  /* Check flags consistency */
  if (UNLIKELY((flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)) && data == NULL)) {
    err = CL_INVALID_HOST_PTR;
    goto error;
  }

  /* Get the size of each pixel */
  if (UNLIKELY((err = cl_image_byte_per_pixel(fmt, &bpp)) != CL_SUCCESS))
    goto error;

  /* Only a sub-set of the formats are supported */
  intel_fmt = cl_image_get_intel_format(fmt);
  if (UNLIKELY(intel_fmt == INTEL_UNSUPPORTED_FORMAT)) {
    err = CL_INVALID_IMAGE_FORMAT_DESCRIPTOR;
    goto error;
  }

  /* See if the user parameters match */
#define DO_IMAGE_ERROR            \
  do {                            \
    err = CL_INVALID_IMAGE_SIZE;  \
    goto error;                   \
  } while (0);
  if (UNLIKELY(w == 0)) DO_IMAGE_ERROR;
  if (UNLIKELY(h == 0)) DO_IMAGE_ERROR;

  if (image_type == CL_MEM_OBJECT_IMAGE2D) {
    size_t min_pitch = bpp * w;
    if (data && pitch == 0)
      pitch = min_pitch;
    if (UNLIKELY(w > ctx->device->image2d_max_width)) DO_IMAGE_ERROR;
    if (UNLIKELY(h > ctx->device->image2d_max_height)) DO_IMAGE_ERROR;
    if (UNLIKELY(data && min_pitch > pitch)) DO_IMAGE_ERROR;
    if (UNLIKELY(!data && pitch != 0)) DO_IMAGE_ERROR;

    /* Pick up tiling mode (we do only linear on SNB) */
    if (cl_driver_get_ver(ctx->drv) != 6)
      tiling = CL_TILE_Y;
    depth = 1;
  }

  if (image_type == CL_MEM_OBJECT_IMAGE3D) {
    size_t min_pitch = bpp * w;
    if (data && pitch == 0)
      pitch = min_pitch;
    size_t min_slice_pitch = min_pitch * h;
    if (data && slice_pitch == 0)
      slice_pitch = min_slice_pitch;
    if (UNLIKELY(w > ctx->device->image3d_max_width)) DO_IMAGE_ERROR;
    if (UNLIKELY(h > ctx->device->image3d_max_height)) DO_IMAGE_ERROR;
    if (UNLIKELY(depth > ctx->device->image3d_max_depth)) DO_IMAGE_ERROR;
    if (UNLIKELY(data && min_pitch > pitch)) DO_IMAGE_ERROR;
    if (UNLIKELY(data && min_slice_pitch > slice_pitch)) DO_IMAGE_ERROR;
    if (UNLIKELY(!data && pitch != 0)) DO_IMAGE_ERROR;
    if (UNLIKELY(!data && slice_pitch != 0)) DO_IMAGE_ERROR;

    /* Pick up tiling mode (we do only linear on SNB) */
    if (cl_driver_get_ver(ctx->drv) != 6)
      tiling = CL_TILE_Y;
  }
#undef DO_IMAGE_ERROR

  /* Tiling requires to align both pitch and height */
  if (tiling == CL_NO_TILE) {
    aligned_pitch = w * bpp;
    aligned_h     = h;
  } else if (tiling == CL_TILE_X) {
    aligned_pitch = ALIGN(w * bpp, tilex_w);
    aligned_h     = ALIGN(h, tilex_h);
  } else if (tiling == CL_TILE_Y) {
    aligned_pitch = ALIGN(w * bpp, tiley_w);
    aligned_h     = ALIGN(h, tiley_h);
  }

  sz = aligned_pitch * aligned_h * depth;

  mem = cl_mem_allocate(CL_MEM_IMAGE_TYPE, ctx, flags, sz, tiling != CL_NO_TILE, &err);
  if (mem == NULL || err != CL_SUCCESS)
    goto error;

  cl_buffer_set_tiling(mem->bo, tiling, aligned_pitch);
  aligned_slice_pitch = (image_type == CL_MEM_OBJECT_IMAGE1D
                         || image_type == CL_MEM_OBJECT_IMAGE2D) ? 0 : aligned_pitch * ALIGN(h, 2);

  cl_mem_image_init(cl_mem_image(mem), w, h, image_type, depth, *fmt,
                    intel_fmt, bpp, aligned_pitch, aligned_slice_pitch, tiling,
                    0, 0, 0);

  /* Copy the data if required */
  if (flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_USE_HOST_PTR)) {
    cl_mem_copy_image(cl_mem_image(mem), pitch, slice_pitch, data);
    if (flags & CL_MEM_USE_HOST_PTR) {
      mem->host_ptr = data;
      cl_mem_image(mem)->host_row_pitch = pitch;
      cl_mem_image(mem)->host_slice_pitch = slice_pitch;
    }
  }

exit:
  if (errcode_ret)
    *errcode_ret = err;
  return mem;
error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;
}

LOCAL cl_mem
cl_mem_new_image(cl_context context,
                 cl_mem_flags flags,
                 const cl_image_format *image_format,
                 const cl_image_desc *image_desc,
                 void *host_ptr,
                 cl_int *errcode_ret)
{
  switch (image_desc->image_type) {
  case CL_MEM_OBJECT_IMAGE1D:
  case CL_MEM_OBJECT_IMAGE2D:
  case CL_MEM_OBJECT_IMAGE3D:
    return _cl_mem_new_image(context, flags, image_format, image_desc->image_type,
                             image_desc->image_width, image_desc->image_height, image_desc->image_depth,
                             image_desc->image_row_pitch, image_desc->image_slice_pitch,
                             host_ptr, errcode_ret);
  case CL_MEM_OBJECT_IMAGE2D_ARRAY:
  case CL_MEM_OBJECT_IMAGE1D_ARRAY:
  case CL_MEM_OBJECT_IMAGE1D_BUFFER:
    NOT_IMPLEMENTED;
    break;
  case CL_MEM_OBJECT_BUFFER:
  default:
    assert(0);
  }
  return NULL;
}

LOCAL void
cl_mem_delete(cl_mem mem)
{
  cl_int i;
  if (UNLIKELY(mem == NULL))
    return;
  if (atomic_dec(&mem->ref_n) > 1)
    return;
#ifdef HAS_EGL
  if (UNLIKELY(IS_GL_IMAGE(mem))) {
     cl_mem_gl_delete(cl_mem_gl_image(mem));
  }
#endif

  /* Remove it from the list */
  assert(mem->ctx);
  pthread_mutex_lock(&mem->ctx->buffer_lock);
    if (mem->prev)
      mem->prev->next = mem->next;
    if (mem->next)
      mem->next->prev = mem->prev;
    if (mem->ctx->buffers == mem)
      mem->ctx->buffers = mem->next;
  pthread_mutex_unlock(&mem->ctx->buffer_lock);
  cl_context_delete(mem->ctx);

  /* Someone still mapped, unmap */
  if(mem->map_ref > 0) {
    assert(mem->mapped_ptr);
    for(i=0; i<mem->mapped_ptr_sz; i++) {
      if(mem->mapped_ptr[i].ptr != NULL) {
        mem->map_ref--;
        cl_mem_unmap_gtt(mem);
      }
    }
    assert(mem->map_ref == 0);
  }

  if (mem->mapped_ptr)
    free(mem->mapped_ptr);

  if (mem->dstr_cb) {
    cl_mem_dstr_cb *cb = mem->dstr_cb;
    while (mem->dstr_cb) {
      cb = mem->dstr_cb;
      cb->pfn_notify(mem, cb->user_data);
      mem->dstr_cb = cb->next;
      free(cb);
    }
  }

  /* Iff we are sub, do nothing for bo release. */
  if (mem->type == CL_MEM_SUBBUFFER_TYPE) {
    struct _cl_mem_buffer* buffer = (struct _cl_mem_buffer*)mem;
    /* Remove it from the parent's list */
    assert(buffer->parent);
    pthread_mutex_lock(&buffer->parent->sub_lock);
    if (buffer->sub_prev)
      buffer->sub_prev->sub_next = buffer->sub_next;
    if (buffer->sub_next)
      buffer->sub_next->sub_prev = buffer->sub_prev;
    if (buffer->parent->subs == buffer)
      buffer->parent->subs = buffer->sub_next;
    pthread_mutex_unlock(&buffer->parent->sub_lock);
    cl_mem_delete((cl_mem )(buffer->parent));
  } else if (LIKELY(mem->bo != NULL)) {
    cl_buffer_unreference(mem->bo);
  }

  cl_free(mem);
}

LOCAL void
cl_mem_add_ref(cl_mem mem)
{
  assert(mem);
  atomic_inc(&mem->ref_n);
}

#define LOCAL_SZ_0   16
#define LOCAL_SZ_1   4
#define LOCAL_SZ_2   4

LOCAL cl_int
cl_mem_copy(cl_command_queue queue, cl_mem src_buf, cl_mem dst_buf,
            size_t src_offset, size_t dst_offset, size_t cb)
{
  cl_int ret;
  cl_kernel ker;
  size_t global_off[] = {0,0,0};
  size_t global_sz[] = {1,1,1};
  size_t local_sz[] = {1,1,1};

  /* We use one kernel to copy the data. The kernel is lazily created. */
  assert(src_buf->ctx == dst_buf->ctx);

  if ((cb % 4) || (src_offset % 4) || (dst_offset % 4)) {
    extern char cl_internal_copy_buf_align1_str[];
    extern int cl_internal_copy_buf_align1_str_size;

    ker = cl_context_get_static_kernel_form_bin(queue->ctx, CL_ENQUEUE_COPY_BUFFER_ALIGN1,
             cl_internal_copy_buf_align1_str, (size_t)cl_internal_copy_buf_align1_str_size, NULL);
  } else if ((cb % 16) || (src_offset % 16) || (dst_offset % 16)) {
    extern char cl_internal_copy_buf_align4_str[];
    extern int cl_internal_copy_buf_align4_str_size;

    ker = cl_context_get_static_kernel_form_bin(queue->ctx, CL_ENQUEUE_COPY_BUFFER_ALIGN4,
             cl_internal_copy_buf_align4_str, (size_t)cl_internal_copy_buf_align4_str_size, NULL);
    cb = cb/4;
    src_offset = src_offset/4;
    dst_offset = dst_offset/4;
  } else {
    extern char cl_internal_copy_buf_align16_str[];
    extern int cl_internal_copy_buf_align16_str_size;

    ker = cl_context_get_static_kernel_form_bin(queue->ctx, CL_ENQUEUE_COPY_BUFFER_ALIGN16,
             cl_internal_copy_buf_align16_str, (size_t)cl_internal_copy_buf_align16_str_size, NULL);
    cb = cb/16;
    src_offset = src_offset/4;
    dst_offset = dst_offset/4;
  }

  if (!ker)
    return CL_OUT_OF_RESOURCES;

  if (cb < LOCAL_SZ_0) {
    local_sz[0] = 1;
  } else {
    local_sz[0] = LOCAL_SZ_0;
  }
  global_sz[0] = ((cb + LOCAL_SZ_0 - 1)/LOCAL_SZ_0)*LOCAL_SZ_0;

  cl_kernel_set_arg(ker, 0, sizeof(cl_mem), &src_buf);
  cl_kernel_set_arg(ker, 1, sizeof(int), &src_offset);
  cl_kernel_set_arg(ker, 2, sizeof(cl_mem), &dst_buf);
  cl_kernel_set_arg(ker, 3, sizeof(int), &dst_offset);
  cl_kernel_set_arg(ker, 4, sizeof(int), &cb);

  ret = cl_command_queue_ND_range(queue, ker, 1, global_off, global_sz, local_sz);

  return ret;
}

LOCAL cl_int
cl_mem_copy_buffer_rect(cl_command_queue queue, cl_mem src_buf, cl_mem dst_buf,
                       const size_t *src_origin, const size_t *dst_origin, const size_t *region,
                       size_t src_row_pitch, size_t src_slice_pitch,
                       size_t dst_row_pitch, size_t dst_slice_pitch) {
  cl_int ret;
  cl_kernel ker;
  size_t global_off[] = {0,0,0};
  size_t global_sz[] = {1,1,1};
  size_t local_sz[] = {LOCAL_SZ_0,LOCAL_SZ_1,LOCAL_SZ_1};
  if(region[1] == 1) local_sz[1] = 1;
  if(region[2] == 1) local_sz[2] = 1;
  global_sz[0] = ((region[0] + local_sz[0] - 1) / local_sz[0]) * local_sz[0];
  global_sz[1] = ((region[1] + local_sz[1] - 1) / local_sz[1]) * local_sz[1];
  global_sz[2] = ((region[2] + local_sz[2] - 1) / local_sz[2]) * local_sz[2];
  cl_int index = CL_ENQUEUE_COPY_BUFFER_RECT;
  cl_int src_offset = src_origin[2]*src_slice_pitch + src_origin[1]*src_row_pitch + src_origin[0];
  cl_int dst_offset = dst_origin[2]*dst_slice_pitch + dst_origin[1]*dst_row_pitch + dst_origin[0];

  static const char *str_kernel =
      "kernel void __cl_cpy_buffer_rect ( \n"
      "       global char* src, global char* dst, \n"
      "       unsigned int region0, unsigned int region1, unsigned int region2, \n"
      "       unsigned int src_offset, unsigned int dst_offset, \n"
      "       unsigned int src_row_pitch, unsigned int src_slice_pitch, \n"
      "       unsigned int dst_row_pitch, unsigned int dst_slice_pitch) { \n"
      "  int i = get_global_id(0); \n"
      "  int j = get_global_id(1); \n"
      "  int k = get_global_id(2); \n"
      "  if((i >= region0) || (j>= region1) || (k>=region2)) \n"
      "    return; \n"
      "  src_offset += k * src_slice_pitch + j * src_row_pitch + i; \n"
      "  dst_offset += k * dst_slice_pitch + j * dst_row_pitch + i; \n"
      "  dst[dst_offset] = src[src_offset]; \n"
      "}";


  /* We use one kernel to copy the data. The kernel is lazily created. */
  assert(src_buf->ctx == dst_buf->ctx);

  /* setup the kernel and run. */
  ker = cl_context_get_static_kernel(queue->ctx, index, str_kernel, NULL);
  if (!ker)
    return CL_OUT_OF_RESOURCES;

  cl_kernel_set_arg(ker, 0, sizeof(cl_mem), &src_buf);
  cl_kernel_set_arg(ker, 1, sizeof(cl_mem), &dst_buf);
  cl_kernel_set_arg(ker, 2, sizeof(cl_int), &region[0]);
  cl_kernel_set_arg(ker, 3, sizeof(cl_int), &region[1]);
  cl_kernel_set_arg(ker, 4, sizeof(cl_int), &region[2]);
  cl_kernel_set_arg(ker, 5, sizeof(cl_int), &src_offset);
  cl_kernel_set_arg(ker, 6, sizeof(cl_int), &dst_offset);
  cl_kernel_set_arg(ker, 7, sizeof(cl_int), &src_row_pitch);
  cl_kernel_set_arg(ker, 8, sizeof(cl_int), &src_slice_pitch);
  cl_kernel_set_arg(ker, 9, sizeof(cl_int), &dst_row_pitch);
  cl_kernel_set_arg(ker, 10, sizeof(cl_int), &dst_slice_pitch);

  ret = cl_command_queue_ND_range(queue, ker, 1, global_off, global_sz, local_sz);

  return ret;
}

LOCAL cl_int
cl_mem_kernel_copy_image(cl_command_queue queue, struct _cl_mem_image* src_image, struct _cl_mem_image* dst_image,
                         const size_t *src_origin, const size_t *dst_origin, const size_t *region) {
  cl_int ret;
  cl_kernel ker;
  size_t global_off[] = {0,0,0};
  size_t global_sz[] = {1,1,1};
  size_t local_sz[] = {LOCAL_SZ_0,LOCAL_SZ_1,LOCAL_SZ_2};
  cl_int index = CL_ENQUEUE_COPY_IMAGE_0;
  char option[40] = "";
  uint32_t fixupDataType;
  uint32_t savedIntelFmt;

  if(region[1] == 1) local_sz[1] = 1;
  if(region[2] == 1) local_sz[2] = 1;
  global_sz[0] = ((region[0] + local_sz[0] - 1) / local_sz[0]) * local_sz[0];
  global_sz[1] = ((region[1] + local_sz[1] - 1) / local_sz[1]) * local_sz[1];
  global_sz[2] = ((region[2] + local_sz[2] - 1) / local_sz[2]) * local_sz[2];

  if(src_image->image_type == CL_MEM_OBJECT_IMAGE3D) {
    strcat(option, "-D SRC_IMAGE_3D");
    index += 1;
  }
  if(dst_image->image_type == CL_MEM_OBJECT_IMAGE3D) {
    strcat(option, " -D DST_IMAGE_3D");
    index += 2;
  }

  switch (src_image->fmt.image_channel_data_type) {
    case CL_SNORM_INT8:
    case CL_UNORM_INT8:  fixupDataType = CL_UNSIGNED_INT8; break;
    case CL_HALF_FLOAT:
    case CL_SNORM_INT16:
    case CL_UNORM_INT16: fixupDataType = CL_UNSIGNED_INT16; break;
    case CL_FLOAT:       fixupDataType = CL_UNSIGNED_INT32; break;
    default:
      fixupDataType = 0;
  }

  if (fixupDataType) {
    cl_image_format fmt;
    if (src_image->fmt.image_channel_order != CL_BGRA)
      fmt.image_channel_order = src_image->fmt.image_channel_order;
    else
      fmt.image_channel_order = CL_RGBA;
    fmt.image_channel_data_type = fixupDataType;
    savedIntelFmt = src_image->intel_fmt;
    src_image->intel_fmt = cl_image_get_intel_format(&fmt);
    dst_image->intel_fmt = src_image->intel_fmt;
  }
  static const char *str_kernel =
      "#ifdef SRC_IMAGE_3D \n"
      "  #define SRC_IMAGE_TYPE image3d_t \n"
      "  #define SRC_COORD_TYPE int4 \n"
      "#else \n"
      "  #define SRC_IMAGE_TYPE image2d_t \n"
      "  #define SRC_COORD_TYPE int2 \n"
      "#endif \n"
      "#ifdef DST_IMAGE_3D \n"
      "  #define DST_IMAGE_TYPE image3d_t \n"
      "  #define DST_COORD_TYPE int4 \n"
      "#else \n"
      "  #define DST_IMAGE_TYPE image2d_t \n"
      "  #define DST_COORD_TYPE int2 \n"
      "#endif \n"
      "kernel void __cl_copy_image ( \n"
      "       __read_only SRC_IMAGE_TYPE src_image, __write_only DST_IMAGE_TYPE dst_image, \n"
      "       unsigned int region0, unsigned int region1, unsigned int region2, \n"
      "       unsigned int src_origin0, unsigned int src_origin1, unsigned int src_origin2, \n"
      "       unsigned int dst_origin0, unsigned int dst_origin1, unsigned int dst_origin2) { \n"
      "  int i = get_global_id(0); \n"
      "  int j = get_global_id(1); \n"
      "  int k = get_global_id(2); \n"
      "  int4 color; \n"
      "  const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST; \n"
      "  SRC_COORD_TYPE src_coord; \n"
      "  DST_COORD_TYPE dst_coord; \n"
      "  if((i >= region0) || (j>= region1) || (k>=region2)) \n"
      "    return; \n"
      "  src_coord.x = src_origin0 + i; \n"
      "  src_coord.y = src_origin1 + j; \n"
      "#ifdef SRC_IMAGE_3D \n"
      "  src_coord.z = src_origin2 + k; \n"
      "#endif \n"
      "  dst_coord.x = dst_origin0 + i; \n"
      "  dst_coord.y = dst_origin1 + j; \n"
      "#ifdef DST_IMAGE_3D \n"
      "  dst_coord.z = dst_origin2 + k; \n"
      "#endif \n"
      "  color = read_imagei(src_image, sampler, src_coord); \n"
      "  write_imagei(dst_image, dst_coord, color); \n"
      "}";

  /* We use one kernel to copy the data. The kernel is lazily created. */
  assert(src_image->base.ctx == dst_image->base.ctx);

  /* setup the kernel and run. */
  ker = cl_context_get_static_kernel(queue->ctx, index, str_kernel, option);
  if (!ker) {
    ret = CL_OUT_OF_RESOURCES;
    goto fail;
  }

  cl_kernel_set_arg(ker, 0, sizeof(cl_mem), &src_image);
  cl_kernel_set_arg(ker, 1, sizeof(cl_mem), &dst_image);
  cl_kernel_set_arg(ker, 2, sizeof(cl_int), &region[0]);
  cl_kernel_set_arg(ker, 3, sizeof(cl_int), &region[1]);
  cl_kernel_set_arg(ker, 4, sizeof(cl_int), &region[2]);
  cl_kernel_set_arg(ker, 5, sizeof(cl_int), &src_origin[0]);
  cl_kernel_set_arg(ker, 6, sizeof(cl_int), &src_origin[1]);
  cl_kernel_set_arg(ker, 7, sizeof(cl_int), &src_origin[2]);
  cl_kernel_set_arg(ker, 8, sizeof(cl_int), &dst_origin[0]);
  cl_kernel_set_arg(ker, 9, sizeof(cl_int), &dst_origin[1]);
  cl_kernel_set_arg(ker, 10, sizeof(cl_int), &dst_origin[2]);

  ret = cl_command_queue_ND_range(queue, ker, 1, global_off, global_sz, local_sz);

fail:
  if (fixupDataType) {
    src_image->intel_fmt = savedIntelFmt;
    dst_image->intel_fmt = savedIntelFmt;
  }
  return ret;
}

LOCAL cl_int
cl_mem_copy_image_to_buffer(cl_command_queue queue, struct _cl_mem_image* image, cl_mem buffer,
                         const size_t *src_origin, const size_t dst_offset, const size_t *region) {
  cl_int ret;
  cl_kernel ker;
  size_t global_off[] = {0,0,0};
  size_t global_sz[] = {1,1,1};
  size_t local_sz[] = {LOCAL_SZ_0,LOCAL_SZ_1,LOCAL_SZ_2};
  cl_int index = CL_ENQUEUE_COPY_IMAGE_TO_BUFFER_0;
  char option[40] = "";
  uint32_t intel_fmt, bpp;
  cl_image_format fmt;
  size_t origin0, region0;

  if(region[1] == 1) local_sz[1] = 1;
  if(region[2] == 1) local_sz[2] = 1;
  global_sz[0] = ((region[0] + local_sz[0] - 1) / local_sz[0]) * local_sz[0];
  global_sz[1] = ((region[1] + local_sz[1] - 1) / local_sz[1]) * local_sz[1];
  global_sz[2] = ((region[2] + local_sz[2] - 1) / local_sz[2]) * local_sz[2];

  if(image->image_type == CL_MEM_OBJECT_IMAGE3D) {
    strcat(option, "-D IMAGE_3D");
    index += 1;
  }

  static const char *str_kernel =
      "#ifdef IMAGE_3D \n"
      "  #define IMAGE_TYPE image3d_t \n"
      "  #define COORD_TYPE int4 \n"
      "#else \n"
      "  #define IMAGE_TYPE image2d_t \n"
      "  #define COORD_TYPE int2 \n"
      "#endif \n"
      "kernel void __cl_copy_image_to_buffer ( \n"
      "       __read_only IMAGE_TYPE image, global uchar* buffer, \n"
      "       unsigned int region0, unsigned int region1, unsigned int region2, \n"
      "       unsigned int src_origin0, unsigned int src_origin1, unsigned int src_origin2, \n"
      "       unsigned int dst_offset) { \n"
      "  int i = get_global_id(0); \n"
      "  int j = get_global_id(1); \n"
      "  int k = get_global_id(2); \n"
      "  uint4 color; \n"
      "  const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_NONE | CLK_FILTER_NEAREST; \n"
      "  COORD_TYPE src_coord; \n"
      "  if((i >= region0) || (j>= region1) || (k>=region2)) \n"
      "    return; \n"
      "  src_coord.x = src_origin0 + i; \n"
      "  src_coord.y = src_origin1 + j; \n"
      "#ifdef IMAGE_3D \n"
      "  src_coord.z = src_origin2 + k; \n"
      "#endif \n"
      "  color = read_imageui(image, sampler, src_coord); \n"
      "  dst_offset += (k * region1 + j) * region0 + i; \n"
      "  buffer[dst_offset] = color.x; \n"
      "}";

  /* We use one kernel to copy the data. The kernel is lazily created. */
  assert(image->base.ctx == buffer->ctx);

  fmt.image_channel_order = CL_R;
  fmt.image_channel_data_type = CL_UNSIGNED_INT8;
  intel_fmt = image->intel_fmt;
  bpp = image->bpp;
  image->intel_fmt = cl_image_get_intel_format(&fmt);
  image->w = image->w * image->bpp;
  image->bpp = 1;
  region0 = region[0] * bpp;
  origin0 = src_origin[0] * bpp;
  global_sz[0] = ((region0 + local_sz[0] - 1) / local_sz[0]) * local_sz[0];

  /* setup the kernel and run. */
  ker = cl_context_get_static_kernel(queue->ctx, index, str_kernel, option);
  if (!ker) {
    ret = CL_OUT_OF_RESOURCES;
    goto fail;
  }

  cl_kernel_set_arg(ker, 0, sizeof(cl_mem), &image);
  cl_kernel_set_arg(ker, 1, sizeof(cl_mem), &buffer);
  cl_kernel_set_arg(ker, 2, sizeof(cl_int), &region0);
  cl_kernel_set_arg(ker, 3, sizeof(cl_int), &region[1]);
  cl_kernel_set_arg(ker, 4, sizeof(cl_int), &region[2]);
  cl_kernel_set_arg(ker, 5, sizeof(cl_int), &origin0);
  cl_kernel_set_arg(ker, 6, sizeof(cl_int), &src_origin[1]);
  cl_kernel_set_arg(ker, 7, sizeof(cl_int), &src_origin[2]);
  cl_kernel_set_arg(ker, 8, sizeof(cl_int), &dst_offset);

  ret = cl_command_queue_ND_range(queue, ker, 1, global_off, global_sz, local_sz);

fail:

  image->intel_fmt = intel_fmt;
  image->bpp = bpp;
  image->w = image->w / bpp;

  return ret;
}


LOCAL cl_int
cl_mem_copy_buffer_to_image(cl_command_queue queue, cl_mem buffer, struct _cl_mem_image* image,
                         const size_t src_offset, const size_t *dst_origin, const size_t *region) {
  cl_int ret;
  cl_kernel ker;
  size_t global_off[] = {0,0,0};
  size_t global_sz[] = {1,1,1};
  size_t local_sz[] = {LOCAL_SZ_0,LOCAL_SZ_1,LOCAL_SZ_2};
  cl_int index = CL_ENQUEUE_COPY_BUFFER_TO_IMAGE_0;
  char option[40] = "";
  uint32_t intel_fmt, bpp;
  cl_image_format fmt;
  size_t origin0, region0;

  if(region[1] == 1) local_sz[1] = 1;
  if(region[2] == 1) local_sz[2] = 1;
  global_sz[0] = ((region[0] + local_sz[0] - 1) / local_sz[0]) * local_sz[0];
  global_sz[1] = ((region[1] + local_sz[1] - 1) / local_sz[1]) * local_sz[1];
  global_sz[2] = ((region[2] + local_sz[2] - 1) / local_sz[2]) * local_sz[2];

  if(image->image_type == CL_MEM_OBJECT_IMAGE3D) {
    strcat(option, "-D IMAGE_3D");
    index += 1;
  }

  static const char *str_kernel =
      "#ifdef IMAGE_3D \n"
      "  #define IMAGE_TYPE image3d_t \n"
      "  #define COORD_TYPE int4 \n"
      "#else \n"
      "  #define IMAGE_TYPE image2d_t \n"
      "  #define COORD_TYPE int2 \n"
      "#endif \n"
      "kernel void __cl_copy_image_to_buffer ( \n"
      "       __read_only IMAGE_TYPE image, global uchar* buffer, \n"
      "       unsigned int region0, unsigned int region1, unsigned int region2, \n"
      "       unsigned int dst_origin0, unsigned int dst_origin1, unsigned int dst_origin2, \n"
      "       unsigned int src_offset) { \n"
      "  int i = get_global_id(0); \n"
      "  int j = get_global_id(1); \n"
      "  int k = get_global_id(2); \n"
      "  uint4 color = (uint4)(0); \n"
      "  COORD_TYPE dst_coord; \n"
      "  if((i >= region0) || (j>= region1) || (k>=region2)) \n"
      "    return; \n"
      "  dst_coord.x = dst_origin0 + i; \n"
      "  dst_coord.y = dst_origin1 + j; \n"
      "#ifdef IMAGE_3D \n"
      "  dst_coord.z = dst_origin2 + k; \n"
      "#endif \n"
      "  src_offset += (k * region1 + j) * region0 + i; \n"
      "  color.x = buffer[src_offset]; \n"
      "  write_imageui(image, dst_coord, color); \n"
      "}";

  /* We use one kernel to copy the data. The kernel is lazily created. */
  assert(image->base.ctx == buffer->ctx);

  fmt.image_channel_order = CL_R;
  fmt.image_channel_data_type = CL_UNSIGNED_INT8;
  intel_fmt = image->intel_fmt;
  bpp = image->bpp;
  image->intel_fmt = cl_image_get_intel_format(&fmt);
  image->w = image->w * image->bpp;
  image->bpp = 1;
  region0 = region[0] * bpp;
  origin0 = dst_origin[0] * bpp;
  global_sz[0] = ((region0 + local_sz[0] - 1) / local_sz[0]) * local_sz[0];

  /* setup the kernel and run. */
  ker = cl_context_get_static_kernel(queue->ctx, index, str_kernel, option);
  if (!ker)
    return CL_OUT_OF_RESOURCES;

  cl_kernel_set_arg(ker, 0, sizeof(cl_mem), &image);
  cl_kernel_set_arg(ker, 1, sizeof(cl_mem), &buffer);
  cl_kernel_set_arg(ker, 2, sizeof(cl_int), &region0);
  cl_kernel_set_arg(ker, 3, sizeof(cl_int), &region[1]);
  cl_kernel_set_arg(ker, 4, sizeof(cl_int), &region[2]);
  cl_kernel_set_arg(ker, 5, sizeof(cl_int), &origin0);
  cl_kernel_set_arg(ker, 6, sizeof(cl_int), &dst_origin[1]);
  cl_kernel_set_arg(ker, 7, sizeof(cl_int), &dst_origin[2]);
  cl_kernel_set_arg(ker, 8, sizeof(cl_int), &src_offset);

  ret = cl_command_queue_ND_range(queue, ker, 1, global_off, global_sz, local_sz);

  image->intel_fmt = intel_fmt;
  image->bpp = bpp;
  image->w = image->w / bpp;

  return ret;
}


LOCAL void*
cl_mem_map(cl_mem mem)
{
  cl_buffer_map(mem->bo, 1);
  assert(cl_buffer_get_virtual(mem->bo));
  return cl_buffer_get_virtual(mem->bo);
}

LOCAL cl_int
cl_mem_unmap(cl_mem mem)
{
  cl_buffer_unmap(mem->bo);
  return CL_SUCCESS;
}

LOCAL void*
cl_mem_map_gtt(cl_mem mem)
{
  cl_buffer_map_gtt(mem->bo);
  assert(cl_buffer_get_virtual(mem->bo));
  return cl_buffer_get_virtual(mem->bo);
}

LOCAL void *
cl_mem_map_gtt_unsync(cl_mem mem)
{
  cl_buffer_map_gtt_unsync(mem->bo);
  assert(cl_buffer_get_virtual(mem->bo));
  return cl_buffer_get_virtual(mem->bo);
}

LOCAL cl_int
cl_mem_unmap_gtt(cl_mem mem)
{
  cl_buffer_unmap_gtt(mem->bo);
  return CL_SUCCESS;
}

LOCAL void*
cl_mem_map_auto(cl_mem mem)
{
  if (IS_IMAGE(mem) && cl_mem_image(mem)->tiling != CL_NO_TILE)
    return cl_mem_map_gtt(mem);
  else
    return cl_mem_map(mem);
}

LOCAL cl_int
cl_mem_unmap_auto(cl_mem mem)
{
  if (IS_IMAGE(mem) && cl_mem_image(mem)->tiling != CL_NO_TILE)
    cl_buffer_unmap_gtt(mem->bo);
  else
    cl_buffer_unmap(mem->bo);
  return CL_SUCCESS;
}

LOCAL cl_int
cl_mem_pin(cl_mem mem)
{
  assert(mem);
  if (UNLIKELY((mem->flags & CL_MEM_PINNABLE) == 0))
    return CL_INVALID_MEM_OBJECT;
  cl_buffer_pin(mem->bo, 4096);
  return CL_SUCCESS;
}

LOCAL cl_int
cl_mem_unpin(cl_mem mem)
{
  assert(mem);
  if (UNLIKELY((mem->flags & CL_MEM_PINNABLE) == 0))
    return CL_INVALID_MEM_OBJECT;
  cl_buffer_unpin(mem->bo);
  return CL_SUCCESS;
}

LOCAL cl_mem cl_mem_new_libva_buffer(cl_context ctx,
                                     unsigned int bo_name,
                                     cl_int* errcode)
{
  cl_int err = CL_SUCCESS;
  cl_mem mem = NULL;

  mem = cl_mem_allocate(CL_MEM_BUFFER_TYPE, ctx, 0, 0, CL_FALSE, &err);
  if (mem == NULL || err != CL_SUCCESS)
    goto error;

  size_t sz = 0;
  mem->bo = cl_buffer_get_buffer_from_libva(ctx, bo_name, &sz);
  mem->size = sz;

exit:
  if (errcode)
    *errcode = err;
  return mem;

error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;
}

LOCAL cl_mem cl_mem_new_libva_image(cl_context ctx,
                                    unsigned int bo_name, size_t offset,
                                    size_t width, size_t height,
                                    cl_image_format fmt,
                                    size_t row_pitch,
                                    cl_int *errcode)
{
  cl_int err = CL_SUCCESS;
  cl_mem mem = NULL;
  struct _cl_mem_image *image = NULL;
  uint32_t intel_fmt, bpp;

  intel_fmt = cl_image_get_intel_format(&fmt);
  if (intel_fmt == INTEL_UNSUPPORTED_FORMAT) {
    err = CL_IMAGE_FORMAT_NOT_SUPPORTED;
    goto error;
  }

  cl_image_byte_per_pixel(&fmt, &bpp);

  mem = cl_mem_allocate(CL_MEM_IMAGE_TYPE, ctx, 0, 0, 0, &err);
  if (mem == NULL || err != CL_SUCCESS) {
    err = CL_OUT_OF_HOST_MEMORY;
    goto error;
  }

  image = cl_mem_image(mem);

  mem->bo = cl_buffer_get_image_from_libva(ctx, bo_name, image);

  image->w = width;
  image->h = height;
  image->image_type = CL_MEM_OBJECT_IMAGE2D;
  image->depth = 2;
  image->fmt = fmt;
  image->intel_fmt = intel_fmt;
  image->bpp = bpp;
  image->row_pitch = row_pitch;
  image->slice_pitch = 0;
  // NOTE: tiling of image is set in cl_buffer_get_image_from_libva().
  image->tile_x = 0;
  image->tile_y = 0;
  image->offset = offset;

exit:
  if (errcode)
    *errcode = err;
  return mem;

error:
  cl_mem_delete(mem);
  mem = NULL;
  goto exit;
}
