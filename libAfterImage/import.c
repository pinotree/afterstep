/* This file contains code for unified image loading from many file formats */
/********************************************************************/
/* Copyright (c) 2001 Sasha Vasko <sashav@sprintmail.com>           */
/********************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

/*#define LOCAL_DEBUG */
/*#define DO_CLOCKING */

#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <malloc.h>
/* <setjmp.h> is used for the optional error recovery mechanism */

#ifdef HAVE_PNG
/* Include file for users of png library. */
#include <png.h>
#else
#include <setjmp.h>
#endif

#include "afterbase.h"
#ifdef HAVE_JPEG
/* Include file for users of png library. */
#include <jpeglib.h>
#endif
#ifdef HAVE_GIF
#include <gif_lib.h>
#endif
#ifdef HAVE_TIFF
#include <tiff.h>
#include <tiffio.h>
#endif
#ifdef HAVE_LIBXPM
#ifdef HAVE_LIBXPM_X11
#include <X11/xpm.h>
#else
#include <xpm.h>
#endif
#endif

#include "asimage.h"
#include "xcf.h"
#include "xpm.h"
#include "import.h"


/***********************************************************************************/
/* High level interface : 														   */
static char *locate_image_file( const char *file, char **paths );
static ASImageFileTypes	check_image_type( const char *realfilename );

as_image_loader_func as_image_file_loaders[ASIT_Unknown] =
{
	xpm2ASImage ,
	xpm2ASImage ,
	xpm2ASImage ,
	png2ASImage ,
	jpeg2ASImage,
	xcf2ASImage ,
	ppm2ASImage ,
	ppm2ASImage ,
	bmp2ASImage ,
	ico2ASImage ,
	ico2ASImage ,
	gif2ASImage ,
	tiff2ASImage,
	NULL,
	NULL,
	NULL
};

ASImage *
file2ASImage( const char *file, ASFlagType what, double gamma, unsigned int compression, ... )
{
	int 		  filename_len ;
	int 		  subimage = -1 ;
	char 		 *realfilename = NULL, *tmp = NULL ;
	va_list       ap;
	char 		 *paths[8] ;
	register int i;

	ASImage *im = NULL;
	CARD8 *gamma_table = NULL;

	if( file == NULL ) return NULL;

	filename_len = strlen(file);

	va_start (ap, compression);
	for( i = 0 ; i < 8 ; i++ )
		if( (paths[i] = va_arg(ap,char*)) == NULL )
			break;
	paths[7] = NULL ;
	va_end (ap);

	/* first lets try to find file as it is */
	if( (realfilename = locate_image_file(file,&(paths[0]))) == NULL )
	{
		tmp = safemalloc( filename_len+3+1);
		strcpy(tmp, file);
	}
	if( realfilename == NULL )
	{ /* let's try and see if appending .gz will make any difference */
		strcpy(&(tmp[filename_len]), ".gz");
		realfilename = locate_image_file(tmp,&(paths[0]));
	}
	if( realfilename == NULL )
	{ /* let's try and see if appending .Z will make any difference */
		strcpy(&(tmp[filename_len]), ".Z");
		realfilename = locate_image_file(tmp,&(paths[0]));
	}
	if( realfilename == NULL )
	{ /* let's try and see if we have subimage number appended */
		for( i = filename_len-1 ; i > 0; i-- )
			if( !isdigit( tmp[i] ) )
				break;
		if( i < filename_len-1 && i > 0 )
			if( tmp[i] == '.' )                 /* we have possible subimage number */
			{
				subimage = atoi( &tmp[i+1] );
				tmp[i] = '\0';
				filename_len = i ;
				realfilename = locate_image_file(tmp,&(paths[0]));
				if( realfilename == NULL )
				{ /* let's try and see if appending .gz will make any difference */
					strcpy(&(tmp[filename_len]), ".gz");
					realfilename = locate_image_file(tmp,&(paths[0]));
				}
				if( realfilename == NULL )
				{ /* let's try and see if appending .Z will make any difference */
					strcpy(&(tmp[filename_len]), ".Z");
					realfilename = locate_image_file(tmp,&(paths[0]));
				}
			}
	}
	if( tmp != realfilename && tmp != NULL )
		free( tmp );
	if( realfilename )
	{
		ASImageFileTypes file_type = check_image_type( realfilename );
		if( file_type == ASIT_Unknown )
			show_error( "Unknown format of the image file \"%s\". Please check the manual", realfilename );
		else if( as_image_file_loaders[file_type] )
			im = as_image_file_loaders[file_type](realfilename, what, gamma, gamma_table, subimage, compression);
		else
			show_error( "Support for the format of image file \"%s\" has not been implemented yet.", realfilename );

		if( realfilename != file )
			free( realfilename );
	}
	return im;
}

Pixmap
file2pixmap(ASVisual *asv, Window root, const char *realfilename, Pixmap *mask_out)
{
	Pixmap trg = None, mask = None;

	if( asv && realfilename )
	{
		double gamma = SCREEN_GAMMA;
		char  *gamma_str;
		ASImage *im = NULL;

		if ((gamma_str = getenv ("SCREEN_GAMMA")) != NULL)
		{
			gamma = atof (gamma_str);
			if (gamma == 0.0)
				gamma = SCREEN_GAMMA;
		}

		im = file2ASImage( realfilename, 0xFFFFFFFF, gamma, 0, NULL );

		if( im != NULL )
		{
			trg = asimage2pixmap( asv, root, im, NULL, False );
			if( mask_out )
			{
				register int i ;
				for( i = 0 ; i < im->height ; i++ )
					if( im->alpha[i] != NULL )
					{
						mask = asimage2mask( asv, root, im, NULL, False );
						break;
					}
			}
			destroy_asimage( &im );
		}
	}
	if( mask_out )
	{
		if( *mask_out )
			XFreePixmap( asv->dpy, *mask_out );
		*mask_out = mask ;
	}
	return trg ;
}

/***********************************************************************************/
/* Some helper functions :                                                         */

static char *
locate_image_file( const char *file, char **paths )
{
	char *realfilename = NULL;
	if( CheckFile( file ) == 0 )
	{
		realfilename = (char*)file;
	}else
	{	/* now lets try and find the file in any of the optional paths :*/
		register int i = 0;
		do
		{
fprintf( stderr, "%p , paths[%d] = %s\n", paths, i, paths[i] );
			realfilename = find_file( file, paths[i], R_OK );
		}while( paths[i++] != NULL );
	}
	return realfilename;
}

