#include <vector>
#include <assert.h>
#include <algorithm>
#include <limits>
#include <vector>
#include "animation.h"
#include "animation_defs.h"
#include "combat.h"
#include "combat_ai.h"
#include "critter.h"
#include "debug.h"
#include "game.h"
#include "game_mouse.h"
#include "item.h"
#include "map.h"
#include "map_defs.h"
#include "party_member.h"
#include "scripts.h"
#include "settings.h"
#include "svga.h"
#include "worldmap.h"
#include "presenter.h"
#include "server_players.h"
#include "server_loop.h"
#include "client_net.h" // clientViewerActive — combat-mirror guards (COMBAT_CLIENT_DESIGN.md §5.1)
#include "object_render.h"

#include <string.h>

#include "art.h"
#include "color.h"
#include "draw.h"
#include "geometry.h"
#include "light.h"
#include "memory.h"
#include "object.h"
#include "proto.h"
#include "proto_instance.h"
#include "text_object.h"
#include "tile.h"

// Client-side render layer extracted from object.cc (Batch 7 / REWRITE_PLAN 1.4).
// Pure presentation: blits into the iso window buffer handed in by the client.
// object.cc (core sim) retains ownership of the shared state below and calls
// the render/blend table init/exit seam declared in object_render.h. This move
// is mechanical and replay-identical.

