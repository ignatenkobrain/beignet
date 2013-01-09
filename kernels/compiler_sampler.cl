/* test OpenCL 1.1 sampler declaration */
__kernel void compiler_sampler () {
#define S(A,B,C) CLK_NORMALIZED_COORDS_##A | CLK_ADDRESS_##B | CLK_FILTER_##C
  const sampler_t \
    s0 = S(TRUE,REPEAT,NEAREST),
    s1 = S(TRUE,REPEAT,LINEAR),
    s2 = S(TRUE,CLAMP,NEAREST),
    s3 = S(TRUE,CLAMP,LINEAR),
    s4 = S(TRUE,NONE,NEAREST),
    s5 = S(TRUE,NONE,LINEAR),
    s6 = S(TRUE,CLAMP_TO_EDGE,NEAREST),
    s7 = S(TRUE,CLAMP_TO_EDGE,LINEAR),
    s8 = S(TRUE,MIRRORED_REPEAT,NEAREST),
    s9 = S(TRUE,MIRRORED_REPEAT,LINEAR),
    s10 = S(FALSE,REPEAT,NEAREST),
    s11 = S(FALSE,REPEAT,LINEAR),
    s12 = S(FALSE,CLAMP,NEAREST),
    s13 = S(FALSE,CLAMP,LINEAR),
    s14 = S(FALSE,NONE,NEAREST),
    s15 = S(FALSE,NONE,LINEAR),
    s16 = S(FALSE,CLAMP_TO_EDGE,NEAREST),
    s17 = S(FALSE,CLAMP_TO_EDGE,LINEAR),
    s18 = S(FALSE,MIRRORED_REPEAT,NEAREST),
    s19 = S(FALSE,MIRRORED_REPEAT,LINEAR);
}