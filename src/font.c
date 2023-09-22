/*
 *  font.c
 *  dEngine
 *
 *  Created by fabien sanglard on 17/08/09.
 *
 */

#include "font.h"
#include "renderer.h"

font_t font;

void FM_Init( void )
{
	
			
	FM_createFont("data/font/font1.png.argb4444.pvr");
	
	
	font.nSize = 2;
	

	
}



void FM_createFont( char *filename )
{
	
	char* datname;
	filehandle_t *fp;
	uint size;

	
	font.texfont = (texture_t*)calloc(1, sizeof(texture_t));
	font.texfont->path = (char*)filename;
	
	TEX_LoadTexture(font.texfont);
	renderer.UpLoadTextureToGpu(font.texfont);
	
	if( NULL == font.texfont )
	{
		printf( "[createFont]: unable to open file (%s)\n", filename );
		return ;
	}
	
	memset( font.nCharWidth, 0, sizeof( font.nCharWidth ) );
	
	datname = (char*)calloc(strlen( font.texfont->path ) + 1,sizeof(char) );
	
	FS_StripExtension( (char *)font.texfont->path, datname );
	
	strcat( datname, ".dat");
	
	fp = FS_OpenFile( datname, "rb");
	if( NULL == fp )
	{
		printf( "[createFont]: unable to open file (%s)\n", datname );
		
		free( datname );
		return ;
	}
	
	size = FS_GetFileSize( fp );
	
	// check header size
	if( size < 10 )
	{
		printf( "[createFont]: File (%s) has incorrect file length\n", datname );
		
		free( datname );
		
		FS_CloseFile( fp );
		
		return ;	
	}
	
	
	// Check sig of font dat file
	
	FS_ReadFile( &size, 1, 4, fp );
	
	
	FS_ReadFile( &font.nMaxWidth, 1, 1, fp );
	FS_ReadFile( &font.nMaxHeight, 1, 1, fp );
	
	
	FS_ReadFile( &size, 1, 4, fp );
	//size = LittleLong( size );
	
	if( size > 127 )
	{
		printf( "[createFont]: File (%s) has incorrect Character Width array\n", datname );
		
		free( datname );
		
		FS_CloseFile( fp );
		
		return;	
	}
	
	FS_ReadFile( &font.nCharWidth, 1, size, fp );
	
	FS_CloseFile( fp );
	
	font.nSize = 2;	
	
	
	font.hFrac = (float)(font.nMaxHeight / (float)font.texfont->height);
	font.wFrac = (float)(font.nMaxWidth / (float)font.texfont->width);
	
	//printf("hFrac=%.2f wFrac=%.2f\n",temp_font->hFrac,temp_font->wFrac);
	
	
	free( datname );

}


int WriteVertices( int x, int y, char num, svertex_t* vertices )
{
	//printf("char: '%c' at (%i,%i) ###### \n",num,x,y);
	
	int	row, col; 
	int scale, sh; // scaled width, height
	float frow, fcol;
	
	num &= 255;
	
	if( (num & 127) == 32 )
	{
		return 0;		// space
	}
	
	if( y <= -font.nMaxHeight )
	{
		return 0;			// totally off screen
	}
	
	scale = font.nSize;
	sh = font.nMaxHeight;
	
	row = (num >> 4) - 2;
	col = num & 15;
	
	frow = row * font.hFrac;
	fcol = col * font.wFrac;
	
	//	4 3
	//	1 2
	
	//1
	vertices->text[0] = fcol ;
	vertices->text[1] = frow+font.hFrac; 
	vertices->pos[0] = x;
	vertices->pos[1] = y;
    vertices->pos[2] = 10.0f;
    vertices->col[0] = 255;
    vertices->col[1] = 255;
    vertices->col[2] = 255;
    vertices->col[3] = 255;
	vertices++;
		
	//2
	vertices->text[0] = fcol+font.wFrac;
	vertices->text[1] = frow+font.hFrac;		
	vertices->pos[0] = x+font.nMaxWidth*scale ;
	vertices->pos[1] = y;
    vertices->pos[2] = 10.0f;
    vertices->col[0] = 255;
    vertices->col[1] = 255;
    vertices->col[2] = 255;
    vertices->col[3] = 255;
	vertices++;
	
	//3
	vertices->text[0] = fcol+font.wFrac;
	vertices->text[1] = frow;		
	vertices->pos[0] = x+font.nMaxWidth*scale ;
	vertices->pos[1] = y+sh*scale;
    vertices->pos[2] = 10.0f;
    vertices->col[0] = 255;
    vertices->col[1] = 255;
    vertices->col[2] = 255;
    vertices->col[3] = 255;
	vertices++;

	 
	//4
	vertices->text[0] = fcol;
	vertices->text[1] = frow;		
	vertices->pos[0] = x;
	vertices->pos[1] = y+sh*scale;
    vertices->pos[2] = 10.0f;
    vertices->col[0] = 255;
    vertices->col[1] = 255;
    vertices->col[2] = 255;
    vertices->col[3] = 255;
	vertices++;
	

	
	return 4;
}

ubyte Font_GenerateStringVertexArray(svertex_t* vertices,char* string, int x, int y)
{
	int mx = x;
	unsigned int charindex;	
	unsigned int i;

	uchar numVerticeForString;
	uchar numVerticeForChar;
	
	numVerticeForString = 0 ;
	numVerticeForChar  = 0;
	
	int stringLength = strlen( string );
	
	for ( i = 0; i < stringLength; i++ )
	{
		charindex = stringLength - i - 1;
		
		mx -= font.nCharWidth[ string[ charindex ]-32 ] * font.nSize;
		
		numVerticeForChar = WriteVertices( mx, y, string[ charindex ] ,vertices);
		
		numVerticeForString += numVerticeForChar;
		vertices += numVerticeForChar;
	}


	
	return numVerticeForString;
}
