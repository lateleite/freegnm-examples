#ifndef _DISPLAYCTX_H_
#define _DISPLAYCTX_H_

#include <orbis/libkernel.h>

#include <gnm/rendertarget.h>

typedef struct {
	int videohandle;
	OrbisKernelEqueue flipqueue;
	uint32_t screenw;
	uint32_t screenh;
} DisplayContext;

bool displayctx_init(DisplayContext* ctx);
bool displayctx_setrts(
	DisplayContext* ctx, GnmRenderTarget* rts, unsigned numrts
);
bool displayctx_flip(DisplayContext* ctx, unsigned bufidx);
void displayctx_destroy(DisplayContext* ctx);

#endif	// _DISPLAYCTX_H_
