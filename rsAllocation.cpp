/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rsContext.h"

#include <GLES/gl.h>
#include <GLES/glext.h>

using namespace android;
using namespace android::renderscript;

Allocation::Allocation(Context *rsc, const Type *type) : ObjectBase(rsc)
{
    mAllocFile = __FILE__;
    mAllocLine = __LINE__;
    mPtr = NULL;

    mCpuWrite = false;
    mCpuRead = false;
    mGpuWrite = false;
    mGpuRead = false;

    mReadWriteRatio = 0;
    mUpdateSize = 0;

    mIsTexture = false;
    mTextureID = 0;

    mIsVertexBuffer = false;
    mBufferID = 0;

    mType.set(type);
    rsAssert(type);
    mPtr = malloc(mType->getSizeBytes());
    if (!mPtr) {
        LOGE("Allocation::Allocation, alloc failure");
    }
}

Allocation::~Allocation()
{
    free(mPtr);
    mPtr = NULL;

    if (mBufferID) {
        // Causes a SW crash....
        //LOGV(" mBufferID %i", mBufferID);
        //glDeleteBuffers(1, &mBufferID);
        //mBufferID = 0;
    }
    if (mTextureID) {
        glDeleteTextures(1, &mTextureID);
        mTextureID = 0;
    }
}

void Allocation::setCpuWritable(bool)
{
}

void Allocation::setGpuWritable(bool)
{
}

void Allocation::setCpuReadable(bool)
{
}

void Allocation::setGpuReadable(bool)
{
}

bool Allocation::fixAllocation()
{
    return false;
}

void Allocation::uploadToTexture(uint32_t lodOffset)
{
    //rsAssert(!mTextureId);
    rsAssert(lodOffset < mType->getLODCount());

    GLenum type = mType->getElement()->getGLType();
    GLenum format = mType->getElement()->getGLFormat();

    if (!type || !format) {
        return;
    }

    if (!mTextureID) {
        glGenTextures(1, &mTextureID);
    }
    glBindTexture(GL_TEXTURE_2D, mTextureID);

    Adapter2D adapt(getContext(), this);
    for(uint32_t lod = 0; (lod + lodOffset) < mType->getLODCount(); lod++) {
        adapt.setLOD(lod+lodOffset);

        uint16_t * ptr = static_cast<uint16_t *>(adapt.getElement(0,0));
        glTexImage2D(GL_TEXTURE_2D, lod, format,
                     adapt.getDimX(), adapt.getDimY(),
                     0, format, type, ptr);
    }
}

