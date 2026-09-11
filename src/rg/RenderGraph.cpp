#include "RenderGraph.h"

void RenderGraph::execute(void* cmdBuf) {
  assert(mBackend);

  for (u32 gi = 0; gi < static_cast<u32>(mSortedPasses.size()); gi++) {
    const u32 passId       = mSortedPasses[gi];
    const RGPassData& pass = mPasses[passId];

    for (const RGBarrier& b : mBarriers) {
      if (b.dst_pass != gi) continue;
      RHIBarrierInfo info{
        mGPUHandles[b.resource_id],
        b.resource_id,
        mResources[b.resource_id].kind,
        static_cast<RHIUsage>(b.before_usage),
        static_cast<RHIUsage>(b.after_usage),
        b.kind,
        RHI_QUEUE_IGNORED,
        RHI_QUEUE_IGNORED,
      };
      mBackend->emitBarrier(info, cmdBuf);
    }

    const RHIPassType rhiType = (pass.flags & RG_PASS_RASTER)  ? RHI_PASS_RASTER  :
                                (pass.flags & RG_PASS_COMPUTE) ? RHI_PASS_COMPUTE : RHI_PASS_COPY;
    mBackend->beginPass(cmdBuf, rhiType);
    RGResources res(*this);
    mExecutors[passId]->execute(res, cmdBuf);
    mBackend->endPass(cmdBuf);
  }
}
