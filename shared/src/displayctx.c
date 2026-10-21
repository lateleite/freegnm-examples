#include "displayctx.h"

#include <stdio.h>

#include <orbis/VideoOut.h>

bool displayctx_init(DisplayContext* ctx) {
	int res = sceVideoOutOpen(0, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
	if (res < 0) {
		printf("display: VideoOutOpen failed with 0x%x\n", res);
		return false;
	}

	ctx->videohandle = res;

	OrbisVideoOutResolutionStatus resinfo = {0};
	res = sceVideoOutGetResolutionStatus(ctx->videohandle, &resinfo);
	if (res != 0) {
		printf("display: GetResolution failed with 0x%x\n", res);
		return false;
	}

	ctx->screenw = resinfo.width;
	ctx->screenh = resinfo.height;

	res = sceKernelCreateEqueue(&ctx->flipqueue, "display flip queue");
	if (res != 0) {
		printf("display: CreateEqueue failed with 0x%x\n", res);
		return false;
	}

	sceVideoOutSetFlipRate(ctx->videohandle, 0);
	sceVideoOutAddFlipEvent(ctx->flipqueue, ctx->videohandle, 0);

	return true;
}

bool displayctx_setrts(
	DisplayContext* ctx, GnmRenderTarget* rts, unsigned numrts
) {
	if (!ctx || !rts || !numrts) {
		return false;
	}

	for (unsigned i = 0; i < numrts; i += 1) {
		GnmRenderTarget* rt = &rts[i];
		OrbisVideoOutBufferAttribute attr = {0};
		sceVideoOutSetBufferAttribute(
			&attr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
			ORBIS_VIDEO_OUT_TILING_MODE_TILE, ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
			rt->size.width, rt->size.height, gnmRtGetPitch(rt)
		);

		void* rtaddr = gnmRtGetBaseAddr(rt);
		int res =
			sceVideoOutRegisterBuffers(ctx->videohandle, i, &rtaddr, 1, &attr);
		if (res < 0) {
			printf("display: RegisterBuffers failed with 0x%x\n", res);
			return false;
		}
	}

	return true;
}

bool displayctx_flip(DisplayContext* ctx, unsigned bufidx) {
	int res = sceVideoOutSubmitFlip(
		ctx->videohandle, bufidx, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0
	);
	if (res != 0) {
		printf("display: SubmitFlip failed with 0x%x\n", res);
		return false;
	}

	OrbisKernelEvent ev = {0};
	int out = 0;
	res = sceKernelWaitEqueue(ctx->flipqueue, &ev, 1, &out, 0);
	if (res != 0) {
		printf("display: WaitEqueue failed with 0x%x\n", res);
		return false;
	}

	return true;
}

void displayctx_destroy(DisplayContext* ctx) {
	if (ctx->videohandle) {
		sceVideoOutClose(ctx->videohandle);
		ctx->videohandle = 0;
	}
	if (ctx->flipqueue) {
		sceKernelDeleteEqueue(ctx->flipqueue);
		ctx->flipqueue = 0;
	}
}
