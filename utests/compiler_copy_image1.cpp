#include "utest_helper.hpp"

static void compiler_copy_image1(void)
{
  const size_t w = 512;
  const size_t h = 512;
  cl_image_format format;
  cl_sampler sampler;

  // Setup kernel and images
  OCL_CREATE_KERNEL("test_copy_image1");
  buf_data[0] = (uint32_t*) malloc(sizeof(uint32_t) * w * h);
  for (uint32_t j = 0; j < h; ++j)
    for (uint32_t i = 0; i < w; i++)
      ((uint32_t*)buf_data[0])[j * w + i] = j * w + i;

  format.image_channel_order = CL_RGBA;
  format.image_channel_data_type = CL_UNSIGNED_INT8;
  OCL_CREATE_IMAGE2D(buf[0], CL_MEM_COPY_HOST_PTR, &format, w, h, w * sizeof(uint32_t), buf_data[0]);
  OCL_CREATE_SAMPLER(sampler, CL_ADDRESS_REPEAT, CL_FILTER_NEAREST);

  OCL_CREATE_IMAGE2D(buf[1], 0, &format, w, h, 0, NULL);
  OCL_CREATE_IMAGE2D(buf[2], 0, &format, w, h, 0, NULL);
  OCL_CREATE_IMAGE2D(buf[3], 0, &format, w, h, 0, NULL);
  OCL_CREATE_IMAGE2D(buf[4], 0, &format, w, h, 0, NULL);
  OCL_CREATE_IMAGE2D(buf[5], 0, &format, w, h, 0, NULL);
  free(buf_data[0]);
  buf_data[0] = NULL;

  // Run the kernel
  OCL_SET_ARG(0, sizeof(cl_mem), &buf[0]);
  OCL_SET_ARG(1, sizeof(cl_mem), &buf[1]);
  OCL_SET_ARG(2, sizeof(sampler), &sampler);
  OCL_SET_ARG(3, sizeof(cl_mem), &buf[2]);
  OCL_SET_ARG(4, sizeof(cl_mem), &buf[3]);
  OCL_SET_ARG(5, sizeof(cl_mem), &buf[4]);
  OCL_SET_ARG(6, sizeof(cl_mem), &buf[5]);
  float w_inv = 1.0/w;
  float h_inv = 1.0/h;
  OCL_SET_ARG(7, sizeof(float), &w_inv);
  OCL_SET_ARG(8, sizeof(float), &h_inv);

  globals[0] = w;
  globals[1] = h;
  locals[0] = 16;
  locals[1] = 16;
  OCL_NDRANGE(2);

  // Check result
  OCL_MAP_BUFFER(0);
  OCL_MAP_BUFFER(1);
  OCL_MAP_BUFFER(2);
  OCL_MAP_BUFFER(3);
  OCL_MAP_BUFFER(4);
  OCL_MAP_BUFFER(5);

  for(uint32_t k = 0; k < 5; k++)
  {
    for (uint32_t j = 0; j < h; ++j)
      for (uint32_t i = 0; i < w; i++)
        OCL_ASSERT(((uint32_t*)buf_data[0])[j * w + i] == ((uint32_t*)buf_data[1 + k])[j * w + i]);
  }
  OCL_UNMAP_BUFFER(0);
  OCL_UNMAP_BUFFER(1);
  OCL_UNMAP_BUFFER(2);
  OCL_UNMAP_BUFFER(3);
  OCL_UNMAP_BUFFER(4);
  OCL_UNMAP_BUFFER(5);
}

MAKE_UTEST_FROM_FUNCTION(compiler_copy_image1);
