/*
 *  ItextureLoader.c
 *  dEngine
 *
 *  Created by fabien sanglard on 16/08/09.
 *
 */

#include "ItextureLoader.h"
#include "filesystem.h"

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
	
	texture->data = malloc(texture->file->filesize - sizeof(PVRTexHeader)) ;
	memcpy(texture->data, texture->file->ptrStart+sizeof(PVRTexHeader), texture->file->filesize - sizeof(PVRTexHeader));
	
	
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

	flags = pvrHeader->type;
	formatFlags = flags;

    printf("[loadNativePVRT] PVR texture format: 0x%08x\n", formatFlags);

    texture->format = GL_COMPRESSED_RGB565_PVRTC_IMG;
    texture->dataLength = pvrHeader->size * pvrHeader->size * bpp;
}