static FILE*
open_image_file( const char *path )
{
	FILE *fp = NULL;
	if ( path )
		if ((fp = fopen (path, "rb")) == NULL)
			show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
	return fp ;
}

static ASImageFileTypes
check_image_type( const char *realfilename )
{
	int filename_len = strlen( realfilename );
	CARD8 head[16] ;
	int bytes_in = 0 ;
	FILE *fp ;
	/* lets check if we have compressed xpm file : */
	if( filename_len > 6 && mystrncasecmp( realfilename+filename_len-3, "xpm.gz", 6 ) == 0 )
		return ASIT_GZCompressedXpm;
	if( filename_len > 5 && mystrncasecmp( realfilename+filename_len-3, "xpm.Z", 5 ) == 0 )
		return ASIT_ZCompressedXpm;
	if( (fp = open_image_file( realfilename )) != NULL )
	{
		bytes_in = fread( &(head[0]), sizeof(char), 16, fp );
		head[15] = '\0' ;
		DEBUG_OUT("%s: head[0]=0x%2.2X(%d),head[2]=0x%2.2X(%d)\n", realfilename+filename_len-4, head[0], head[0], head[2], head[2] );
/*		fprintf( stderr, " IMAGE FILE HEADER READS : [%s][%c%c%c%c%c%c%c%c][%s], bytes_in = %d\n", (char*)&(head[0]),
						head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7], strstr ((char *)&(head[0]), "XPM"),bytes_in );
 */		fclose( fp );
		if( bytes_in > 3 )
		{
			if( head[0] == 0xff && head[1] == 0xd8 && head[2] == 0xff)
				return ASIT_Jpeg;
			else if (strstr ((char *)&(head[0]), "XPM") != NULL)
				return ASIT_Xpm;
			else if (head[1] == 'P' && head[2] == 'N' && head[3] == 'G')
				return ASIT_Png;
			else if (head[0] == 'G' && head[1] == 'I' && head[2] == 'F')
				return ASIT_Gif;
			else if (head[0] == head[1] && (head[0] == 'I' || head[0] == 'M'))
				return ASIT_Tiff;
			else if (head[0] == 'P' && isdigit(head[1]))
				return (head[1]!='5' && head[1]!='6')?ASIT_Pnm:ASIT_Ppm;
			else if (head[0] == 0xa && head[1] <= 5 && head[2] == 1)
				return ASIT_Pcx;
			else if (head[0] == 'B' && head[1] == 'M')
				return ASIT_Bmp;
			else if (head[0] == 0 && head[2] == 1 && mystrncasecmp(realfilename+filename_len-4, ".ICO", 4)==0 )
				return ASIT_Ico;
			else if (head[0] == 0 && head[2] == 2 &&
						(mystrncasecmp(realfilename+filename_len-4, ".CUR", 4)==0 ||
						 mystrncasecmp(realfilename+filename_len-4, ".ICO", 4)==0) )
				return ASIT_Cur;
		}
		if( bytes_in  > 8 )
		{
			if( strncmp((char *)&(head[0]), XCF_SIGNATURE, (size_t) XCF_SIGNATURE_LEN) == 0)
				return ASIT_Xcf;
	   		else if (head[0] == 0 && head[1] == 0 &&
			    	 head[2] == 2 && head[3] == 0 && head[4] == 0 && head[5] == 0 && head[6] == 0 && head[7] == 0)
				return ASIT_Targa;
			else if (strncmp ((char *)&(head[0]), "#define", (size_t) 7) == 0)
				return ASIT_Xbm;
		}
	}
	return ASIT_Unknown;
}

static void
raw2scanline( register CARD8 *row, ASScanline *buf, CARD8 *gamma_table, unsigned int width, Bool grayscale, Bool do_alpha )
{
	register int x = width;

	if( grayscale )
		row += do_alpha? width<<1 : width ;
	else
		row += width*(do_alpha?4:3) ;

	if( gamma_table )
	{
		if( !grayscale )
		{
			while ( --x >= 0 )
			{
				row -= 3 ;
				if( do_alpha )
				{
					--row;
					buf->alpha[x] = row[3];
				}
				buf->xc1[x]  = gamma_table[row[0]];
				buf->xc2[x]= gamma_table[row[1]];
				buf->xc3[x] = gamma_table[row[2]];
			}
		}else /* greyscale */
			while ( --x >= 0 )
			{
				if( do_alpha )
					buf->alpha[x] = *(--row);
				buf->xc1 [x] = buf->xc2[x] = buf->xc3[x]  = gamma_table[*(--row)];
			}
	}else
	{
		if( !grayscale )
		{
			while ( --x >= 0 )
			{
				row -= 3 ;
				if( do_alpha )
				{
					--row;
					buf->alpha[x] = row[3];
				}
				buf->xc1[x]  = row[0];
				buf->xc2[x]= row[1];
				buf->xc3[x] = row[2];
			}
		}else /* greyscale */
			while ( --x >= 0 )
			{
				if( do_alpha )
					buf->alpha[x] = *(--row);
				buf->xc1 [x] = buf->xc2[x] = buf->xc3[x]  = *(--row);
			}
	}
}

/***********************************************************************************/
#ifdef HAVE_XPM      /* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */

#ifdef LOCAL_DEBUG
Bool print_component( CARD32*, int, unsigned int );
#endif

ASImage *
xpm2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	ASXpmFile *xpm_file = NULL;
	ASImage *im = NULL ;
	int line = 0;

	LOCAL_DEBUG_CALLER_OUT ("(\"%s\", 0x%lX)", path, what);
	if( (xpm_file=open_xpm_file(path)) == NULL )
	{
		show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
		return NULL;
	}

	LOCAL_DEBUG_OUT( "do_alpha is %d. im->height = %d, im->width = %d", xpm_file->do_alpha, xpm_file->height, xpm_file->width );
	if( build_xpm_colormap( xpm_file ) )
		if( (im = create_xpm_image( xpm_file, compression )) != NULL )
		{
			for( line = 0 ; line < xpm_file->height ; ++line )
			{
				if( !convert_xpm_scanline( xpm_file, line ) )
					break;
				asimage_add_line (im, IC_RED,   xpm_file->scl.red, line);
				asimage_add_line (im, IC_GREEN, xpm_file->scl.green, line);
				asimage_add_line (im, IC_BLUE,  xpm_file->scl.blue, line);
				if( xpm_file->do_alpha )
					asimage_add_line (im, IC_ALPHA,  xpm_file->scl.alpha, line);
#ifdef LOCAL_DEBUG
				printf( "%d: \"%s\"\n",  line, xpm_file->str_buf );
				print_component( xpm_file->scl.red, 0, xpm_file->width );
				print_component( xpm_file->scl.green, 0, xpm_file->width );
				print_component( xpm_file->scl.blue, 0, xpm_file->width );
#endif
			}
		}
	close_xpm_file( &xpm_file );