namespace fallout {

// Render-only file statics (moved wholesale from object.cc).
// 0x519620
static ObjectListNode** _renderTable = nullptr;
// Number of objects in _outlinedObjects.
//
// 0x519624
static int _outlineCount = 0;
// 0x519780
Color* _wallBlendTable = nullptr;
// 0x519784
static Color* _glassBlendTable = nullptr;
// 0x519788
static Color* _steamBlendTable = nullptr;
// 0x51978C
static Color* _energyBlendTable = nullptr;
// 0x519790
static Color* _redBlendTable = nullptr;
// Likely outlined objects on the screen.
//
// 0x639C00
static std::vector<Object*> outlinedObjects;
// 0x660EA0
static Color _glassGrayTable[COLOR_COUNT];
// 0x660FA0
Color _commonGrayTable[COLOR_COUNT];

static void objectDrawOutline(Object* object, Rect* rect);
static void _obj_render_object(Object* object, Rect* rect, int light);

// 0x489550
void _obj_render_pre_roof(Rect* rect, MapElevation elevation)
{
    if (!gObjectsInitialized) {
        return;
    }

    Rect updatedRect;
    if (rectIntersection(rect, &gObjectsWindowRect, &updatedRect) != 0) {
        return;
    }

    int ambientIntensity = lightGetAmbientIntensity();
    int minX = updatedRect.left - 320;
    int minY = updatedRect.top - 240;
    int maxX = updatedRect.right + 320;
    int maxY = updatedRect.bottom + 240;
    int upperLeftTile = tileFromScreenXY(minX, minY, true);
    int updateAreaHexWidth = (maxX - minX + 1) / 32;
    int updateAreaHexHeight = (maxY - minY + 1) / 12;
    int parity = gCenterTile & 1;

    outlinedObjects.clear();

    int renderCount = 0;
    for (int i = 0; i < gObjectsUpdateAreaHexSize; i++) {
        int offsetIndex = _orderTable[parity][i];
        if (updateAreaHexHeight > _offsetDivTable[offsetIndex] && updateAreaHexWidth > _offsetModTable[offsetIndex]) {
            int tile = upperLeftTile + _offsetTable[parity][offsetIndex];
            ObjectListNode* objectListNode = hexGridTileIsValid(tile)
                ? gObjectListHeadByTile[tile]
                : nullptr;

            int lightIntensity = ambientIntensity;
            if (objectListNode != nullptr) {
                // NOTE: Calls `lightGetTileIntensity` twice.
                lightIntensity = std::max(ambientIntensity, lightGetTileIntensity(elevation, objectListNode->obj->tile));
            }

            while (objectListNode != nullptr) {
                if (elevation < objectListNode->obj->elevation) {
                    break;
                }

                if (elevation == objectListNode->obj->elevation) {
                    if ((objectListNode->obj->flags & OBJECT_FLAT) == OBJECT_NONE) {
                        break;
                    }

                    if ((objectListNode->obj->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
                        _obj_render_object(objectListNode->obj, &updatedRect, lightIntensity);

                        if (objectHasVisibleOutline(objectListNode->obj)) {
                            outlinedObjects.push_back(objectListNode->obj);
                        }
                    }
                }

                objectListNode = objectListNode->next;
            }

            if (objectListNode != nullptr) {
                _renderTable[renderCount++] = objectListNode;
            }
        }
    }

    tileRenderEdgeBlackSquares(&updatedRect, elevation, false);

    for (int i = 0; i < renderCount; i++) {
        int lightIntensity = ambientIntensity;

        ObjectListNode* objectListNode = _renderTable[i];
        if (objectListNode != nullptr) {
            // NOTE: Calls `lightGetTileIntensity` twice.
            lightIntensity = std::max(ambientIntensity, lightGetTileIntensity(elevation, objectListNode->obj->tile));
        }

        while (objectListNode != nullptr) {
            Object* object = objectListNode->obj;
            if (elevation < object->elevation) {
                break;
            }

            if (elevation == objectListNode->obj->elevation) {
                if ((objectListNode->obj->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
                    _obj_render_object(object, &updatedRect, lightIntensity);

                    if (objectHasVisibleOutline(objectListNode->obj)) {
                        outlinedObjects.push_back(objectListNode->obj);
                    }
                }
            }

            objectListNode = objectListNode->next;
        }
    }

    tileRenderEdgeBlackSquares(&updatedRect, elevation, true);
}

// 0x4897EC
void _obj_render_post_roof(Rect* rect, MapElevation elevation)
{
    if (!gObjectsInitialized) {
        return;
    }

    Rect updatedRect;
    if (rectIntersection(rect, &gObjectsWindowRect, &updatedRect) != 0) {
        return;
    }

    for (Object* object : outlinedObjects) {
        objectDrawOutline(object, &updatedRect);
    }

    textObjectsRenderInRect(&updatedRect);

    ObjectListNode* objectListNode = gObjectListHead;
    while (objectListNode != nullptr) {
        Object* object = objectListNode->obj;
        if ((object->flags & OBJECT_HIDDEN) == OBJECT_NONE) {
            _obj_render_object(object, &updatedRect, 0x10000);
        }
        objectListNode = objectListNode->next;
    }
}

// 0x48BDD8
void _translucent_trans_buf_to_buf(unsigned char* src, int srcWidth, int srcHeight, int srcPitch, unsigned char* dest, int destX, int destY, int destPitch, Color* blendTable, Color* colorTable)
{
    dest += destPitch * destY + destX;
    int srcStep = srcPitch - srcWidth;
    int destStep = destPitch - srcWidth;

    for (int y = 0; y < srcHeight; y++) {
        for (int x = 0; x < srcWidth; x++) {
            Color v1 = colorTable[*src];
            Color* v2 = blendTable + (v1 << 8);
            unsigned char v3 = *dest;

            *dest = v2[v3];

            src++;
            dest++;
        }

        src += srcStep;
        dest += destStep;
    }
}

// 0x48BEFC
void _dark_trans_buf_to_buf(unsigned char* src, int srcWidth, int srcHeight, int srcPitch, unsigned char* dest, int destX, int destY, int destPitch, int intensity)
{
    unsigned char* sp = src;
    unsigned char* dp = dest + destPitch * destY + destX;

    int srcStep = srcPitch - srcWidth;
    int destStep = destPitch - srcWidth;
    int intensityIndex = intensity / 512;

    for (int y = 0; y < srcHeight; y++) {
        for (int x = 0; x < srcWidth; x++) {
            Color color = static_cast<Color>(*sp);
            if (color != COLOR_FIRST) {
                if (color < 0xE5) {
                    color = intensityColorTable[color][intensityIndex];
                }

                *dp = color;
            }

            sp++;
            dp++;
        }

        sp += srcStep;
        dp += destStep;
    }
}

// 0x48BF88
void _dark_translucent_trans_buf_to_buf(unsigned char* src, int srcWidth, int srcHeight, int srcPitch, unsigned char* dest, int destX, int destY, int destPitch, int intensity, Color* blendTable, Color* colorTable)
{
    int srcStep = srcPitch - srcWidth;
    int destStep = destPitch - srcWidth;
    int intensityIndex = intensity / 512;

    dest += destPitch * destY + destX;

    for (int y = 0; y < srcHeight; y++) {
        for (int x = 0; x < srcWidth; x++) {
            unsigned char srcByte = *src;
            if (srcByte != 0) {
                unsigned char destByte = *dest;
                unsigned int index = colorTable[srcByte] << 8;
                index = blendTable[index + destByte];
                *dest = intensityColorTable[index][intensityIndex];
            }

            src++;
            dest++;
        }

        src += srcStep;
        dest += destStep;
    }
}

// 0x48C03C
void _intensity_mask_buf_to_buf(unsigned char* src, int srcWidth, int srcHeight, int srcPitch, unsigned char* dest, int destPitch, unsigned char* mask, int maskPitch, int intensity)
{
    int srcStep = srcPitch - srcWidth;
    int destStep = destPitch - srcWidth;
    int maskStep = maskPitch - srcWidth;
    int intensityIndex = intensity / 512;

    for (int y = 0; y < srcHeight; y++) {
        for (int x = 0; x < srcWidth; x++) {
            unsigned char color = *src;
            if (color != 0) {
                color = intensityColorTable[color][intensityIndex];
                if (*mask != 0) {
                    unsigned char v1 = intensityColorTable[*dest][128 - *mask];
                    unsigned char v2 = intensityColorTable[color][*mask];
                    color = colorMixAddTable[v2][v1];
                }
                *dest = color;
            }

            src++;
            dest++;
            mask++;
        }

        src += srcStep;
        dest += destStep;
        mask += maskStep;
    }
}

// 0x48CF8C
int _obj_render_table_init()
{
    if (_renderTable != nullptr) {
        return -1;
    }

    _renderTable = (ObjectListNode**)internal_malloc(sizeof(*_renderTable) * gObjectsUpdateAreaHexSize);
    if (_renderTable == nullptr) {
        return -1;
    }

    for (int index = 0; index < gObjectsUpdateAreaHexSize; index++) {
        _renderTable[index] = nullptr;
    }

    return 0;
}

// NOTE: Inlined.
//
// 0x48D000
void _obj_render_table_exit()
{
    if (_renderTable != nullptr) {
        internal_free(_renderTable);
        _renderTable = nullptr;
    }
}

// 0x48D1E4
void _obj_blend_table_init()
{
    for (int index = COLOR_FIRST; index < COLOR_COUNT; index++) {
        int rgb = Color2RGB(static_cast<Color>(index & COLOR_LAST));
        int r = (rgb & 0x7C00) >> 10;
        int g = (rgb & 0x3E0) >> 5;
        int b = rgb & 0x1F;
        _glassGrayTable[index] = static_cast<Color>(((r + 5 * g + 4 * b) / 10) >> 2);
        _commonGrayTable[index] = static_cast<Color>(((b + 3 * r + 6 * g) / 10) >> 2);
    }

    _glassGrayTable[0] = COLOR_FIRST;
    _commonGrayTable[0] = COLOR_FIRST;

    _wallBlendTable = _getColorBlendTable(COLOR_PALE_BLUE);
    _glassBlendTable = _getColorBlendTable(COLOR_CYAN);
    _steamBlendTable = _getColorBlendTable(COLOR_WHITE);
    _energyBlendTable = _getColorBlendTable(COLOR_YELLOW_GREEN);
    _redBlendTable = _getColorBlendTable(COLOR_RED);
}

// NOTE: Inlined.
//
// 0x48D2E8
void _obj_blend_table_exit()
{
    _freeColorBlendTable(COLOR_PALE_BLUE);
    _freeColorBlendTable(COLOR_CYAN);
    _freeColorBlendTable(COLOR_WHITE);
    _freeColorBlendTable(COLOR_YELLOW_GREEN);
    _freeColorBlendTable(COLOR_RED);
}

// 0x48EABC
static void objectDrawOutline(Object* object, Rect* rect)
{
    CacheEntry* cacheEntry;
    Art* art = artLock(FrmId(object), &cacheEntry);
    if (art == nullptr) {
        return;
    }

    int frameWidth = 0;
    int frameHeight = 0;
    artGetSize(art, object->frame, object->rotation, &frameWidth, &frameHeight);

    Rect visibleFrameRect;
    visibleFrameRect.left = 0;
    visibleFrameRect.top = 0;
    visibleFrameRect.right = frameWidth - 1;

    visibleFrameRect.bottom = frameHeight - 1;

    Rect objectRect;
    if (object->tile == -1) {
        objectRect.left = object->sx;
        objectRect.top = object->sy;
        objectRect.right = object->sx + frameWidth - 1;
        objectRect.bottom = object->sy + frameHeight - 1;
    } else {
        int x;
        int y;
        tileToScreenXY(object->tile, &x, &y);
        x += 16;
        y += 8;

        x += art->xOffsets[object->rotation];
        y += art->yOffsets[object->rotation];

        x += object->x;
        y += object->y;

        objectRect.left = x - frameWidth / 2;
        objectRect.top = y - (frameHeight - 1);
        objectRect.right = objectRect.left + frameWidth - 1;
        objectRect.bottom = y;

        object->sx = objectRect.left;
        object->sy = objectRect.top;
    }

    Rect expandedRect;
    rectCopy(&expandedRect, rect);

    expandedRect.left--;
    expandedRect.top--;
    expandedRect.right++;
    expandedRect.bottom++;

    rectIntersection(&expandedRect, &gObjectsWindowRect, &expandedRect);

    if (rectIntersection(&objectRect, &expandedRect, &objectRect) == 0) {
        visibleFrameRect.left += objectRect.left - object->sx;
        visibleFrameRect.top += objectRect.top - object->sy;
        visibleFrameRect.right = visibleFrameRect.left + (objectRect.right - objectRect.left);
        visibleFrameRect.bottom = visibleFrameRect.top + (objectRect.bottom - objectRect.top);

        unsigned char* src = artGetFrameData(art, object->frame, object->rotation);

        // TODO: This base pointer is computed from unclipped object coordinates.
        // Convert the outline scan to integer offsets so partially offscreen
        // objects do not form out-of-bounds pointers before write-site checks.
        unsigned char* dest = gObjectsWindowBuffer + gObjectsWindowPitch * object->sy + object->sx;
        int destStep = gObjectsWindowPitch - frameWidth;

        Color color;
        Color* grayTable = nullptr;
        Color* blendTable = nullptr;
        int isOutlinePalleted = object->outline & OUTLINE_PALETTED;
        OutlineType outlineType = object->outline & OUTLINE_TYPE_MAX;
        int animatedColorCount = 0;
        int animatedColorBandHeight;

        switch (outlineType) {
        case OUTLINE_TYPE_HOSTILE:
            color = Color(243);
            isOutlinePalleted = 0;
            animatedColorCount = 5;
            animatedColorBandHeight = frameHeight / 5;
            break;
        case OUTLINE_TYPE_SAME_TEAM:
            color = COLOR_RED;
            animatedColorBandHeight = 0;
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_BODY:
            color = COLOR_GREY_2;
            animatedColorBandHeight = 0;
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _wallBlendTable;
            }
            break;
        case OUTLINE_TYPE_FRIENDLY:
            animatedColorCount = 4;
            animatedColorBandHeight = frameHeight / 4;
            color = Color(229);
            isOutlinePalleted = 0;
            break;
        case OUTLINE_TYPE_ITEM:
            animatedColorBandHeight = 0;
            color = COLOR_LIGHT_GOLD_2;
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_LOOT_ITEM:
            // Highlight-lootables overlay: loose ground item — yellow (248,248,0).
            animatedColorBandHeight = 0;
            color = _colorTable[32736];
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_LOOT_CORPSE:
            // Highlight-lootables overlay: dead critter — orange (248,128,0).
            animatedColorBandHeight = 0;
            color = _colorTable[32256];
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_LOOT_CONTAINER:
            // Highlight-lootables overlay: container — cyan (0,248,248).
            animatedColorBandHeight = 0;
            color = _colorTable[1023];
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_LOOT_DOOR:
            // Highlight-lootables overlay: door / transition scenery — green (0,248,0).
            animatedColorBandHeight = 0;
            color = _colorTable[992];
            if (isOutlinePalleted != 0) {
                grayTable = _commonGrayTable;
                blendTable = _redBlendTable;
            }
            break;
        case OUTLINE_TYPE_BLOCKED:
            color = Color(61);
            isOutlinePalleted = 0;
            animatedColorCount = 1;
            animatedColorBandHeight = frameHeight;
            break;
        default:
            color = COLOR_MAGENTA;
            isOutlinePalleted = 0;
            animatedColorBandHeight = 0;
            break;
        }

        Color outlineColor = color;
        unsigned char* destPtr = dest;
        unsigned char* srcPtr = src;
        for (int y = 0; y < frameHeight; y++) {
            bool cycle = true;
            if (animatedColorBandHeight != 0) {
                if (y % animatedColorBandHeight == 0) {
                    outlineColor = static_cast<Color>((outlineColor + 1) & COLOR_LAST);
                }

                if (outlineColor > animatedColorCount + color - 1) {
                    outlineColor = color;
                }
            }

            bool yVisible = y >= visibleFrameRect.top && y <= visibleFrameRect.bottom;
            Color* outlineBlendTable = isOutlinePalleted != 0
                ? blendTable + (grayTable[outlineColor] << 8)
                : nullptr;
            for (int x = 0; x < frameWidth; x++) {
                int destOffset = destPtr - gObjectsWindowBuffer;
                if (*srcPtr != 0 && cycle) {
                    if (yVisible && x >= visibleFrameRect.left && x <= visibleFrameRect.right && destOffset > 0 && destOffset % gObjectsWindowPitch != 0) {
                        Color leftOutlineColor;
                        if (isOutlinePalleted != 0) {
                            leftOutlineColor = outlineBlendTable[*(destPtr - 1)];
                        } else {
                            leftOutlineColor = outlineColor;
                        }
                        *(destPtr - 1) = leftOutlineColor;
                    }
                    cycle = false;
                } else if (*srcPtr == 0 && !cycle) {
                    if (yVisible && x >= visibleFrameRect.left && x <= visibleFrameRect.right) {
                        Color rightOutlineColor;
                        if (isOutlinePalleted != 0) {
                            rightOutlineColor = outlineBlendTable[*destPtr];
                        } else {
                            rightOutlineColor = outlineColor;
                        }
                        *destPtr = rightOutlineColor & COLOR_LAST;
                    }
                    cycle = true;
                }
                destPtr++;
                srcPtr++;
            }

            if (*(srcPtr - 1) != 0) {
                int rightOutlineDestOffset = destPtr - gObjectsWindowBuffer;
                if (rightOutlineDestOffset >= 0 && rightOutlineDestOffset < gObjectsWindowBufferSize && rightOutlineDestOffset % gObjectsWindowPitch != 0) {
                    int rightEdgeX = frameWidth - 1;
                    if (yVisible && rightEdgeX >= visibleFrameRect.left && rightEdgeX <= visibleFrameRect.right) {
                        if (isOutlinePalleted != 0) {
                            *destPtr = outlineBlendTable[*destPtr];
                        } else {
                            *destPtr = outlineColor;
                        }
                    }
                }
            }

            destPtr += destStep;
        }

        for (int x = 0; x < frameWidth; x++) {
            if (x < visibleFrameRect.left || x > visibleFrameRect.right) {
                continue;
            }

            bool cycle = true;
            Color columnOutlineColor = color;
            unsigned char* columnDestPtr = dest + x;
            unsigned char* columnSrcPtr = src + x;
            for (int y = 0; y < frameHeight; y++) {
                if (animatedColorBandHeight != 0) {
                    if (y % animatedColorBandHeight == 0) {
                        columnOutlineColor = static_cast<Color>((columnOutlineColor + 1) & COLOR_LAST);
                    }

                    if (columnOutlineColor > color + animatedColorCount - 1) {
                        columnOutlineColor = color;
                    }
                }

                bool yVisible = y >= visibleFrameRect.top && y <= visibleFrameRect.bottom;
                Color* columnOutlineBlendTable = isOutlinePalleted != 0
                    ? blendTable + (grayTable[columnOutlineColor] << 8)
                    : nullptr;
                if (*columnSrcPtr != 0 && cycle) {
                    if (yVisible) {
                        unsigned char* aboveDestPtr = columnDestPtr - gObjectsWindowPitch;
                        if (aboveDestPtr >= gObjectsWindowBuffer) {
                            if (isOutlinePalleted) {
                                *aboveDestPtr = columnOutlineBlendTable[*aboveDestPtr];
                            } else {
                                *aboveDestPtr = columnOutlineColor;
                            }
                        }
                    }
                    cycle = false;
                } else if (*columnSrcPtr == 0 && !cycle) {
                    if (yVisible) {
                        if (isOutlinePalleted) {
                            *columnDestPtr = columnOutlineBlendTable[*columnDestPtr];
                        } else {
                            *columnDestPtr = columnOutlineColor;
                        }
                    }
                    cycle = true;
                }

                columnDestPtr += gObjectsWindowPitch;
                columnSrcPtr += frameWidth;
            }

            if (columnSrcPtr[-frameWidth] != 0) {
                int bottomOutlineDestOffset = columnDestPtr - gObjectsWindowBuffer;
                if (bottomOutlineDestOffset >= 0 && bottomOutlineDestOffset < gObjectsWindowBufferSize) {
                    int y = frameHeight - 1;
                    if (y >= visibleFrameRect.top && y <= visibleFrameRect.bottom) {
                        if (isOutlinePalleted) {
                            Color* columnOutlineBlendTable = blendTable + (grayTable[columnOutlineColor] << 8);
                            *columnDestPtr = columnOutlineBlendTable[*columnDestPtr];
                        } else {
                            *columnDestPtr = columnOutlineColor;
                        }
                    }
                }
            }
        }
    }

    artUnlock(cacheEntry);
}

// 0x48F1B0
static void _obj_render_object(Object* object, Rect* rect, int light)
{
    const FrmId frmId = FrmId(object);
    ObjectType type = frmId.objectType();
    if (artIsObjectTypeHidden(type)) {
        return;
    }

    CacheEntry* cacheEntry;
    Art* art = artLock(frmId, &cacheEntry);
    if (art == nullptr) {
        return;
    }

    int frameWidth = artGetWidth(art, object->frame, object->rotation);
    int frameHeight = artGetHeight(art, object->frame, object->rotation);

    Rect objectRect;
    if (object->tile == -1) {
        objectRect.left = object->sx;
        objectRect.top = object->sy;
        objectRect.right = object->sx + frameWidth - 1;
        objectRect.bottom = object->sy + frameHeight - 1;
    } else {
        int objectScreenX;
        int objectScreenY;
        tileToScreenXY(object->tile, &objectScreenX, &objectScreenY);
        objectScreenX += 16;
        objectScreenY += 8;

        objectScreenX += art->xOffsets[object->rotation];
        objectScreenY += art->yOffsets[object->rotation];

        objectScreenX += object->x;
        objectScreenY += object->y;

        objectRect.left = objectScreenX - frameWidth / 2;
        objectRect.top = objectScreenY - (frameHeight - 1);
        objectRect.right = objectRect.left + frameWidth - 1;
        objectRect.bottom = objectScreenY;

        object->sx = objectRect.left;
        object->sy = objectRect.top;
    }

    if (rectIntersection(&objectRect, rect, &objectRect) != 0) {
        artUnlock(cacheEntry);
        return;
    }

    unsigned char* src = artGetFrameData(art, object->frame, object->rotation);
    unsigned char* src2 = src;
    int v50 = objectRect.left - object->sx;
    int v49 = objectRect.top - object->sy;
    src += frameWidth * v49 + v50;
    int objectWidth = objectRect.right - objectRect.left + 1;
    int objectHeight = objectRect.bottom - objectRect.top + 1;

    if (type == OBJ_TYPE_INTERFACE) {
        blitBufferToBufferTrans(src,
            objectWidth,
            objectHeight,
            frameWidth,
            gObjectsWindowBuffer + gObjectsWindowPitch * objectRect.top + objectRect.left,
            gObjectsWindowPitch);
        artUnlock(cacheEntry);
        return;
    }

    if (type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_WALL) {
        if ((gDude->flags & OBJECT_HIDDEN) == OBJECT_NONE && (object->flags & OBJECT_FLAG_0xFC000) == OBJECT_NONE) {
            Proto* proto;
            protoGetProto(object, &proto);

            bool v17;
            ProtoExtendedFlags extendedFlags = proto->critter.extendedFlags;
            if ((extendedFlags & PROTO_EXT_FLAG_HIDDEN) != PROTO_EXT_FLAG_NONE || (extendedFlags & PROTO_EXT_FLAG_WEST_CORNER) != PROTO_EXT_FLAG_NONE) {
                // TODO: Verify this visibility branch against the original logic.
                v17 = tileIsInFrontOf(object->tile, gDude->tile);
                if (!v17
                    || !tileIsToRightOf(object->tile, gDude->tile)
                    || (object->flags & OBJECT_WALL_TRANS_END) == OBJECT_NONE) {
                    // nothing
                } else {
                    v17 = false;
                }
            } else if ((extendedFlags & PROTO_EXT_FLAG_NORTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                // NOTE: Original code used bitwise OR here; logical OR is clearer.
                v17 = tileIsInFrontOf(object->tile, gDude->tile)
                    || tileIsToRightOf(gDude->tile, object->tile);
            } else if ((extendedFlags & PROTO_EXT_FLAG_SOUTH_CORNER) != PROTO_EXT_FLAG_NONE) {
                v17 = tileIsInFrontOf(object->tile, gDude->tile)
                    && tileIsToRightOf(gDude->tile, object->tile);
            } else {
                v17 = tileIsToRightOf(gDude->tile, object->tile);
                if (v17
                    && tileIsInFrontOf(gDude->tile, object->tile)
                    && (object->flags & OBJECT_WALL_TRANS_END) != OBJECT_NONE) {
                    v17 = 0;
                }
            }

            if (v17) {
                CacheEntry* eggHandle;
                Art* egg = artLock(FrmId(gEgg), &eggHandle);
                if (egg == nullptr) {
                    return;
                }

                int eggWidth;
                int eggHeight;
                artGetSize(egg, 0, ROTATION_NE, &eggWidth, &eggHeight);

                int eggScreenX;
                int eggScreenY;
                tileToScreenXY(gEgg->tile, &eggScreenX, &eggScreenY);
                eggScreenX += 16;
                eggScreenY += 8;

                eggScreenX += egg->xOffsets[0];
                eggScreenY += egg->yOffsets[0];

                eggScreenX += gEgg->x;
                eggScreenY += gEgg->y;

                Rect eggRect;
                eggRect.left = eggScreenX - eggWidth / 2;
                eggRect.top = eggScreenY - (eggHeight - 1);
                eggRect.right = eggRect.left + eggWidth - 1;
                eggRect.bottom = eggScreenY;

                gEgg->sx = eggRect.left;
                gEgg->sy = eggRect.top;

                Rect updatedEggRect;
                if (rectIntersection(&eggRect, &objectRect, &updatedEggRect) == 0) {
                    Rect rects[4];

                    rects[0].left = objectRect.left;
                    rects[0].top = objectRect.top;
                    rects[0].right = objectRect.right;
                    rects[0].bottom = updatedEggRect.top - 1;

                    rects[1].left = objectRect.left;
                    rects[1].top = updatedEggRect.top;
                    rects[1].right = updatedEggRect.left - 1;
                    rects[1].bottom = updatedEggRect.bottom;

                    rects[2].left = updatedEggRect.right + 1;
                    rects[2].top = updatedEggRect.top;
                    rects[2].right = objectRect.right;
                    rects[2].bottom = updatedEggRect.bottom;

                    rects[3].left = objectRect.left;
                    rects[3].top = updatedEggRect.bottom + 1;
                    rects[3].right = objectRect.right;
                    rects[3].bottom = objectRect.bottom;

                    for (int i = 0; i < 4; i++) {
                        Rect* v21 = &(rects[i]);
                        if (v21->left <= v21->right && v21->top <= v21->bottom) {
                            unsigned char* sp = src + frameWidth * (v21->top - objectRect.top) + (v21->left - objectRect.left);
                            _dark_trans_buf_to_buf(sp, v21->right - v21->left + 1, v21->bottom - v21->top + 1, frameWidth, gObjectsWindowBuffer, v21->left, v21->top, gObjectsWindowPitch, light);
                        }
                    }

                    unsigned char* mask = artGetFrameData(egg);
                    _intensity_mask_buf_to_buf(
                        src + frameWidth * (updatedEggRect.top - objectRect.top) + (updatedEggRect.left - objectRect.left),
                        updatedEggRect.right - updatedEggRect.left + 1,
                        updatedEggRect.bottom - updatedEggRect.top + 1,
                        frameWidth,
                        gObjectsWindowBuffer + gObjectsWindowPitch * updatedEggRect.top + updatedEggRect.left,
                        gObjectsWindowPitch,
                        mask + eggWidth * (updatedEggRect.top - eggRect.top) + (updatedEggRect.left - eggRect.left),
                        eggWidth,
                        light);
                    artUnlock(eggHandle);
                    artUnlock(cacheEntry);
                    return;
                }

                artUnlock(eggHandle);
            }
        }
    }

    switch (object->flags & OBJECT_FLAG_0xFC000) {
    case OBJECT_TRANS_RED:
        _dark_translucent_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, light, _redBlendTable, _commonGrayTable);
        break;
    case OBJECT_TRANS_WALL:
        _dark_translucent_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, 0x10000, _wallBlendTable, _commonGrayTable);
        break;
    case OBJECT_TRANS_GLASS:
        _dark_translucent_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, light, _glassBlendTable, _glassGrayTable);
        break;
    case OBJECT_TRANS_STEAM:
        _dark_translucent_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, light, _steamBlendTable, _commonGrayTable);
        break;
    case OBJECT_TRANS_ENERGY:
        _dark_translucent_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, light, _energyBlendTable, _commonGrayTable);
        break;
    default:
        _dark_trans_buf_to_buf(src, objectWidth, objectHeight, frameWidth, gObjectsWindowBuffer, objectRect.left, objectRect.top, gObjectsWindowPitch, light);
        break;
    }

    artUnlock(cacheEntry);
}

} // namespace fallout
