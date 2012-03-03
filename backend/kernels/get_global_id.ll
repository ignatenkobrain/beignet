; ModuleID = 'get_global_id.o'
target datalayout = "e-p:32:32-i64:64:64-f64:64:64-n1:8:16:32:64"
target triple = "ptx32--"

define ptx_kernel void @test_global_id(i32* nocapture %dst, i32* nocapture %p) nounwind noinline {
get_global_id.exit13:
  %call.i = tail call ptx_device i32 @__gen_ocl_get_local_id0() nounwind readnone
  %sext = shl i32 %call.i, 16
  %conv1 = ashr exact i32 %sext, 16
  %call.i6 = tail call ptx_device i32 @__gen_ocl_get_global_id0() nounwind readnone
  %arrayidx = getelementptr inbounds i32* %dst, i32 %call.i6
  store i32 %conv1, i32* %arrayidx, align 4, !tbaa !1
  %arrayidx5 = getelementptr inbounds i32* %p, i32 %call.i6
  store i32 %call.i, i32* %arrayidx5, align 4, !tbaa !1
  ret void
}

declare ptx_device i32 @__gen_ocl_get_global_id0() nounwind readnone

declare ptx_device i32 @__gen_ocl_get_local_id0() nounwind readnone

!opencl.kernels = !{!0}

!0 = metadata !{void (i32*, i32*)* @test_global_id}
!1 = metadata !{metadata !"int", metadata !2}
!2 = metadata !{metadata !"omnipotent char", metadata !3}
!3 = metadata !{metadata !"Simple C/C++ TBAA", null}