#ifdef DO_CLOCKING
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im;
}

#else  			/* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */

ASImage *
xpm2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	show_error( "unable to load file \"%s\" - XPM image format is not supported.\n", path );
	return NULL ;
}

#endif 			/* XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM XPM */
/***********************************************************************************/

/***********************************************************************************/
#ifdef HAVE_PNG		/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
ASImage *
png2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	static ASImage 	 *im = NULL ;

	FILE 		 *fp ;
	double 		  image_gamma = 1.0;
	png_structp   png_ptr;
	png_infop     info_ptr;
	png_uint_32   width, height;
	int           bit_depth, color_type, interlace_type;
	int           intent;
	ASScanline    buf;
	Bool 		  do_alpha = False, grayscale = False ;
	png_bytep     *row_pointers, row;
	unsigned int  y;
	size_t		  row_bytes, offset ;


	im = NULL ;

	if ((fp = open_image_file(path)) == NULL)
		return NULL;

	/* Create and initialize the png_struct with the desired error handler
	 * functions.  If you want to use the default stderr and longjump method,
	 * you can supply NULL for the last three parameters.  We also supply the
	 * the compiler header file version, so that we know if the application
	 * was compiled with a compatible version of the library.  REQUIRED
	 */
	if((png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) != NULL )
	{
		/* Allocate/initialize the memory for image information.  REQUIRED. */
		if( (info_ptr = png_create_info_struct (png_ptr)) != NULL )
		{
		  	/* Set error handling if you are using the setjmp/longjmp method (this is
			 * the normal method of doing things with libpng).  REQUIRED unless you
			 * set up your own error handlers in the png_create_read_struct() earlier.
			 */
			if ( !setjmp (png_ptr->jmpbuf))
			{
				png_init_io (png_ptr, fp);
		    	png_read_info (png_ptr, info_ptr);
				png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

				if (bit_depth < 8)
				{/* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
				  * byte into separate bytes (useful for paletted and grayscale images).
				  */
					png_set_packing (png_ptr);
				}else if (bit_depth == 16)
				{/* tell libpng to strip 16 bit/color files down to 8 bits/color */
					png_set_strip_16 (png_ptr);
				}
				bit_depth = 8;

				/* Expand paletted colors into true RGB triplets */
				if (color_type == PNG_COLOR_TYPE_PALETTE)
				{
					png_set_expand (png_ptr);
					color_type = PNG_COLOR_TYPE_RGB;
				}

				/* Expand paletted or RGB images with transparency to full alpha channels
				 * so the data will be available as RGBA quartets.
		 		 */
   				if( color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY )
   				{
				   	if( png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
					{
						png_set_expand(png_ptr);
						color_type |= PNG_COLOR_MASK_ALPHA;
					}
   				}else
				{
					png_set_filler( png_ptr, 0xFF, PNG_FILLER_AFTER );
					color_type |= PNG_COLOR_MASK_ALPHA;
				}

/*				if( color_type == PNG_COLOR_TYPE_RGB )
					color_type = PNG_COLOR_TYPE_RGB_ALPHA ;
   				else
					color_type = PNG_COLOR_TYPE_GRAY_ALPHA ;
  */
				if (png_get_sRGB (png_ptr, info_ptr, &intent))
					png_set_sRGB (png_ptr, info_ptr, image_gamma);
				else if (png_get_gAMA (png_ptr, info_ptr, &image_gamma))
					png_set_gamma (png_ptr, gamma, image_gamma);
				else
					png_set_gamma (png_ptr, gamma, 1.0);

				/* Optional call to gamma correct and add the background to the palette
				 * and update info structure.  REQUIRED if you are expecting libpng to
				 * update the palette for you (ie you selected such a transform above).
				 */
				png_read_update_info (png_ptr, info_ptr);

				im = safecalloc( 1, sizeof( ASImage ) );
				asimage_start( im, width, height, compression );
				prepare_scanline( im->width, 0, &buf, False );
				do_alpha = ((color_type & PNG_COLOR_MASK_ALPHA) != 0 );
				grayscale = (color_type == PNG_COLOR_TYPE_GRAY_ALPHA) ;

				row_bytes = png_get_rowbytes (png_ptr, info_ptr);
				/* allocating big chunk of memory at once, to enable mmap
				 * that will release memory to system right after free() */
				row_pointers = safemalloc( height * sizeof( png_bytep ) + row_bytes * height );
				row = (png_bytep)(row_pointers + height) ;
				for (offset = 0, y = 0; y < height; y++, offset += row_bytes)
					row_pointers[y] = row + offset;

				/* The easiest way to read the image: */
				png_read_image (png_ptr, row_pointers);
				for (y = 0; y < height; y++)
				{
					register int i;
/*					for ( i = 0 ; i < row_bytes ; ++i)
						fprintf( stderr, "%2.2X ", row_pointers[y][i] );
					fprintf( stderr, " do_alpha = %d\n", do_alpha);
 */
					raw2scanline( row_pointers[y], &buf, NULL, buf.width, grayscale, do_alpha );
					asimage_add_line (im, IC_RED,   buf.red, y);
					asimage_add_line (im, IC_GREEN, buf.green, y);
					asimage_add_line (im, IC_BLUE,  buf.blue, y);
					if( do_alpha )
						for ( i = 0 ; i < buf.width ; ++i)
							if( buf.alpha[i] != 0x00FF )
							{
								asimage_add_line (im, IC_ALPHA,  buf.alpha, y);
								break;
							}
				}
				free (row_pointers);
				free_scanline(&buf, True);
				/* read rest of file, and get additional chunks in info_ptr - REQUIRED */
				png_read_end (png_ptr, info_ptr);
		  	}
		}
		/* clean up after the read, and free any memory allocated - REQUIRED */
		png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp) NULL);
		if (info_ptr)
			free (info_ptr);
	}
	/* close the file */
	fclose (fp);
	return im ;
}
#else 			/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
ASImage *
png2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	show_error( "unable to load file \"%s\" - PNG image format is not supported.\n", path );
	return NULL ;
}

