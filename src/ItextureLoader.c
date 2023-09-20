/*
 *  ItextureLoader.c
 *  dEngine
 *
 *  Created by fabien sanglard on 16/08/09.
 *
 */

#include "ItextureLoader.h"
#include "filesystem.h"
#include "GL/gl.h"
#include "GL/glu.h"
#include "GL/glkos.h"
#include "GL/glext.h"


static char gPVRTexIdentifier[4] = "DTEX";


typedef struct _PVRTexHeader
	{
		unsigned int headerLength;
		unsigned int height;
		unsigned int width;
		unsigned int numMipmaps;
		unsigned int flags;
		unsigned int dataLength;
		unsigned int bpp;
		unsigned int bitmaskRed;
		unsigned int bitmaskGreen;
		unsigned int bitmaskBlue;
		unsigned int bitmaskAlpha;
		unsigned int pvrTag;
		unsigned int numSurfs;
	} PVRTexHeader;

typedef struct _DTEXTexHeader {
    unsigned int magic;
    unsigned short width;
    unsigned short height;
    unsigned int type;
    unsigned int size;
} DTEXTexHeader;

#define kPVRTextureFlagTypePVRTC_2 24
#define kPVRTextureFlagTypePVRTC_4 25
#define PVR_TEXTURE_FLAG_TYPE_MASK	0xff

#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG			0x8C00
#define GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG			0x8C01
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG			0x8C02
#define GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG			0x8C03
#define GL_COMPRESSED_RGB565_PVRTC_IMG              0x8C04

#define MAX(A,B) ((A) > (B))? (A) : (B)



void loadNativePVRT(texture_t* texture)
{
	DTEXTexHeader* pvrHeader;
	unsigned int flags, pvrTag;
	unsigned int formatFlags;

	unsigned int  blockSize = 0, widthBlocks = 0, heightBlocks = 0;
	unsigned int  width = 0, height = 0, bpp = 16;

	texture->file = FS_OpenFile(texture->path,"rb");
	
	if (!texture->file)
	{
		printf("[loadNativePVRT] Could not load: '%s'\n",texture->path);
		return;
	}
	
	pvrHeader = (DTEXTexHeader *)texture->file->ptrStart;
	
	texture->data = malloc(texture->file->filesize - sizeof(DTEXTexHeader)) ;
	memcpy(texture->data, texture->file->ptrStart+sizeof(DTEXTexHeader), texture->file->filesize - sizeof(DTEXTexHeader));
	
	pvrTag = pvrHeader->magic;
	
	//Analysing header magic number
	if (gPVRTexIdentifier[0] != ((pvrTag >>  0) & 0xff) ||
		gPVRTexIdentifier[1] != ((pvrTag >>  8) & 0xff) ||
		gPVRTexIdentifier[2] != ((pvrTag >> 16) & 0xff) ||
		gPVRTexIdentifier[3] != ((pvrTag >> 24) & 0xff))
	{
        printf("[loadNativePVRT] Invalid magic number: 0x%08x (%c%c%c%c)\n", pvrTag, ((pvrTag >>  0) & 0xff), ((pvrTag >>  8) & 0xff), ((pvrTag >>  16) & 0xff), ((pvrTag >>  24) & 0xff));
		return ;
	}

    texture->width = pvrHeader->width;
    texture->height = pvrHeader->height;

    GLboolean twiddled = (pvrHeader->type & (1 << 25)) < 1;
    GLboolean compressed = (pvrHeader->type & (1 << 29)) > 0;
    GLboolean mipmapped = (pvrHeader->type & (1 << 30)) > 0;
    GLboolean strided = (pvrHeader->type & (1 << 24)) > 0;


    flags = pvrHeader->type;
	formatFlags = (pvrHeader->type >> 27) & 0b111;

    GLuint COMPRESSED_MASK = 4;
    GLuint TWIDDLED_MASK = 2;
    GLuint MIPMAPPED_MASK = 1;

    GLuint lookup[8] = {0};

    switch(formatFlags) {
        case 0:
            lookup[COMPRESSED_MASK] = GL_COMPRESSED_ARGB_1555_VQ_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK] = GL_COMPRESSED_ARGB_1555_VQ_TWID_KOS;
            lookup[COMPRESSED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_ARGB_1555_VQ_MIPMAP_TWID_KOS;
            lookup[TWIDDLED_MASK] = GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS;
            lookup[TWIDDLED_MASK | MIPMAPPED_MASK] = GL_UNSIGNED_SHORT_1_5_5_5_REV_TWID_KOS;
            lookup[0] = GL_UNSIGNED_SHORT_1_5_5_5_REV;
            break;
        case 1:
            lookup[COMPRESSED_MASK] = GL_COMPRESSED_RGB_565_VQ_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK] = GL_COMPRESSED_RGB_565_VQ_TWID_KOS;
            lookup[COMPRESSED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_RGB_565_VQ_MIPMAP_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_RGB_565_VQ_MIPMAP_TWID_KOS;
            lookup[TWIDDLED_MASK] = GL_UNSIGNED_SHORT_5_6_5_TWID_KOS;
            lookup[TWIDDLED_MASK | MIPMAPPED_MASK] = GL_UNSIGNED_SHORT_5_6_5_TWID_KOS;
            lookup[0] = GL_UNSIGNED_SHORT_5_6_5;
            break;
        case 2:
            lookup[COMPRESSED_MASK] = GL_COMPRESSED_ARGB_4444_VQ_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK] = GL_COMPRESSED_ARGB_4444_VQ_TWID_KOS;
            lookup[COMPRESSED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_KOS;
            lookup[COMPRESSED_MASK | TWIDDLED_MASK | MIPMAPPED_MASK] = GL_COMPRESSED_ARGB_4444_VQ_MIPMAP_TWID_KOS;
            lookup[TWIDDLED_MASK] = GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS;
            lookup[TWIDDLED_MASK | MIPMAPPED_MASK] = GL_UNSIGNED_SHORT_4_4_4_4_REV_TWID_KOS;
            lookup[0] = GL_UNSIGNED_SHORT_4_4_4_4_REV;
            break;
        default:
            printf("[ERROR] Unknown format\n");
    }


    printf("[loadNativePVRT] PVR texture format: 0x%08x\n", formatFlags);

    texture->format = (formatFlags == 1) ? GL_RGB : GL_BGRA;
    texture->internal_format = (formatFlags == 1) ? GL_RGB : GL_RGBA;
    texture->type = lookup[(compressed << 2) | (twiddled << 1) | mipmapped];
    texture->dataLength = pvrHeader->size * pvrHeader->size * bpp;
}
