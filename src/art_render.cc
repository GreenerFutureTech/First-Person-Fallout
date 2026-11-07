#include <lodepng.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "animation_defs.h"
#include "art_defs.h"
#include "content_config.h"
#include "datafile.h"
#include "debug.h"
#include "game.h"
#include "memory.h"
#include "proto.h"
#include "settings.h"
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>
#include "animation.h"
#include "object.h"
#include "sfall_config.h"
#include "art.h"

#include "draw.h"

// artRender is the ONLY presentation-coupled function of the art subsystem: it
// blits a frame into a caller buffer via draw.cc (SDL-backed). It is split out
// of the otherwise SDL-free art.cc so that art.cc can live in f2_core; this TU
// stays in f2_client. The server never renders, so it needs no stub here (the
// core never references artRender).

namespace fallout {

// 0x418FFC
void artRender(const FrmId& frmId, unsigned char* dest, int width, int height, int pitch)
{
    // NOTE: Original code is different. For unknown reason it directly calls
    // many art functions, for example instead of [artLock] it calls lower level
    // [cacheLock], instead of [artGetWidth] is calls [artGetFrame], then get
    // width from frame's struct field. I don't know if this was intentional or
    // not. I've replaced these calls with higher level functions where
    // appropriate.

    FrmImage frmImage;

    if (!frmImage.lock(frmId)) {
        return;
    }

    unsigned char* frameData = frmImage.getData();
    int frameWidth = frmImage.getWidth();
    int frameHeight = frmImage.getHeight();

    int remainingWidth = width - frameWidth;
    int remainingHeight = height - frameHeight;
    if (remainingWidth < 0 || remainingHeight < 0) {
        if (height * frameWidth >= width * frameHeight) {
            blitBufferToBufferStretchTrans(frameData,
                frameWidth,
                frameHeight,
                frameWidth,
                dest + pitch * ((height - width * frameHeight / frameWidth) / 2),
                width,
                width * frameHeight / frameWidth,
                pitch);
        } else {
            blitBufferToBufferStretchTrans(frameData,
                frameWidth,
                frameHeight,
                frameWidth,
                dest + (width - height * frameWidth / frameHeight) / 2,
                height * frameWidth / frameHeight,
                height,
                pitch);
        }
    } else {
        blitBufferToBufferTrans(frameData,
            frameWidth,
            frameHeight,
            frameWidth,
            dest + pitch * (remainingHeight / 2) + remainingWidth / 2,
            pitch);
    }

    frmImage.unlock();
}

} // namespace fallout