#endif 			/* PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG PNG */
/***********************************************************************************/


/***********************************************************************************/
#ifdef HAVE_JPEG     /* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
struct my_error_mgr
{
	struct jpeg_error_mgr pub;				   /* "public" fields */
	jmp_buf       setjmp_buffer;			   /* for return to caller */
};
typedef struct my_error_mgr *my_error_ptr;

METHODDEF (void)
my_error_exit (j_common_ptr cinfo)
{
	/* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
	my_error_ptr  myerr = (my_error_ptr) cinfo->err;
	/* Always display the message. */
	/* We could postpone this until after returning, if we chose. */
	(*cinfo->err->output_message) (cinfo);
	/* Return control to the setjmp point */
	longjmp (myerr->setjmp_buffer, 1);
}

ASImage *
jpeg2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	ASImage *im ;
	/* This struct contains the JPEG decompression parameters and pointers to
	 * working space (which is allocated as needed by the JPEG library).
	 */
	struct jpeg_decompress_struct cinfo;
	/* We use our private extension JPEG error handler.
	 * Note that this struct must live as long as the main JPEG parameter
	 * struct, to avoid dangling-pointer problems.
	 */
	struct my_error_mgr jerr;
	/* More stuff */
	FILE         *infile;					   /* source file */
	JSAMPARRAY    buffer;					   /* Output row buffer */
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	ASScanline    buf;
	int y;
 /*	register int i ;*/

	/* we want to open the input file before doing anything else,
	 * so that the setjmp() error recovery below can assume the file is open.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to read binary files.
	 */

	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	/* Step 1: allocate and initialize JPEG decompression object */
	/* We set up the normal JPEG error routines, then override error_exit. */
	cinfo.err = jpeg_std_error (&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp (jerr.setjmp_buffer))
	{
		/* If we get here, the JPEG code has signaled an error.
		   * We need to clean up the JPEG object, close the input file, and return.
		 */
		jpeg_destroy_decompress (&cinfo);
		fclose (infile);
		return NULL;
	}
	/* Now we can initialize the JPEG decompression object. */
	jpeg_create_decompress (&cinfo);
	/* Step 2: specify data source (eg, a file) */
	jpeg_stdio_src (&cinfo, infile);
	/* Step 3: read file parameters with jpeg_read_header() */
	(void)jpeg_read_header (&cinfo, TRUE);
	/* We can ignore the return value from jpeg_read_header since
	 *   (a) suspension is not possible with the stdio data source, and
	 *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
	 * See libjpeg.doc for more info.
	 */

	/* Step 4: set parameters for decompression */
	/* Adjust default decompression parameters */
	cinfo.quantize_colors = FALSE;		       /* we don't want no stinking colormaps ! */
	cinfo.output_gamma = gamma;
	/* Step 5: Start decompressor */
	(void)jpeg_start_decompress (&cinfo);
	LOCAL_DEBUG_OUT("stored image size %dx%d", cinfo.output_width,  cinfo.output_height);

	im = safecalloc( 1, sizeof( ASImage ) );
	asimage_start( im, cinfo.output_width,  cinfo.output_height, compression );
	prepare_scanline( im->width, 0, &buf, False );

	/* Make a one-row-high sample array that will go away when done with image */
	buffer =(*cinfo.mem->alloc_sarray)((j_common_ptr) & cinfo, JPOOL_IMAGE,
										cinfo.output_width * cinfo.output_components, 1);

	/* Step 6: while (scan lines remain to be read) */
#ifdef DO_CLOCKING
		printf (" loading initialization time (clocks): %lu\n", clock () - started);
#endif
	y = -1 ;
	/*cinfo.output_scanline*/
/*	for( i = 0 ; i < im->width ; i++ )	fprintf( stderr, "%3.3d    ", i );
	fprintf( stderr, "\n");
 */
 	while ( ++y < cinfo.output_height )
	{
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could ask for
		 * more than one scanline at a time if that's more convenient.
		 */
		(void)jpeg_read_scanlines (&cinfo, buffer, 1);
		raw2scanline( buffer[0], &buf, gamma_table, im->width, (cinfo.output_components==1), False);
/*		fprintf( stderr, "src:");
		for( i = 0 ; i < im->width ; i++ )
			fprintf( stderr, "%2.2X%2.2X%2.2X ", buffer[0][i*3], buffer[0][i*3+1], buffer[0][i*3+2] );
		fprintf( stderr, "\ndst:");
		for( i = 0 ; i < im->width ; i++ )
			fprintf( stderr, "%2.2X%2.2X%2.2X ", buf.red[i], buf.green[i], buf.blue[i] );
		fprintf( stderr, "\n");
 */
		asimage_add_line (im, IC_RED,   buf.red  , y);
		asimage_add_line (im, IC_GREEN, buf.green, y);
		asimage_add_line (im, IC_BLUE,  buf.blue , y);
	}
	free_scanline(&buf, True);
#ifdef DO_CLOCKING
		printf ("\n read time (clocks): %lu\n", clock () - started);
#endif

	/* Step 7: Finish decompression */
	/* we must abort the decompress if not all lines were read */
	if (cinfo.output_scanline < cinfo.output_height)
		jpeg_abort_decompress (&cinfo);
	else
		(void)jpeg_finish_decompress (&cinfo);
	/* We can ignore the return value since suspension is not possible
	 * with the stdio data source.
	 */
	/* Step 8: Release JPEG decompression object */
	/* This is an important step since it will release a good deal of memory. */
	jpeg_destroy_decompress (&cinfo);
	/* After finish_decompress, we can close the input file.
	 * Here we postpone it until after no more JPEG errors are possible,
	 * so as to simplify the setjmp error logic above.  (Actually, I don't
	 * think that jpeg_destroy can do an error exit, but why assume anything...)
	 */
	fclose (infile);
	/* At this point you may want to check to see whether any corrupt-data
	 * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
	 */
