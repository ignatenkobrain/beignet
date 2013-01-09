/* The following approved Khronos extension
 * names must be returned by all device that
 * support OpenCL C 1.2. */
#define DECL_BASE_EXTENSIONS \
  DECL_EXT(cl_khr_global_int32_base_atomics) \
  DECL_EXT(cl_khr_global_int32_extended_atomics) \
  DECL_EXT(cl_khr_local_int32_base_atomics) \
  DECL_EXT(cl_khr_local_int32_extended_atomics) \
  DECL_EXT(cl_khr_byte_addressable_store) \
  DECL_EXT(cl_khr_fp64)

/* The OPT1 extensions are those optional extensions
 * which don't have external dependecies*/
#define DECL_OPT1_EXTENSIONS \
  DECL_EXT(cl_khr_int64_base_atomics)\
  DECL_EXT(cl_khr_int64_extended_atomics)\
  DECL_EXT(cl_khr_3d_image_writes)\
  DECL_EXT(cl_khr_fp16)\
  DECL_EXT(cl_khr_image2d_from_buffer)\
  DECL_EXT(cl_khr_initialize_memory)\
  DECL_EXT(cl_khr_context_abort)\
  DECL_EXT(cl_khr_depth_images)\
  DECL_EXT(cl_khr_spir)

#define DECL_GL_EXTENSIONS \
  DECL_EXT(cl_khr_gl_sharing)\
  DECL_EXT(cl_khr_gl_event)\
  DECL_EXT(cl_khr_gl_depth_images)\
  DECL_EXT(cl_khr_gl_msaa_sharing)

#define DECL_D3D_EXTENSIONS \
  DECL_EXT(cl_khr_d3d10_sharing)\
  DECL_EXT(cl_khr_dx9_media_sharing)\
  DECL_EXT(cl_khr_d3d11_sharing)\

#define DECL_ALL_EXTENSIONS \
  DECL_BASE_EXTENSIONS \
  DECL_OPT1_EXTENSIONS \
  DECL_GL_EXTENSIONS \
  DECL_D3D_EXTENSIONS

#define EXT_ID(name) name ## _ext_id
#define EXT_STRUCT_NAME(name) name ##ext
/*Declare enum ids */
typedef enum {
#define DECL_EXT(name) EXT_ID(name),
DECL_ALL_EXTENSIONS
#undef DECL_EXT
cl_khr_extension_id_max
}cl_extension_enum;

#define BASE_EXT_START_ID EXT_ID(cl_khr_global_int32_base_atomics)
#define BASE_EXT_END_ID EXT_ID(cl_khr_fp64)
#define GL_EXT_START_ID EXT_ID(cl_khr_gl_sharing)
#define GL_EXT_END_ID EXT_ID(cl_khr_gl_msaa_sharing)

#define IS_BASE_EXTENSION(id)  (id >= BASE_EXT_START_ID && id <= BASE_EXT_END_ID)
#define IS_GL_EXTENSION(id)    (id >= GL_EXT_START_ID && id <= GL_EXT_END_ID)

struct cl_extension_base {
  cl_extension_enum ext_id;
  int  ext_enabled;
  char *ext_name;
};

/* Declare each extension structure. */
#define DECL_EXT(name) \
struct EXT_STRUCT_NAME(name) { \
  struct cl_extension_base base;\
};

DECL_BASE_EXTENSIONS
DECL_OPT1_EXTENSIONS
DECL_D3D_EXTENSIONS
#undef DECL_EXT

#define DECL_EXT(name) \
struct EXT_STRUCT_NAME(name) { \
  struct cl_extension_base base; \
  struct cl_gl_ext_deps *gl_ext_deps; \
};

struct cl_gl_ext_deps {
#ifdef HAS_EGL
#ifndef EGL_KHR_image
#define PFNEGLCREATEIMAGEKHRPROC void*
#define PFNEGLDESTROYIMAGEKHRPROC void*
#endif
  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_func;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_func;
#ifndef EGL_KHR_image
#undef PFNEGLCREATEIMAGEKHRPROC
#undef PFNEGLDESTROYIMAGEKHRPROC
#endif
#endif
};

DECL_GL_EXTENSIONS
#undef DECL_EXT

/* Union all extensions together. */
typedef union {
  struct cl_extension_base base;
  #define DECL_EXT(name) struct EXT_STRUCT_NAME(name) EXT_STRUCT_NAME(name);
  DECL_ALL_EXTENSIONS
  #undef DECL_EXT
} extension_union;

typedef struct cl_extensions {
  extension_union extensions[cl_khr_extension_id_max];
  char ext_str[256];
} cl_extensions_t;

struct _cl_platform_id;
typedef struct _cl_platform_id * cl_platform_id;
#define CL_EXTENSION_GET_FUNCS(ctx, name, funcs) \
  ctx->device->platform->internal_extensions->extensions[EXT_ID(name)].EXT_STRUCT_NAME(name).funcs

extern void
cl_intel_platform_extension_init(cl_platform_id intel_platform);