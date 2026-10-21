#ifndef _PASS_UI_H_
#define _PASS_UI_H_

#include <gnm/commandbuffer.h>

#include "memalloc.h"

typedef struct {
	float r;
	float g;
	float b;
	float a;
} ui_color;

void pass_ui_init(
	uint32_t viewwidth, uint32_t viewheight, MemoryAllocator* glrmem
);

void pass_ui_begin(GnmCommandBuffer* cmd);
void pass_ui_end(GnmCommandBuffer* cmd, GnmRenderTarget* rt);

void pass_ui_drawtext(float x, float y, ui_color color, const char* text);
void pass_ui_drawtextf(float x, float y, ui_color color, const char* fmt, ...);

#endif	// _PASS_UI_H_