#ifdef DO_CLOCKING
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im ;
}
#else 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
ASImage *
jpeg2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	show_error( "unable to load file \"%s\" - JPEG image format is not supported.\n", path );
	return NULL ;
}

#endif 			/* JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG JPEG */
/***********************************************************************************/

/***********************************************************************************/
/* XCF - GIMP's native file format : 											   */

ASImage *
xcf2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	XcfImage  *xcf_im;

	/* we want to open the input file before doing anything else,
	 * so that the setjmp() error recovery below can assume the file is open.
	 * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
	 * requires it in order to read binary files.
	 */
	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	xcf_im = read_xcf_image( infile );
	fclose( infile );

	if( xcf_im == NULL )
		return NULL;

	LOCAL_DEBUG_OUT("stored image size %ldx%ld", xcf_im->width,  xcf_im->height);
#ifdef LOCAL_DEBUG
	print_xcf_image( xcf_im );
#endif
	{/* TODO : temporary workaround untill we implement layers merging */
		XcfLayer *layer = xcf_im->layers ;
		while ( layer )
		{
			if( layer->hierarchy )
				if( layer->hierarchy->image )
					if( layer->hierarchy->width == xcf_im->width &&
						layer->hierarchy->height == xcf_im->height )
					{
						im = layer->hierarchy->image ;
						layer->hierarchy->image = NULL ;
					}
			layer = layer->next ;
		}
	}
	/* Make a one-row-high sample array that will go away when done with image */
#ifdef DO_CLOCKING
		printf (" loading initialization time (clocks): %lu\n", clock () - started);
#endif
#if 0
	im = safecalloc( 1, sizeof( ASImage ) );
	asimage_start( im, xcf_im->width,  xcf_im->height, compression );
	prepare_scanline( im->width, 0, &buf, False );

	y = -1 ;
	/*cinfo.output_scanline*/
	while ( ++y < height )
	{
		register int x = im->width ;
		register JSAMPROW row ;
		/* jpeg_read_scanlines expects an array of pointers to scanlines.
		 * Here the array is only one element long, but you could ask for
		 * more than one scanline at a time if that's more convenient.
		 */
		(void)jpeg_read_scanlines (&cinfo, buffer, 1);
		raw2scanline( buffer[0], &buf, gamma_table, im->width, (cinfo.output_components==3), False);
		asimage_add_line (im, IC_RED,   buf.red  , y);
		asimage_add_line (im, IC_GREEN, buf.green, y);
		asimage_add_line (im, IC_BLUE,  buf.blue , y);
	}
	free_scanline(&buf, True);
#endif

	free_xcf_image(xcf_im);

#ifdef DO_CLOCKING
	printf ("\n read time (clocks): %lu\n", clock () - started);
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im ;
}

/***********************************************************************************/
/* PPM/PNM file format : 											   				   */
ASImage *
ppm2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	ASScanline    buf;
	int y;
	unsigned int type = 0, width = 0, height = 0, colors = 0;
#define PPM_BUFFER_SIZE 71                     /* Sun says that no line should be longer then this */
	char buffer[PPM_BUFFER_SIZE];

	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	if( fgets( &(buffer[0]), PPM_BUFFER_SIZE, infile ) )
	{
		if( buffer[0] == 'P' )
			switch( buffer[1] )
			{    /* we only support RAWBITS formats : */
					case '5' : 	type= 5 ; break ;
					case '6' : 	type= 6 ; break ;
					case '8' : 	type= 8 ; break ;
				default:
					show_error( "invalid or unsupported PPM/PNM file format in image file \"%s\"", path );
			}
		if( type > 0 )
		{
			while ( fgets( &(buffer[0]), PPM_BUFFER_SIZE, infile ) )
			{
				if( buffer[0] != '#' )
				{
					register int i = 0;
					if( width > 0 )
					{
						colors = atoi(&(buffer[i]));
						break;
					}
					width = atoi( &(buffer[i]) );
					while ( buffer[i] != '\0' && !isspace(buffer[i]) ) ++i;
					while ( isspace(buffer[i]) ) ++i;
					if( buffer[i] != '\0')
						height = atoi(&(buffer[i]));
				}
			}
		}
	}

	if( type > 0 && colors <= 255 &&
		width > 0 && width < MAX_IMPORT_IMAGE_SIZE &&
		height > 0 && height < MAX_IMPORT_IMAGE_SIZE )
	{
		CARD8 *data ;
		size_t row_size = width * ((type==6)?3:((type==8)?4:1));

		data = safemalloc( row_size );

		LOCAL_DEBUG_OUT("stored image size %dx%d", width,  height);
		im = safecalloc( 1, sizeof( ASImage ) );
		asimage_start( im, width,  height, compression );
		prepare_scanline( im->width, 0, &buf, False );
		y = -1 ;
		/*cinfo.output_scanline*/
		while ( ++y < height )
		{
			if( fread( data, sizeof (char), row_size, infile ) < row_size )
				break;

			raw2scanline( data, &buf, gamma_table, im->width, (type==5), (type==8));

			asimage_add_line (im, IC_RED,   buf.red  , y);
			asimage_add_line (im, IC_GREEN, buf.green, y);
			asimage_add_line (im, IC_BLUE,  buf.blue , y);
			if( type == 8 )
				asimage_add_line (im, IC_ALPHA,   buf.alpha  , y);
		}
		free_scanline(&buf, True);
		free( data );
	}
	fclose( infile );
#ifdef DO_CLOCKING
	printf ("\n read time (clocks): %lu\n", clock () - started);
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im ;
}

/***********************************************************************************/
/* Windows BMP file format :   									   				   */
static size_t
bmp_read32 (FILE *fp, CARD32 *data, int count)
{
  	size_t total = count;
	if( count > 0 )
	{
#ifdef WORDS_BIGENDIAN                         /* BMPs are encoded as Little Endian */
		CARD8 *raw = (CARD8*)data ;
#endif
		total = fread((char*) data, sizeof (CARD8), count<<2, fp)>>2;
		count = 0 ;
#ifdef WORDS_BIGENDIAN                         /* BMPs are encoded as Little Endian */
		while( count < total )
		{
			data[count] = (raw[0]<<24)|(raw[1]<<16)|(raw[2]<<8)|raw[3];
			++count ;
			raw += 4 ;
		}
#endif
	}
	return total;
}