void Allocation::uploadToBufferObject()
{
    rsAssert(!mType->getDimY());
    rsAssert(!mType->getDimZ());

    if (!mBufferID) {
        glGenBuffers(1, &mBufferID);
    }
    glBindBuffer(GL_ARRAY_BUFFER, mBufferID);
    glBufferData(GL_ARRAY_BUFFER, mType->getSizeBytes(), getPtr(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}


void Allocation::data(const void *data, uint32_t sizeBytes)
{
    uint32_t size = mType->getSizeBytes();
    if (size != sizeBytes) {
        LOGE("Allocation::data called with mismatched size expected %i, got %i", size, sizeBytes);
        return;
    }
    memcpy(mPtr, data, size);
    sendDirty();
}

void Allocation::read(void *data)
{
    memcpy(data, mPtr, mType->getSizeBytes());
}

void Allocation::subData(uint32_t xoff, uint32_t count, const void *data, uint32_t sizeBytes)
{
    uint32_t eSize = mType->getElementSizeBytes();
    uint8_t * ptr = static_cast<uint8_t *>(mPtr);
    ptr += eSize * xoff;
    uint32_t size = count * eSize;

    if (size != sizeBytes) {
        LOGE("Allocation::subData called with mismatched size expected %i, got %i", size, sizeBytes);
        mType->dumpLOGV("type info");
        return;
    }
    memcpy(ptr, data, size);
    sendDirty();
}

void Allocation::subData(uint32_t xoff, uint32_t yoff,
             uint32_t w, uint32_t h, const void *data, uint32_t sizeBytes)
{
    uint32_t eSize = mType->getElementSizeBytes();
    uint32_t lineSize = eSize * w;
    uint32_t destW = mType->getDimX();

    const uint8_t *src = static_cast<const uint8_t *>(data);
    uint8_t *dst = static_cast<uint8_t *>(mPtr);
    dst += eSize * (xoff + yoff * destW);

    if ((lineSize * eSize * h) != sizeBytes) {
        rsAssert(!"Allocation::subData called with mismatched size");
        return;
    }

    for (uint32_t line=yoff; line < (yoff+h); line++) {
        uint8_t * ptr = static_cast<uint8_t *>(mPtr);
        memcpy(dst, src, lineSize);
        src += lineSize;
        dst += destW * eSize;
    }
    sendDirty();
}

void Allocation::subData(uint32_t xoff, uint32_t yoff, uint32_t zoff,
             uint32_t w, uint32_t h, uint32_t d, const void *data, uint32_t sizeBytes)
{
}

void Allocation::addProgramToDirty(const Program *p)
{
    mToDirtyList.add(p);
}

void Allocation::removeProgramToDirty(const Program *p)
{
    for (size_t ct=0; ct < mToDirtyList.size(); ct++) {
        if (mToDirtyList[ct] == p) {
            mToDirtyList.removeAt(ct);
            return;
        }
    }
    rsAssert(0);
}

void Allocation::sendDirty() const
{
    for (size_t ct=0; ct < mToDirtyList.size(); ct++) {
        mToDirtyList[ct]->forceDirty();
    }
}

/////////////////
//


namespace android {
namespace renderscript {

RsAllocation rsi_AllocationCreateTyped(Context *rsc, RsType vtype)
{
    const Type * type = static_cast<const Type *>(vtype);

    Allocation * alloc = new Allocation(rsc, type);
    alloc->incUserRef();
    return alloc;
}

RsAllocation rsi_AllocationCreateSized(Context *rsc, RsElement e, size_t count)
{
    Type * type = new Type(rsc);
    type->setDimX(count);
    type->setElement(static_cast<Element *>(e));
    type->compute();
    return rsi_AllocationCreateTyped(rsc, type);
}

void rsi_AllocationUploadToTexture(Context *rsc, RsAllocation va, uint32_t baseMipLevel)
{
    Allocation *alloc = static_cast<Allocation *>(va);
    alloc->uploadToTexture(baseMipLevel);
}

void rsi_AllocationUploadToBufferObject(Context *rsc, RsAllocation va)
{
    Allocation *alloc = static_cast<Allocation *>(va);
    alloc->uploadToBufferObject();
}

static void mip565(const Adapter2D &out, const Adapter2D &in)
{
    uint32_t w = out.getDimX();
    uint32_t h = out.getDimY();

    for (uint32_t y=0; y < h; y++) {
        uint16_t *oPtr = static_cast<uint16_t *>(out.getElement(0, y));
        const uint16_t *i1 = static_cast<uint16_t *>(in.getElement(0, y*2));
        const uint16_t *i2 = static_cast<uint16_t *>(in.getElement(0, y*2+1));

        for (uint32_t x=0; x < w; x++) {
            *oPtr = rsBoxFilter565(i1[0], i1[1], i2[0], i2[1]);
            oPtr ++;
            i1 += 2;
            i2 += 2;
        }
    }
}

static void mip8888(const Adapter2D &out, const Adapter2D &in)
{
    uint32_t w = out.getDimX();
    uint32_t h = out.getDimY();

    for (uint32_t y=0; y < h; y++) {
        uint32_t *oPtr = static_cast<uint32_t *>(out.getElement(0, y));
        const uint32_t *i1 = static_cast<uint32_t *>(in.getElement(0, y*2));
        const uint32_t *i2 = static_cast<uint32_t *>(in.getElement(0, y*2+1));

        for (uint32_t x=0; x < w; x++) {
            *oPtr = rsBoxFilter8888(i1[0], i1[1], i2[0], i2[1]);
            oPtr ++;
            i1 += 2;
            i2 += 2;
        }
    }
}

static void mip(const Adapter2D &out, const Adapter2D &in)
{
    switch(out.getBaseType()->getElement()->getSizeBits()) {
    case 32:
        mip8888(out, in);
        break;
    case 16:
        mip565(out, in);
        break;

    }

}

typedef void (*ElementConverter_t)(void *dst, const void *src, uint32_t count);

static void elementConverter_cpy_16(void *dst, const void *src, uint32_t count)
{
    memcpy(dst, src, count * 2);
}
static void elementConverter_cpy_8(void *dst, const void *src, uint32_t count)
{
    memcpy(dst, src, count);
}
static void elementConverter_cpy_32(void *dst, const void *src, uint32_t count)
{
    memcpy(dst, src, count * 4);
}


static void elementConverter_888_to_565(void *dst, const void *src, uint32_t count)
{
    uint16_t *d = static_cast<uint16_t *>(dst);
    const uint8_t *s = static_cast<const uint8_t *>(src);

    while(count--) {
        *d = rs888to565(s[0], s[1], s[2]);
        d++;
        s+= 3;
    }
}

static void elementConverter_8888_to_565(void *dst, const void *src, uint32_t count)
{
    uint16_t *d = static_cast<uint16_t *>(dst);
    const uint8_t *s = static_cast<const uint8_t *>(src);

    while(count--) {
        *d = rs888to565(s[0], s[1], s[2]);
        d++;
        s+= 4;
    }
}

static ElementConverter_t pickConverter(const Element *dst, const Element *src)
{
    GLenum srcGLType = src->getGLType();
    GLenum srcGLFmt = src->getGLFormat();
    GLenum dstGLType = dst->getGLType();
    GLenum dstGLFmt = dst->getGLFormat();

    if (srcGLFmt == dstGLFmt && srcGLType == dstGLType) {
        switch(dst->getSizeBytes()) {
        case 4:
            return elementConverter_cpy_32;
        case 2:
            return elementConverter_cpy_16;
        case 1:
            return elementConverter_cpy_8;
        }
    }

    if (srcGLType == GL_UNSIGNED_BYTE &&
        srcGLFmt == GL_RGB &&
        dstGLType == GL_UNSIGNED_SHORT_5_6_5 &&
        dstGLType == GL_RGB) {

        return elementConverter_888_to_565;
    }

    if (srcGLType == GL_UNSIGNED_BYTE &&
        srcGLFmt == GL_RGBA &&
        dstGLType == GL_UNSIGNED_SHORT_5_6_5 &&
        dstGLType == GL_RGB) {

        return elementConverter_8888_to_565;
    }

    LOGE("pickConverter, unsuported combo, src %p,  dst %p", src, dst);
    return 0;
}


RsAllocation rsi_AllocationCreateFromBitmap(Context *rsc, uint32_t w, uint32_t h, RsElement _dst, RsElement _src,  bool genMips, const void *data)
{
    const Element *src = static_cast<const Element *>(_src);
    const Element *dst = static_cast<const Element *>(_dst);
    rsAssert(!(w & (w-1)));
    rsAssert(!(h & (h-1)));

    //LOGE("rsi_AllocationCreateFromBitmap %i %i %i %i %i", w, h, dstFmt, srcFmt, genMips);
    rsi_TypeBegin(rsc, _dst);
    rsi_TypeAdd(rsc, RS_DIMENSION_X, w);
    rsi_TypeAdd(rsc, RS_DIMENSION_Y, h);
    if (genMips) {
        rsi_TypeAdd(rsc, RS_DIMENSION_LOD, 1);
    }
    RsType type = rsi_TypeCreate(rsc);

    RsAllocation vTexAlloc = rsi_AllocationCreateTyped(rsc, type);
    Allocation *texAlloc = static_cast<Allocation *>(vTexAlloc);
    if (texAlloc == NULL) {
        LOGE("Memory allocation failure");
        return NULL;
    }
    texAlloc->incUserRef();

    ElementConverter_t cvt = pickConverter(dst, src);
    cvt(texAlloc->getPtr(), data, w * h);

    if (genMips) {
        Adapter2D adapt(rsc, texAlloc);
        Adapter2D adapt2(rsc, texAlloc);
        for(uint32_t lod=0; lod < (texAlloc->getType()->getLODCount() -1); lod++) {
            adapt.setLOD(lod);
            adapt2.setLOD(lod + 1);
            mip(adapt2, adapt);
        }
    }

    return texAlloc;
}

RsAllocation rsi_AllocationCreateFromBitmapBoxed(Context *rsc, uint32_t w, uint32_t h, RsElement _dst, RsElement _src, bool genMips, const void *data)
{
    const Element *srcE = static_cast<const Element *>(_src);
    const Element *dstE = static_cast<const Element *>(_dst);
    uint32_t w2 = rsHigherPow2(w);
    uint32_t h2 = rsHigherPow2(h);

    if ((w2 == w) && (h2 == h)) {
        return rsi_AllocationCreateFromBitmap(rsc, w, h, _dst, _src, genMips, data);
    }

    uint32_t bpp = srcE->getSizeBytes();
    size_t size = w2 * h2 * bpp;
    uint8_t *tmp = static_cast<uint8_t *>(malloc(size));
    memset(tmp, 0, size);

    const uint8_t * src = static_cast<const uint8_t *>(data);
    for (uint32_t y = 0; y < h; y++) {
        uint8_t * ydst = &tmp[(y + ((h2 - h) >> 1)) * w2 * bpp];
        memcpy(&ydst[((w2 - w) >> 1) * bpp], src, w * bpp);
        src += w * bpp;
    }

    RsAllocation ret = rsi_AllocationCreateFromBitmap(rsc, w2, h2, _dst, _src, genMips, tmp);
    free(tmp);
    return ret;
}

void rsi_AllocationData(Context *rsc, RsAllocation va, const void *data, uint32_t sizeBytes)
{
    Allocation *a = static_cast<Allocation *>(va);
    a->data(data, sizeBytes);
}

void rsi_Allocation1DSubData(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t count, const void *data, uint32_t sizeBytes)
{
    Allocation *a = static_cast<Allocation *>(va);
    a->subData(xoff, count, data, sizeBytes);
}

void rsi_Allocation2DSubData(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t yoff, uint32_t w, uint32_t h, const void *data, uint32_t sizeBytes)
{
    Allocation *a = static_cast<Allocation *>(va);
    a->subData(xoff, yoff, w, h, data, sizeBytes);
}

void rsi_AllocationRead(Context *rsc, RsAllocation va, void *data)
{
    Allocation *a = static_cast<Allocation *>(va);
    a->read(data);
}


}
}
