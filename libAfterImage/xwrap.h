#ifndef XWRAP_H_HEADER_INCLUDED
#define XWRAP_H_HEADER_INCLUDED

#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xresource.h>

extern Display *dpy;

#else

#define Display  void 
#ifndef Bool
#define Bool int
#endif
#ifndef True
#define True 1
#endif
#ifndef False
#define False 0
#endif

#ifndef CARD32   
#define CARD32 unsigned long
#endif
#ifndef CARD16
#define CARD16 unsigned short
#endif
#ifndef CARD8   
#define CARD8 unsigned char
#endif

#ifndef XID
#define XID CARD32
#endif

#ifndef Drawable
#define Drawable   XID
#endif
#ifndef Atom
#define Atom       XID
#endif
#ifndef Window
#define Window     XID
#endif
#ifndef Pixmap
#define Pixmap     XID
#endif
#ifndef Colormap
#define Colormap   XID
#endif
#ifndef Cursor
#define Cursor   XID
#endif
#ifndef VisualID
#define VisualID   XID
#endif

#ifndef GC
#define GC void*
#endif

#ifndef None
#define None 0
#endif


typedef struct {
    unsigned char *value;		/* same as Property routines */
    Atom encoding;			/* prop type */
    int format;				/* prop data format: 8, 16, or 32 */
    unsigned long nitems;		/* number of data items in value */
} XTextProperty;

typedef struct {
	void *ext_data;	/* hook for extension to hang data */
	XID visualid;	/* visual id of this visual */
#if defined(__cplusplus) || defined(c_plusplus)
	int c_class;		/* C++ class of screen (monochrome, etc.) */
#else
	int class;		/* class of screen (monochrome, etc.) */
#endif
	unsigned long red_mask, green_mask, blue_mask;	/* mask values */
	int bits_per_rgb;	/* log base 2 of distinct color values */
	int map_entries;	/* color map entries */
} Visual;

typedef struct {
  Visual *visual;
  VisualID visualid;
  int screen;
  int depth;
#if defined(__cplusplus) || defined(c_plusplus)
  int c_class;					/* C++ */
#else
  int class;
#endif
  unsigned long red_mask;
  unsigned long green_mask;
  unsigned long blue_mask;
  int colormap_size;
  int bits_per_rgb;
} XVisualInfo;

typedef struct _XImage {
    int width, height;		/* size of image */
    int xoffset;		/* number of pixels offset in X direction */
    int format;			/* XYBitmap, XYPixmap, ZPixmap */
    char *data;			/* pointer to image data */
    int byte_order;		/* data byte order, LSBFirst, MSBFirst */
    int bitmap_unit;		/* quant. of scanline 8, 16, 32 */
    int bitmap_bit_order;	/* LSBFirst, MSBFirst */
    int bitmap_pad;		/* 8, 16, 32 either XY or ZPixmap */
    int depth;			/* depth of image */
    int bytes_per_line;		/* accelarator to next line */
    int bits_per_pixel;		/* bits per pixel (ZPixmap) */
    unsigned long red_mask;	/* bits in z arrangment */
    unsigned long green_mask;
    unsigned long blue_mask;
    void *obdata;		/* hook for the object routines to hang on */
    struct funcs {		/* image manipulation routines */
#if NeedFunctionPrototypes
	struct _XImage *(*create_image)(
		void* /* display */,
		void*		/* visual */,
		unsigned int	/* depth */,
		int		/* format */,
		int		/* offset */,
		char*		/* data */,
		unsigned int	/* width */,
		unsigned int	/* height */,
		int		/* bitmap_pad */,
		int		/* bytes_per_line */);
	int (*destroy_image)        (struct _XImage *);
	unsigned long (*get_pixel)  (struct _XImage *, int, int);
	int (*put_pixel)            (struct _XImage *, int, int, unsigned long);
	struct _XImage *(*sub_image)(struct _XImage *, int, int, unsigned int, unsigned int);
	int (*add_pixel)            (struct _XImage *, long);
#else
	struct _XImage *(*create_image)();
	int (*destroy_image)();
	unsigned long (*get_pixel)();
	int (*put_pixel)();
	struct _XImage *(*sub_image)();
	int (*add_pixel)();
#endif
	} f;
} XImage;

typedef struct {
    Pixmap background_pixmap;	/* background or None or ParentRelative */
    unsigned long background_pixel;	/* background pixel */
    Pixmap border_pixmap;	/* border of the window */
    unsigned long border_pixel;	/* border pixel value */
    int bit_gravity;		/* one of bit gravity values */
    int win_gravity;		/* one of the window gravity values */
    int backing_store;		/* NotUseful, WhenMapped, Always */
    unsigned long backing_planes;/* planes to be preseved if possible */
    unsigned long backing_pixel;/* value to use in restoring planes */
    Bool save_under;		/* should bits under be saved? (popups) */
    long event_mask;		/* set of events that should be saved */
    long do_not_propagate_mask;	/* set of events that should not propagate */
    Bool override_redirect;	/* boolean value for override-redirect */
    Colormap colormap;		/* color map to be associated with window */
    Cursor cursor;		/* cursor to be displayed (or None) */
} XSetWindowAttributes;

#endif

extern Display *dpy ;
int asim_get_drawable_size (Drawable d, unsigned int *ret_w, unsigned int *ret_h);
#define get_drawable_size(d,w,h) asim_get_drawable_size((d),(w),(h))

#endif