static size_t
bmp_read16 (FILE *fp, CARD16 *data, int count)
{
  	size_t total = count;
	if( count > 0 )
	{
#ifdef WORDS_BIGENDIAN                         /* BMPs are encoded as Little Endian */
		CARD8 *raw = (CARD8*)data ;
#endif
		total = fread((char*) data, sizeof (CARD8), count<<1, fp)>>1;
		count = 0 ;
#ifdef WORDS_BIGENDIAN                         /* BMPs are encoded as Little Endian */
		while( count < total )
		{
			data[count] = (raw[0]<<16)|raw[1];
			++count ;
			raw += 2 ;
		}
#endif
	}
	return total;
}

typedef struct tagBITMAPFILEHEADER {
#define BMP_SIGNATURE		0x4D42             /* "BM" */
		CARD16  bfType;
        CARD32  bfSize;
        CARD32  bfReserved;
        CARD32  bfOffBits;
} BITMAPFILEHEADER;

typedef struct tagBITMAPINFOHEADER
	{
	   CARD32 biSize;
	   CARD32 biWidth,  biHeight;
	   CARD16 biPlanes, biBitCount;
	   CARD32 biCompression;
	   CARD32 biSizeImage;
	   CARD32 biXPelsPerMeter, biYPelsPerMeter;
	   CARD32 biClrUsed, biClrImportant;
}BITMAPINFOHEADER;

ASImage *
read_bmp_image( FILE *infile, size_t data_offset, BITMAPINFOHEADER *bmp_info,
				ASScanline *buf, CARD8 *gamma_table,
				unsigned int width, unsigned int height,
				Bool add_colormap, unsigned int compression )
{
	Bool success = False ;
	CARD8 *cmap = NULL ;
	int cmap_entries = 0, cmap_entry_size = 4, row_size ;
	int y;
	ASImage *im = NULL ;
	CARD8 *data ;
	int direction = -1 ;

	if( bmp_read32( infile, &bmp_info->biSize, 1 ) )
	{
		if( bmp_info->biSize == 40 )
		{/* long header */
			bmp_read32( infile, &bmp_info->biWidth, 2 );
			bmp_read16( infile, &bmp_info->biPlanes, 2 );
			bmp_info->biCompression = 1 ;
			success = (bmp_read32( infile, &bmp_info->biCompression, 6 )==6);
		}else
		{
			CARD16 dumm[2] ;
			bmp_read16( infile, &dumm[0], 2 );
			bmp_info->biWidth = dumm[0] ;
			bmp_info->biHeight = dumm[1] ;
			success = ( bmp_read16( infile, &bmp_info->biPlanes, 2 ) == 2 );
			bmp_info->biCompression = 0 ;
		}
	}
#ifdef LOCAL_DEBUG
	fprintf( stderr, "bmp.info.biSize = %ld(0x%lX)\n", bmp_info->biSize, bmp_info->biSize );
	fprintf( stderr, "bmp.info.biWidth = %ld\nbmp.info.biHeight = %ld\n",  bmp_info->biWidth,  bmp_info->biHeight );
	fprintf( stderr, "bmp.info.biPlanes = %d\nbmp.info.biBitCount = %d\n", bmp_info->biPlanes, bmp_info->biBitCount );
	fprintf( stderr, "bmp.info.biCompression = %ld\n", bmp_info->biCompression );
	fprintf( stderr, "bmp.info.biSizeImage = %ld\n", bmp_info->biSizeImage );
#endif
	if( (long)(bmp_info->biHeight) < 0 )
		direction = 1 ;
	if( height == 0 )
		height  = direction == 1 ? -((long)(bmp_info->biHeight)):bmp_info->biHeight ;
	if( width == 0 )
		width = bmp_info->biWidth ;

	if( !success || bmp_info->biCompression != 0 ||
		width > MAX_IMPORT_IMAGE_SIZE ||
		height > MAX_IMPORT_IMAGE_SIZE )
	{
		return NULL;
	}
	if( bmp_info->biBitCount < 16 )
		cmap_entries = 0x01<<bmp_info->biBitCount ;

	if( bmp_info->biSize != 40 )
		cmap_entry_size = 3;
	if( cmap_entries )
	{
		cmap = safemalloc( cmap_entries * cmap_entry_size );
		fread(cmap, sizeof (CARD8), cmap_entries * cmap_entry_size, infile);
	}

	if( add_colormap )
		data_offset += cmap_entries*cmap_entry_size ;

	fseek( infile, data_offset, SEEK_SET );
	row_size = (width*bmp_info->biBitCount)>>3 ;
	if( row_size == 0 )
		row_size = 1 ;
	else
		row_size = (row_size+3)/4 ;            /* everything is aligned by 32 bits */
	row_size *= 4 ;                            /* in bytes  */
	data = safemalloc( row_size );

	im = safecalloc( 1, sizeof( ASImage ) );
	asimage_start( im, width,  height, compression );
	/* Window BMP files are little endian  - we need to swap Red and Blue */
	prepare_scanline( im->width, 0, buf, True );

	y =( direction == 1 )?0:height-1 ;
	fprintf( stderr, "direction = %d, y = %d\n", direction, y );
	while( y >= 0 && y < height)
	{
		int x ;
		if( fread( data, sizeof (char), row_size, infile ) < row_size )
			break;
		switch( bmp_info->biBitCount )
		{
			case 1 :
				for( x = 0 ; x < bmp_info->biWidth ; x++ )
				{
					int entry = (data[x>>3]&(1<<(x&0x07)))?cmap_entry_size:0 ;
					buf->red[x] = cmap[entry+2];
					buf->green[x] = cmap[entry+1];
					buf->blue[x] = cmap[entry];
				}
			    break ;
			case 4 :
				for( x = 0 ; x < bmp_info->biWidth ; x++ )
				{
					int entry = data[x>>1];
					if(x&0x01)
						entry = ((entry>>4)&0x0F)*cmap_entry_size ;
					else
						entry = (entry&0x0F)*cmap_entry_size ;
					buf->red[x] = cmap[entry+2];
					buf->green[x] = cmap[entry+1];
					buf->blue[x] = cmap[entry];
				}
			    break ;
			case 8 :
				for( x = 0 ; x < bmp_info->biWidth ; x++ )
				{
					int entry = data[x]*cmap_entry_size ;
					buf->red[x] = cmap[entry+2];
					buf->green[x] = cmap[entry+1];
					buf->blue[x] = cmap[entry];
				}
			    break ;
			case 16 :
				for( x = 0 ; x < bmp_info->biWidth ; ++x )
				{
					CARD8 c1 = data[x] ;
					CARD8 c2 = data[++x];
					buf->blue[x] =    c1&0x1F;
					buf->green[x] = ((c1>>5)&0x07)|((c2<<3)&0x18);
					buf->red[x] =   ((c2>>2)&0x1F);
				}
			    break ;
			default:
				raw2scanline( data, buf, gamma_table, im->width, False, (bmp_info->biBitCount==32));
		}
		asimage_add_line (im, IC_RED,   buf->red  , y);
		asimage_add_line (im, IC_GREEN, buf->green, y);
		asimage_add_line (im, IC_BLUE,  buf->blue , y);
		y += direction ;
	}
	free( data );
	if( cmap )
		free( cmap );
	return im ;
}

