/*
 * Copyright (c) 2001,2000,1999 Sasha Vasko <sashav@sprintmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdio.h>

#include "xwrap.h"
#include "audit.h"
#include "output.h"

Display *dpy;

#ifndef X_DISPLAY_MISSING
static int
quiet_xerror_handler (Display * dpy, XErrorEvent * error)
{
    return 0;
}
#endif

int
get_drawable_size (Drawable d, unsigned int *ret_w, unsigned int *ret_h)
{
	Window        root;
	unsigned int  ujunk;
	int           junk;

	if( d == None ) return 0;

#ifndef X_DISPLAY_MISSING
	if (XGetGeometry (dpy, d, &root, &junk, &junk, ret_w, ret_h, &ujunk, &ujunk) == 0)
#endif
	{
		*ret_w = 0;
		*ret_h = 0;
		return 0;
	}
	return 1;
}

Drawable
validate_drawable (Drawable d, unsigned int *pwidth, unsigned int *pheight)
{
#ifndef X_DISPLAY_MISSING
	int           (*oldXErrorHandler) (Display *, XErrorEvent *) = NULL;

	/* we need to check if pixmap is still valid */
	Window        root;
	int           junk;

	oldXErrorHandler = XSetErrorHandler (quiet_xerror_handler);

	if (!pwidth)
		pwidth = &junk;
	if (!pheight)
		pheight = &junk;

	if (d != None)
	{
		if (!XGetGeometry (dpy, d, &root, &junk, &junk, pwidth, pheight, &junk, &junk))
			d = None;
	}
	XSetErrorHandler (oldXErrorHandler);

	return d;
#else
	return None ;
#endif	
}

void
backtrace_window ( Window w )
{
#ifndef X_DISPLAY_MISSING
    Window        root, parent, *children = NULL;
	unsigned int  nchildren;
    int           (*oldXErrorHandler) (Display *, XErrorEvent *) = NULL;

    oldXErrorHandler = XSetErrorHandler (quiet_xerror_handler);
    fprintf (stderr, "Backtracing [%lX]", w);
	while (XQueryTree (dpy, w, &root, &parent, &children, &nchildren))
	{
		int x, y ;
		unsigned int width, height, border, depth ;
		XGetGeometry( dpy, w, &root, &x, &y, &width, &height, &border, &depth );
	    fprintf (stderr, "(%dx%d%+d%+d)", width, height, x, y );

		if (children)
			XFree (children);
        children = NULL ;
		w = parent;
        fprintf (stderr, "->[%lX] ", w);
		if( w == None )
			break;
	}
    XSetErrorHandler (oldXErrorHandler);
    fprintf (stderr, "\n");
#endif
}

Window
get_parent_window( Window w )
{
    Window        root, parent = None, *children = NULL;
    unsigned int  child_count;

#ifndef X_DISPLAY_MISSING
	XSync( dpy, False );
    XQueryTree (dpy, w, &root, &parent, &children, &child_count);
    if (children)
        XFree (children);
#endif
	return parent ;
}

Window
get_topmost_parent( Window w, Window *desktop_w )
{
    Window  root = None, parent = w, desktop = w ;
	Window *children = NULL;
    unsigned int  child_count;
#ifndef X_DISPLAY_MISSING
	XSync( dpy, False );
	while( w != root && w != None )
	{
		parent = desktop ;
		desktop = w ;
		XQueryTree (dpy, w, &root, &w, &children, &child_count);
    	if (children)
        	XFree (children);
	}
#endif
	if( desktop_w )
		*desktop_w = desktop ;
	return w ;
}