ASImage *
bmp2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	ASScanline    buf;
	BITMAPFILEHEADER  bmp_header ;
	BITMAPINFOHEADER  bmp_info;


	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	bmp_header.bfType = 0 ;
	if( bmp_read16( infile, &bmp_header.bfType, 1 ) )
		if( bmp_header.bfType == BMP_SIGNATURE )
			if( bmp_read32( infile, &bmp_header.bfSize, 3 ) == 3 )
				im = read_bmp_image( infile, bmp_header.bfOffBits, &bmp_info, &buf, gamma_table, 0, 0, False, compression );
#ifdef LOCAL_DEBUG
	fprintf( stderr, "bmp.header.bfType = 0x%X\nbmp.header.bfSize = %ld\nbmp.header.bfOffBits = %ld(0x%lX)\n",
					  bmp_header.bfType, bmp_header.bfSize, bmp_header.bfOffBits, bmp_header.bfOffBits );
#endif
	if( im != NULL )
		free_scanline( &buf, True );
	else
		show_error( "invalid or unsupported BMP format in image file \"%s\"", path );

	fclose( infile );
#ifdef DO_CLOCKING
	printf ("\n read time (clocks): %lu\n", clock () - started);
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im ;
}

/***********************************************************************************/
/* Windows ICO/CUR file format :   									   			   */

ASImage *
ico2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	ASImage *im = NULL ;
	/* More stuff */
	FILE         *infile;					   /* source file */
#ifdef DO_CLOCKING
	clock_t       started = clock ();
#endif
	ASScanline    buf;
	int y, mask_bytes;
	CARD8  and_mask[8];
	struct IconDirectoryEntry {
    	CARD8  bWidth;
    	CARD8  bHeight;
    	CARD8  bColorCount;
    	CARD8  bReserved;
    	CARD16  wPlanes;
    	CARD16  wBitCount;
    	CARD32 dwBytesInRes;
    	CARD32 dwImageOffset;
	};
	struct ICONDIR {
    	CARD16          idReserved;
    	CARD16          idType;
    	CARD16          idCount;
	} icon_dir;
   	struct IconDirectoryEntry  icon;
	BITMAPINFOHEADER bmp_info;

	if ((infile = open_image_file(path)) == NULL)
		return NULL;

	icon_dir.idType = 0 ;
	if( bmp_read16( infile, &icon_dir.idReserved, 3 ) == 3)
		if( icon_dir.idType == 1 || icon_dir.idType == 2)
		{
			fread( &(icon.bWidth), sizeof(CARD8),4,infile );
			bmp_read16( infile, &(icon.wPlanes), 2 );
			if( bmp_read32( infile, &(icon.dwBytesInRes), 2 ) == 2 )
			{
				fseek( infile, icon.dwImageOffset, SEEK_SET );
				im = read_bmp_image( infile, icon.dwImageOffset+40+(icon.bColorCount*4), &bmp_info, &buf, gamma_table,
					                 icon.bWidth, icon.bHeight, (icon.bColorCount==0), compression );
			}
		}
#ifdef LOCAL_DEBUG
	fprintf( stderr, "icon.dir.idType = 0x%X\nicon.dir.idCount = %d\n",  icon_dir.idType, icon_dir.idCount );
	fprintf( stderr, "icon[1].bWidth = %d(0x%X)\n",  icon.bWidth,  icon.bWidth );
	fprintf( stderr, "icon[1].bHeight = %d(0x%X)\n",  icon.bHeight,  icon.bHeight );
	fprintf( stderr, "icon[1].bColorCount = %d\n",  icon.bColorCount );
	fprintf( stderr, "icon[1].dwImageOffset = %ld(0x%lX)\n",  icon.dwImageOffset,  icon.dwImageOffset );
#endif
	if( im != NULL )
	{
		mask_bytes = icon.bWidth>>3 ;
		if( mask_bytes > 8 )
			mask_bytes = 8 ;
		for( y = icon.bHeight-1 ; y >= 0 ; y-- )
		{
			int x ;
			if( fread( &(and_mask[0]), sizeof (CARD8), mask_bytes, infile ) < mask_bytes )
				break;
			for( x = 0 ; x < icon.bWidth ; ++x )
				buf.alpha[x] = (and_mask[x>>3]&(0x80>>(x&0x7)))? 0x0000 : 0x00FF ;
			asimage_add_line (im, IC_ALPHA, buf.alpha, y);
		}
		free_scanline( &buf, True );
	}else
		show_error( "invalid or unsupported ICO format in image file \"%s\"", path );

	fclose( infile );
#ifdef DO_CLOCKING
	printf ("\n read time (clocks): %lu\n", clock () - started);
	printf ("\n image loading time (clocks): %lu\n", clock () - started);
#endif
	return im ;

}

/***********************************************************************************/
#ifdef HAVE_GIF		/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */
ASImage *
gif2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	FILE			   *fp ;
	int					status = GIF_ERROR;
	GifFileType        *gif;
	GifPixelType	   *all_rows = NULL;
    GifRowType         *rows = NULL;
	GifRecordType       rec;
	ASImage 	 	   *im = NULL ;
	ASScanline    		buf;
	int 		  		transparent = -1 ;
	unsigned int  		y;
	unsigned int		width, height ;

	if ((fp = open_image_file(path)) == NULL)
		return NULL;

	if( (gif = DGifOpenFileHandle(fileno(fp))) != NULL )
	{
		while((status = DGifGetRecordType(gif, &rec)) != GIF_ERROR)
		{
			if( rec == TERMINATE_RECORD_TYPE )
				break;
			if( rec == IMAGE_DESC_RECORD_TYPE && rows == NULL )
			{
				size_t offset = 0;
		    	if ((status = DGifGetImageDesc(gif)) == GIF_ERROR)
  		  			break;
			    width = gif->Image.Width;
			    height = gif->Image.Height;

				if( width >= MAX_IMPORT_IMAGE_SIZE || height >= MAX_IMPORT_IMAGE_SIZE )
					break;

			    rows = safemalloc(height * sizeof(GifRowType *));
				all_rows = safemalloc(height * width * sizeof(GifPixelType));

				for (y = 0; y < height; y++)
				{
					rows[y] = all_rows+offset ;
					offset += width*sizeof(GifPixelType);
				}
				if (gif->Image.Interlace)
				{
					int i ;
					static int intoffset[] = {0, 4, 2, 1};
					static int intjump[] = {8, 8, 4, 2};
					for (i = 0; i < 4; ++i)
			            for (y = intoffset[i]; y < height; y += intjump[i])
				            if( (status = DGifGetLine(gif, rows[y], width)) != GIF_OK )
							{
								i = 4;
								break;
							}
		        }else
			        for (y = 0; y < height; ++y)
			            if( (status = DGifGetLine(gif, rows[y], width)) != GIF_OK )
							break;
			}else if (rec == EXTENSION_RECORD_TYPE )
			{
	    		int         ext_code = 0;
    			GifByteType *ext = NULL;

		  		DGifGetExtension(gif, &ext_code, &ext);
  				while (ext)
				{
					if( transparent < 0 )
      					if( ext_code == 0xf9 && (ext[1]&0x01))
				  			transparent = (int) ext[4];
		      		ext = NULL;
      				DGifGetExtensionNext(gif, &ext);
				}
			}

			if( status != GIF_OK )
				break;
  		}
	}

	if( status == GIF_OK && rows  )
	{
		int bg_color =   gif->SBackGroundColor ;
	  	ColorMapObject  *cmap = gif->SColorMap ;

		im = safecalloc( 1, sizeof( ASImage ) );
		asimage_start( im, width, height, compression );
		prepare_scanline( im->width, 0, &buf, False );

		if( gif->Image.ColorMap != NULL)
			cmap = gif->Image.ColorMap ; /* private colormap where available */

		for (y = 0; y < height; ++y)
		{
			int x ;
			Bool do_alpha = False ;
			for (x = 0; x < width; ++x)
			{
				int c = rows[y][x];
      			if ( c == transparent)
				{
					c = bg_color ;
					do_alpha = True ;
					buf.alpha[x] = 0 ;
				}else
					buf.alpha[x] = 0x00FF ;
		        buf.red[x]   = cmap->Colors[c].Red;
		        buf.green[x] = cmap->Colors[c].Green;
				buf.blue[x]  = cmap->Colors[c].Blue;
	        }
			asimage_add_line (im, IC_RED,   buf.red, y);
			asimage_add_line (im, IC_GREEN, buf.green, y);
			asimage_add_line (im, IC_BLUE,  buf.blue, y);
			if( do_alpha )
				asimage_add_line (im, IC_ALPHA,  buf.alpha, y);
		}
		free_scanline(&buf, True);
	}
	if( rows )
		free( rows );
	if( all_rows )
		free( all_rows );

	DGifCloseFile(gif);
	return im ;
}
#else 			/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */
ASImage *
gif2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	show_error( "unable to load file \"%s\" - missing GIF image format libraries.\n", path );
	return NULL ;
}
#endif			/* GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF GIF */

#ifdef HAVE_TIFF/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */
ASImage *
tiff2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	TIFF 		 *tif ;

	static ASImage 	 *im = NULL ;
	ASScanline    buf;
	unsigned int  width, height;
	CARD32		 *data;

	if ((tif = TIFFOpen(path,"r")) == NULL)
	{
		show_error("cannot open image file \"%s\" for reading. Please check permissions.", path);
		return NULL;
	}

	if( subimage > 0 )
		if( !TIFFSetDirectory(tif, subimage))
			show_warning("failed to read subimage %d from image file \"%s\". Reading first available instead.", subimage, path);

	TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
	TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
	if( width < MAX_IMPORT_IMAGE_SIZE && height < MAX_IMPORT_IMAGE_SIZE )
	{
		if ((data = (CARD32*) _TIFFmalloc(width * height * sizeof (CARD32))) != NULL)
		{
			im = safecalloc( 1, sizeof( ASImage ) );
			asimage_start( im, width, height, compression );
			prepare_scanline( im->width, 0, &buf, False );

			if (TIFFReadRGBAImage(tif, width, height, data, 0))
			{
				register CARD32 *row = data ;
				int y = height ;
				while( --y >= 0 )
				{
					int x ;
					for( x = 0 ; x < width ; ++x )
					{
						CARD32 c = row[x] ;
						buf.alpha[x] = (c>>24)&0x00FF;
						buf.red[x]   = (c    )&0x00FF ;
						buf.green[x] = (c>>8 )&0x00FF ;
						buf.blue[x]  = (c>>16)&0x00FF ;
					}
					asimage_add_line (im, IC_RED,   buf.red, y);
					asimage_add_line (im, IC_GREEN, buf.green, y);
					asimage_add_line (im, IC_BLUE,  buf.blue, y);
					for( x = 0 ; x < width ; ++x )
						if( buf.alpha[x] != 0x00FF )
						{
							asimage_add_line (im, IC_ALPHA,  buf.alpha, y);
							break;
						}
					row += width ;
				}
		    }
			free_scanline(&buf, True);
			_TIFFfree(data);
		}
	}
	/* close the file */
	TIFFClose(tif);
	return im ;
}
#else 			/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */

ASImage *
tiff2ASImage( const char * path, ASFlagType what, double gamma, CARD8 *gamma_table, int subimage, unsigned int compression )
{
	show_error( "unable to load file \"%s\" - missing TIFF image format libraries.\n", path );
	return NULL ;
}
#endif			/* TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF TIFF */
