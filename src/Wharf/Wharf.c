/*
 * Copyright (c) 1998 Michal Vitecek <M.Vitecek@sh.cvut.cz>
 * Copyright (c) 1998,2002 Sasha Vasko <sasha at aftercode.net>
 * Copyright (C) 1998 Ethan Fischer
 * Copyright (C) 1998 Guylhem Aznar
 * Copyright (C) 1996 Alfredo K. Kojima
 * Copyright (C) 1996 Beat Christen
 * Copyright (C) 1996 Kaj Groner
 * Copyright (C) 1996 Frank Fejes
 * Copyright (C) 1996 mj@dfv.rwth-aachen.de
 * Copyright (C) 1995 Bo Yang
 * Copyright (C) 1993 Robert Nation
 *
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#undef DO_CLOCKING
#define LOCAL_DEBUG
#define EVENT_TRACE

#include "../../configure.h"
#include "../../libAfterStep/asapp.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include "../../libAfterStep/afterstep.h"
#include "../../libAfterStep/screen.h"
#include "../../libAfterStep/session.h"
#include "../../libAfterStep/module.h"
#include "../../libAfterStep/parser.h"
#include "../../libAfterStep/mystyle.h"
#include "../../libAfterStep/mystyle_property.h"
#include "../../libAfterStep/balloon.h"
#include "../../libAfterStep/aswindata.h"
#include "../../libAfterStep/decor.h"
#include "../../libAfterStep/event.h"
#include "../../libAfterStep/wmprops.h"
#include "../../libAfterStep/functions.h"
#include "../../libAfterStep/shape.h"

#include "../../libAfterConf/afterconf.h"


#ifdef ENABLE_SOUND
#define WHEV_PUSH		0
#define WHEV_CLOSE_FOLDER	1
#define WHEV_OPEN_FOLDER	2
#define WHEV_CLOSE_MAIN		3
#define WHEV_OPEN_MAIN		4
#define WHEV_DROP		5
#define MAX_EVENTS		6
#endif

#define MAGIC_WHARF_BUTTON    0xA38AB110
#define MAGIC_WHARF_FOLDER    0xA38AF01D

struct ASWharfFolder;

typedef struct ASSwallowed
{
    ASCanvas *normal, *iconic;
    ASCanvas *current;                         /* one of the above */
}ASSwallowed;

typedef struct ASWharfButton
{
    unsigned long magic;
#define ASW_SwallowTarget   (0x01<<0)
#define ASW_MaxSwallow      (0x01<<1)
#define ASW_FixedWidth		(0x01<<2)
#define ASW_FixedHeight		(0x01<<3)
#define ASW_Transient		(0x01<<4)
    ASFlagType flags;
    char        *name ;
    ASCanvas    *canvas;
    ASSwallowed *swallowed;
    ASTBarData  *bar;

    unsigned int    desired_width, desired_height;
    /* this is where it it will actually be placed and what size it should have at the end : */
    int             folder_x, folder_y;
    unsigned int    folder_width, folder_height;

    FunctionData   *fdata;

    struct ASWharfFolder   *folder;
    struct ASWharfFolder   *parent;
}ASWharfButton;

typedef struct ASWharfFolder
{
    unsigned long magic;
#define ASW_Mapped          (0x01<<0)
#define ASW_Vertical        (0x01<<1)
#define ASW_Withdrawn       (0x01<<2)
#define ASW_NeedsShaping    (0x01<<3)
#define ASW_Shaped          (0x01<<4)
#define ASW_ReverseOrder    (0x01<<5)
#define ASW_UseBoundary     (0x01<<6)
#define ASW_AnimationPending  (0x01<<7)
    ASFlagType  flags;

    ASCanvas    *canvas;
    ASWharfButton *buttons ;
    int buttons_num;
    ASWharfButton *parent ;
	
	int gravity ;
    unsigned int total_width, total_height;    /* size calculated based on size of participating buttons */

    int animation_steps;                       /* how many steps left */
    int animation_dir;                         /* +1 or -1 */
	/* this will cache RootImage so we don't have to pull it every time we 
	 * have to be animated */
	ASImage   *root_image ; 
	XRectangle root_clip_area ;

	XRectangle boundary ;
	unsigned int animate_from_w, animate_from_h ;
	unsigned int animate_to_w, animate_to_h ;

	ASWharfButton *withdrawn_button ;

}ASWharfFolder;

typedef struct ASWharfState
{
    ASHashTable   *win2obj_xref;               /* xref of window IDs to wharf buttons and folders */
    ASWharfFolder *root_folder ;

    ASHashTable   *swallow_targets;            /* hash of buttons that needs to swallow  */

    ASWharfButton *pressed_button;
    int pressed_state;

    Bool shaped_style;

	ASImage   *withdrawn_root_image ; 
	XRectangle withdrawn_root_clip_area ;

	ASWharfFolder *root_image_folder ; /* points to a folder that owns current Scr.RootImage */

	int buttons_render_pending ;

	ASWharfButton *focused_button ;

}ASWharfState;

ASWharfState WharfState;
WharfConfig *Config = NULL;

#define WHARF_BUTTON_EVENT_MASK   (ButtonReleaseMask |\
                                   ButtonPressMask | LeaveWindowMask | EnterWindowMask |\
                                   StructureNotifyMask | SubstructureRedirectMask )
#define WHARF_FOLDER_EVENT_MASK   (StructureNotifyMask)


void HandleEvents();
void process_message (send_data_type type, send_data_type *body);
void DispatchEvent (ASEvent * Event);
Window make_wharf_window();
void GetOptions (const char *filename);
void GetBaseOptions (const char *filename);
void CheckConfigSanity();

ASWharfFolder *build_wharf_folder( WharfButton *list, ASWharfButton *parent, Bool vertical );
Bool display_wharf_folder( ASWharfFolder *aswf, int left, int top, int right, int bottom );
Bool display_main_folder();
void withdraw_wharf_folder( ASWharfFolder *aswf );
void on_wharf_moveresize( ASEvent *event );
void destroy_wharf_folder( ASWharfFolder **paswf );
void on_wharf_pressed( ASEvent *event );
void release_pressure();
Bool check_pending_swallow( ASWharfFolder *aswf );
void exec_pending_swallow( ASWharfFolder *aswf );
void check_swallow_window( ASWindowData *wd );
void update_wharf_folder_transprency( ASWharfFolder *aswf, Bool force );
void update_wharf_folder_styles( ASWharfFolder *aswf, Bool force );
void on_wharf_button_confreq( ASWharfButton *aswb, ASEvent *event );
void do_wharf_animate_iter( void *vdata );
void clear_root_image_cache( ASWharfFolder *aswf );
Bool render_wharf_button( ASWharfButton *aswb );
void set_wharf_clip_area( ASWharfFolder *aswf, int x, int y );
void set_withdrawn_clip_area( ASWharfFolder *aswf, int x, int y, unsigned int w, unsigned int h );
void change_button_focus(ASWharfButton *aswb, Bool focused ); 

/***********************************************************************
 *   main - start of module
 ***********************************************************************/
int
main (int argc, char **argv)
{
    /* Save our program name - for error messages */
    InitMyApp (CLASS_WHARF, argc, argv, NULL, NULL, 0 );

    memset( &WharfState, 0x00, sizeof(WharfState));

    ConnectX( &Scr, PropertyChangeMask|EnterWindowMask );
    ConnectAfterStep (M_TOGGLE_PAGING |
                    M_NEW_DESKVIEWPORT |
                    M_END_WINDOWLIST |
                    WINDOW_CONFIG_MASK |
                    WINDOW_NAME_MASK);
    balloon_init (False);

    Config = CreateWharfConfig ();

    LOCAL_DEBUG_OUT("parsing Options ...%s","");
    LoadBaseConfig (GetBaseOptions);
	LoadColorScheme();
    LoadConfig ("wharf", GetOptions);

    CheckConfigSanity();

    WharfState.root_folder = build_wharf_folder( Config->root_folder, NULL, (Config->columns > 0 ) );
    if( !display_main_folder() )
    {
        show_error( "main folder does not have any entries or has zero size. Aborting!");
        return 1;
    }

    /* Create a list of all windows */
    /* Request a list of all windows,
     * wait for ConfigureWindow packets */
    if( check_pending_swallow(WharfState.root_folder) )
        SendInfo ("Send_WindowList", 0);

    /* create main folder here : */

    LOCAL_DEBUG_OUT("starting The Loop ...%s","");
    HandleEvents();

    return 0;
}

void HandleEvents()
{
    ASEvent event;
    Bool has_x_events = False ;
    while (True)
    {
        while((has_x_events = XPending (dpy)))
        {
            if( ASNextEvent (&(event.x), True) )
            {
                event.client = NULL ;
                setup_asevent_from_xevent( &event );
                DispatchEvent( &event );
				timer_handle ();
            }
        }
        module_wait_pipes_input (process_message );
    }
}

void
MapConfigureNotifyLoop()
{
    ASEvent event;

	do
	{
		if( !ASCheckTypedEvent(MapNotify,&(event.x)) )
			if( !ASCheckTypedEvent(ConfigureNotify,&(event.x)) )
				return ;
        
        event.client = NULL ;
        setup_asevent_from_xevent( &event );
        DispatchEvent( &event );
        ASSync(False);
    }while(1);
}



void
DeadPipe (int nonsense)
{
	static int already_dead = False ; 
	if( already_dead ) 
		return;/* non-reentrant function ! */
	already_dead = True ;
    
	destroy_wharf_folder( &(WharfState.root_folder) );
    DestroyWharfConfig( Config );
    destroy_ashash( &(WharfState.win2obj_xref) );
    window_data_cleanup();

    FreeMyAppResources();
#ifdef DEBUG_ALLOCS
/* normally, we let the system clean up, but when auditing time comes
 * around, it's best to have the books in order... */
    print_unfreed_mem ();
#endif /* DEBUG_ALLOCS */
    XFlush (dpy);			/* need this for SetErootPixmap to take effect */
	XCloseDisplay (dpy);		/* need this for SetErootPixmap to take effect */
    exit (0);
}


/*****************************************************************************
 *
 * This routine is responsible for reading and parsing the config file
 *
 ****************************************************************************/
void
CheckConfigSanity()
{
    char buf[256];

    if( Config == NULL )
        Config = CreateWharfConfig ();

    if( Config->rows <= 0 && Config->columns <= 0 )
        Config->rows = 1;

    mystyle_get_property (Scr.wmprops);

    sprintf( buf, "*%sTile", get_application_name() );
    LOCAL_DEBUG_OUT("Attempting to use style \"%s\"", buf);
    Scr.Look.MSWindow[BACK_UNFOCUSED] = mystyle_find_or_default( buf );
    LOCAL_DEBUG_OUT("Will use style \"%s\"", Scr.Look.MSWindow[BACK_UNFOCUSED]->name);
    sprintf( buf, "*%sFocusedTile", get_application_name() );
    LOCAL_DEBUG_OUT("Attempting to use style \"%s\" for focused tile", buf);
    Scr.Look.MSWindow[BACK_FOCUSED] = mystyle_find( buf );

	if( get_flags( Config->set_flags, WHARF_FORCE_SIZE ) )
    {
        if( Config->force_size.width == 0 )
            Config->force_size.width = 64 ;
        if( Config->force_size.height == 0 )
            Config->force_size.height = 64 ;
    }else if( !get_flags(Config->flags, WHARF_FIT_CONTENTS) )
    {
        if( Scr.Look.MSWindow[BACK_UNFOCUSED]->back_icon.image != NULL )
        {
            Config->force_size.width  = Scr.Look.MSWindow[BACK_UNFOCUSED]->back_icon.width ;
            Config->force_size.height = Scr.Look.MSWindow[BACK_UNFOCUSED]->back_icon.height ;
        }else
        {
            Config->force_size.width  = 64;
            Config->force_size.height = 64;
        }
    }else
	{
        Config->force_size.width  = 0 ;
        Config->force_size.height = 0 ;
	}

    if( Config->composition_method == 0 )
        Config->composition_method = TEXTURE_TRANSPIXMAP_ALPHA;

    WharfState.shaped_style = False ;
    if( Scr.Look.MSWindow[BACK_UNFOCUSED]->texture_type >= TEXTURE_TEXTURED_START &&
        Scr.Look.MSWindow[BACK_UNFOCUSED]->texture_type <= TEXTURE_SHAPED_PIXMAP  )
    {
        WharfState.shaped_style = True;
        LOCAL_DEBUG_OUT( "shaped pixmap detected%s","");
    }else if( Scr.Look.MSWindow[BACK_UNFOCUSED]->texture_type >= TEXTURE_SCALED_PIXMAP &&
        Scr.Look.MSWindow[BACK_UNFOCUSED]->texture_type <= TEXTURE_PIXMAP  )
    {
        ASImage *im = Scr.Look.MSWindow[BACK_UNFOCUSED]->back_icon.image ;
        if( im && check_asimage_alpha( Scr.asv, im ) )
        {
            WharfState.shaped_style = True ;
            LOCAL_DEBUG_OUT( "transparent pixmap detected%s","");
        }
    }

    if( !get_flags( Config->balloon_conf->set_flags, BALLOON_STYLE ) )
    {
        Config->balloon_conf->style = mystrdup("*WharfBalloon");
        set_flags( Config->balloon_conf->set_flags, BALLOON_STYLE );
    }

#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
    show_progress( "printing wharf config : ");
    PrintWharfConfig(Config);
    Print_balloonConfig ( Config->balloon_conf );
#endif
    balloon_config2look( &(Scr.Look), Config->balloon_conf );
    LOCAL_DEBUG_OUT( "balloon mystyle = %p (\"%s\")", Scr.Look.balloon_look->style,
                    Scr.Look.balloon_look->style?Scr.Look.balloon_look->style->name:"none" );
    set_balloon_look( Scr.Look.balloon_look );

}

void
merge_wharf_folders( WharfButton **pf1, WharfButton **pf2 )
{
    while( *pf1 )
        pf1 = &((*pf1)->next);
    *pf1 = *pf2 ;
    *pf2 = NULL ;
}

void
GetOptions (const char *filename)
{
    WharfConfig *config;
    START_TIME(option_time);
SHOW_CHECKPOINT;
    LOCAL_DEBUG_OUT( "loading wharf config from \"%s\": ", filename);
    config = ParseWharfOptions (filename, MyName);
    SHOW_TIME("Config parsing",option_time);

    /* Need to merge new config with what we have already :*/
    /* now lets check the config sanity : */
    /* mixing set and default flags : */
    Config->flags = (config->flags&config->set_flags)|(Config->flags & (~config->set_flags));
    Config->set_flags |= config->set_flags;

    if( get_flags(config->set_flags, WHARF_ROWS) )
        Config->rows = config->rows;

    if( get_flags(config->set_flags, WHARF_COLUMNS) )
        Config->columns = config->columns;

    if( get_flags( config->set_flags, WHARF_GEOMETRY ) )
        merge_geometry(&(config->geometry), &(Config->geometry) );

    if( get_flags( config->set_flags, WHARF_WITHDRAW_STYLE ) )
        Config->withdraw_style = config->withdraw_style;

    if( get_flags( config->set_flags, WHARF_FORCE_SIZE ) )
        merge_geometry( &(config->force_size), &(Config->force_size));

    if( get_flags( config->set_flags, WHARF_ANIMATE_STEPS ) )
        Config->animate_steps = config->animate_steps;
    if( get_flags( config->set_flags, WHARF_ANIMATE_STEPS_MAIN ) )
        Config->animate_steps_main = config->animate_steps_main ;
    if( get_flags( config->set_flags, WHARF_ANIMATE_DELAY ) )
        Config->animate_delay = config->animate_delay;
    if( get_flags( config->set_flags, WHARF_LABEL_LOCATION ) )
        Config->label_location = config->label_location;
    if( get_flags( config->set_flags, WHARF_ALIGN_CONTENTS ) )
        Config->align_contents = config->align_contents;
    if( get_flags( config->set_flags, WHARF_BEVEL ) )
	{
        Config->bevel = config->bevel;
		if( !get_flags( config->set_flags, WHARF_NO_BORDER ) )
			clear_flags( Config->set_flags, WHARF_NO_BORDER );
	}

    if( get_flags( config->set_flags, WHARF_COMPOSITION_METHOD ) )
        Config->composition_method = config->composition_method;

/*LOCAL_DEBUG_OUT( "align_contents = %d", Config->align_contents ); */
    if( get_flags( config->set_flags, WHARF_SOUND ) )
    {
        int i ;
        for( i = 0 ; i < WHEV_MAX_EVENTS ; ++i )
        {
            set_string_value(&(Config->sounds[i]), mystrdup(config->sounds[i]), NULL, 0 );
            config->sounds[i] = NULL ;
        }
    }
    /* merging folders : */

    if( config->root_folder )
        merge_wharf_folders( &(Config->root_folder), &(config->root_folder) );

    if( Config->balloon_conf )
        Destroy_balloonConfig( Config->balloon_conf );
    Config->balloon_conf = config->balloon_conf ;
    config->balloon_conf = NULL ;

    if (config->style_defs)
        ProcessMyStyleDefinitions (&(config->style_defs));

    DestroyWharfConfig (config);
    SHOW_TIME("Config parsing",option_time);
}
/*****************************************************************************
 *
 * This routine is responsible for reading and parsing the base file
 *
 ****************************************************************************/
void
GetBaseOptions (const char *filename)
{
    START_TIME(started);

	ReloadASEnvironment( NULL, NULL, NULL, False );

    SHOW_TIME("BaseConfigParsingTime",started);
}

/****************************************************************************/
/* Window ID xref :                                                         */
/****************************************************************************/
Bool
register_object( Window w, ASMagic *obj)
{
    if( WharfState.win2obj_xref == NULL )
        WharfState.win2obj_xref = create_ashash(0, NULL, NULL, NULL);

    return (add_hash_item(WharfState.win2obj_xref, AS_HASHABLE(w), obj) == ASH_Success);
}

ASMagic *
fetch_object( Window w )
{
	ASHashData hdata = {0} ;
    if( WharfState.win2obj_xref )
        if( get_hash_item( WharfState.win2obj_xref, AS_HASHABLE(w), &hdata.vptr ) != ASH_Success )
			hdata.vptr = NULL ;
    return hdata.vptr ;
}

void
unregister_object( Window w )
{
    if( WharfState.win2obj_xref )
        remove_hash_item( WharfState.win2obj_xref, AS_HASHABLE(w), NULL, False );
}

Bool
register_swallow_target( char *name, ASWharfButton *aswb)
{
    if( name && aswb )
    {
        if( WharfState.swallow_targets == NULL )
            WharfState.swallow_targets = create_ashash(0, casestring_hash_value, casestring_compare, NULL);

        return (add_hash_item(WharfState.swallow_targets, AS_HASHABLE(name), aswb) == ASH_Success);
    }
    return False;
}

ASWharfButton *
fetch_swallow_target( char *name )
{
    ASHashData hdata = {0};
    if( WharfState.swallow_targets && name )
        if( get_hash_item( WharfState.swallow_targets, AS_HASHABLE(name), &hdata.vptr ) != ASH_Success )
			hdata.vptr = NULL ;
    return hdata.vptr ;
}

void
unregister_swallow_target( char *name )
{
    if( WharfState.swallow_targets && name )
        remove_hash_item( WharfState.swallow_targets, AS_HASHABLE(name), NULL, False );
}


/****************************************************************************/
/* PROCESSING OF AFTERSTEP MESSAGES :                                       */
/****************************************************************************/
void
process_message (send_data_type type, send_data_type *body)
{
    LOCAL_DEBUG_OUT( "received message %lX", type );
	if( (type&WINDOW_PACKET_MASK) != 0 )
	{
		struct ASWindowData *wd = fetch_window_by_id( body[0] );
        WindowPacketResult res ;
        /* saving relevant client info since handle_window_packet could destroy the actuall structure */
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
        Window               saved_w = (wd && wd->canvas)?wd->canvas->w:None;
        int                  saved_desk = wd?wd->desk:INVALID_DESK;
        struct ASWindowData *saved_wd = wd ;
#endif
        LOCAL_DEBUG_OUT( "message %lX window %lX data %p", type, body[0], wd );
        res = handle_window_packet( type, body, &wd );
        LOCAL_DEBUG_OUT( "\t res = %d, data %p", res, wd );
        if( res == WP_DataCreated || res == WP_DataChanged )
        {
            check_swallow_window( wd );
        }else if( res == WP_DataDeleted )
        {
            LOCAL_DEBUG_OUT( "client deleted (%p)->window(%lX)->desk(%d)", saved_wd, saved_w, saved_desk );
        }
    }else if( type == M_END_WINDOWLIST )
        exec_pending_swallow( WharfState.root_folder );
}
/*************************************************************************
 * Event handling :
 *************************************************************************/
void
DispatchEvent (ASEvent * event)
{
    SHOW_EVENT_TRACE(event);

    event->client = NULL ;
    switch (event->x.type)
    {
	    case ConfigureNotify:
            on_wharf_moveresize( event );
            break;
        case KeyPress :
            break ;
        case KeyRelease :
            break ;
        case ButtonPress:
            on_wharf_pressed( event );
            break;
        case ButtonRelease:
            release_pressure();
            break;
        case MotionNotify :
            break ;
        case EnterNotify :
			if( event->x.xcrossing.window == Scr.Root )
			{
				if( WharfState.focused_button ) 
					change_button_focus(WharfState.focused_button, False ); 
				withdraw_active_balloon();
				break;
			}
        case LeaveNotify :
            {
                ASMagic *obj = fetch_object( event->w ) ;
				if( WharfState.focused_button ) 
					change_button_focus(WharfState.focused_button, False ); 
                if( obj != NULL && obj->magic == MAGIC_WHARF_BUTTON )
                {
                    ASWharfButton *aswb = (ASWharfButton*)obj;
                    on_astbar_pointer_action( aswb->bar, 0, (event->x.type==LeaveNotify));
					if(event->x.type == EnterNotify)
						change_button_focus(aswb, True ); 
                }
            }
            break ;
        case ConfigureRequest:
			{
            	ASMagic *obj = fetch_object( event->w ) ;
                if( obj != NULL && obj->magic == MAGIC_WHARF_BUTTON )
                {
                    ASWharfButton *aswb = (ASWharfButton*)obj;
					on_wharf_button_confreq( aswb, event );
				}
			}
            break;

	    case ClientMessage:
            {
#ifdef LOCAL_DEBUG
                char *name = XGetAtomName( dpy, event->x.xclient.message_type );
                LOCAL_DEBUG_OUT("ClientMessage(\"%s\",data=(%lX,%lX,%lX,%lX,%lX)", name, event->x.xclient.data.l[0], event->x.xclient.data.l[1], event->x.xclient.data.l[2], event->x.xclient.data.l[3], event->x.xclient.data.l[4]);
                XFree( name );
#endif
                if ( event->x.xclient.format == 32 &&
                    event->x.xclient.data.l[0] == _XA_WM_DELETE_WINDOW )
                {
                    DeadPipe(0);
                }
            }
            break;
        case ReparentNotify :
            if( event->x.xreparent.parent == Scr.Root )
            {
                //sleep_a_millisec( 100 );
                //XMoveResizeWindow( dpy, event->x.xreparent.window, -10000, -10000, 1, 1 );
            }
            break ;
	    case PropertyNotify:
			LOCAL_DEBUG_OUT( "property %s(%lX), _XROOTPMAP_ID = %lX, event->w = %lX, root = %lX", XGetAtomName(dpy, event->x.xproperty.atom), event->x.xproperty.atom, _XROOTPMAP_ID, event->w, Scr.Root );
            if( event->x.xproperty.atom == _XROOTPMAP_ID && event->w == Scr.Root )
            {
                LOCAL_DEBUG_OUT( "root background updated!%s","");
				clear_root_image_cache( WharfState.root_folder );
				if( Scr.RootImage ) 
				{	
                	safe_asimage_destroy( Scr.RootImage );
                	Scr.RootImage = NULL ;
				}
                update_wharf_folder_transprency( WharfState.root_folder, True );
            }else if( event->x.xproperty.atom == _AS_STYLE )
			{
				LOCAL_DEBUG_OUT( "AS Styles updated!%s","");
				handle_wmprop_event (Scr.wmprops, &(event->x));
				mystyle_list_destroy_all(&(Scr.Look.styles_list));
				LoadColorScheme();
				CheckConfigSanity();
				/* now we need to update everything */
				update_wharf_folder_styles( WharfState.root_folder, True );
			}
            break;
    }
}

/*************************************************************************/
/* Wharf buttons :                                                       */

ASCanvas*
create_wharf_folder_canvas(ASWharfFolder *aswf)
{
    static XSetWindowAttributes attr ;
    Window w ;
    ASCanvas *pc = NULL;
    unsigned long mask = CWEventMask|CWBackPixel ; /*map ; */

    attr.event_mask = WHARF_FOLDER_EVENT_MASK ;
    attr.background_pixel = Scr.asv->black_pixel ;
    attr.background_pixmap = ParentRelative ;
    if( Scr.asv->visual_info.visual != DefaultVisual( dpy, DefaultScreen(dpy)) )
        mask |= CWBackPixel ;
    w = create_visual_window( Scr.asv, Scr.Root, 0, 0, 2, 2, 0, InputOutput, mask, &attr );

#ifdef SHAPE
	{
		XRectangle rect ;
		rect.x = rect.y = 0 ;
		rect.width = rect.height = 1 ;
		XShapeCombineRectangles ( dpy, w, ShapeBounding, 0, 0, &rect, 1, ShapeSet, Unsorted);
	}
#endif

    register_object( w, (ASMagic*)aswf );
    pc = create_ascanvas_container(w);
    LOCAL_DEBUG_OUT("folder canvas %p created for window %lX", pc, w );
    return pc;
}


ASCanvas*
create_wharf_button_canvas(ASWharfButton *aswb, ASCanvas *parent)
{
    static XSetWindowAttributes attr ;
    Window w ;
    ASCanvas *canvas;

    attr.event_mask = WHARF_BUTTON_EVENT_MASK ;
	attr.background_pixel = Scr.asv->black_pixel ;
    w = create_visual_window( Scr.asv, parent->w, -1, -1, 1, 1, 0, InputOutput, CWEventMask|CWBackPixel, &attr );
    register_object( w, (ASMagic*)aswb );
    canvas = create_ascanvas(w);

    if( WharfState.shaped_style )
        set_flags( canvas->state, CANVAS_FORCE_MASK );
    return canvas;
}

ASTBarData *
build_wharf_button_tbar(WharfButton *wb)
{
    ASTBarData *bar = create_astbar();
    int icon_row = 0, icon_col = 0;
    int label_row = 0, label_col = 0;
    int label_flip = get_flags( Config->flags, WHARF_FLIP_LABEL )?FLIP_UPSIDEDOWN:0;
    int label_align = NO_ALIGN ;
#define WLL_Vertical    (0x01<<0)
#define WLL_Opposite    (0x01<<1)
#define WLL_AlignCenter (0x01<<2)
#define WLL_AlignRight  (0x01<<3)
#define WLL_OverIcon    (0x01<<4)

    if( get_flags( Config->flags, WHARF_SHOW_LABEL ) && wb->title )
    {
        if( get_flags(Config->label_location, WLL_Vertical ))
            label_flip |= FLIP_VERTICAL ;
        if( !get_flags(Config->label_location, WLL_OverIcon ))
        {
            if( get_flags(Config->label_location, WLL_Vertical ))
            {
                if( get_flags(Config->label_location, WLL_Opposite ))
                    label_col = 1;
                else
                    icon_col = 1;
            }else if( get_flags(Config->label_location, WLL_Opposite ))
                label_row = 1;
            else
                icon_row = 1;
        }
        if( get_flags(Config->label_location, WLL_AlignCenter ))
            label_align = ALIGN_CENTER ;
        else if( get_flags(Config->label_location, WLL_AlignRight ))
            label_align = ALIGN_RIGHT|ALIGN_TOP ;
        else
            label_align = ALIGN_LEFT|ALIGN_BOTTOM ;
    }
	if( wb->contents )
	{
		char ** icon = NULL ;
		if( wb->selected_content >= 0 && wb->selected_content < wb->contents_num )
			icon = wb->contents[wb->selected_content].icon ;
		if( icon == NULL )
		{
			register int i ;
			for( i = 0 ; i < wb->contents_num ; ++i )
				if( wb->contents[i].icon != NULL )
				{
					icon = wb->contents[i].icon ;
					break;
				}
		}

    	if( icon )
    	{
        	register int i = -1;
        	while( icon[++i] )
        	{
            	ASImage *im = NULL ;
            	/* load image here */
            	im = get_asimage( Scr.image_manager, icon[i], ASFLAGS_EVERYTHING, 100 );
            	if( im )
                	add_astbar_icon( bar, icon_col, icon_row, 0, Config->align_contents, im );
        	}
    	}
	}

    set_astbar_composition_method( bar, BAR_STATE_UNFOCUSED, Config->composition_method );

	if( !get_flags( wb->set_flags, WHARF_BUTTON_TRANSIENT ) )
	{	
    	if( get_flags( Config->flags, WHARF_SHOW_LABEL ) && wb->title )
        	add_astbar_label( bar, label_col, label_row, label_flip, label_align, 2, 2, wb->title, AS_Text_ASCII );
    	
		set_astbar_balloon( bar, 0, wb->title, AS_Text_ASCII );
	}

    set_astbar_style_ptr( bar, BAR_STATE_UNFOCUSED, Scr.Look.MSWindow[BACK_UNFOCUSED] );
	set_astbar_style_ptr( bar, BAR_STATE_FOCUSED, Scr.Look.MSWindow[Scr.Look.MSWindow[BACK_FOCUSED]?BACK_FOCUSED:BACK_UNFOCUSED] );

    LOCAL_DEBUG_OUT( "wharf bevel is %s, value 0x%lX, wharf_no_border is %s",
                        get_flags( Config->set_flags, WHARF_BEVEL )? "set":"unset",
                        Config->bevel,
                        get_flags( Config->flags, WHARF_NO_BORDER )?"set":"unset" );
    if( get_flags( Config->set_flags, WHARF_BEVEL ) )
    {
        if( get_flags( Config->flags, WHARF_NO_BORDER ) )
		{	
            set_astbar_hilite( bar, BAR_STATE_UNFOCUSED, 0 );
            set_astbar_hilite( bar, BAR_STATE_FOCUSED, 0 );
        }else
		{	
            set_astbar_hilite( bar, BAR_STATE_UNFOCUSED, Config->bevel );
            set_astbar_hilite( bar, BAR_STATE_FOCUSED, Config->bevel );
		}
    }else
	{	
       	set_astbar_hilite( bar, BAR_STATE_UNFOCUSED, RIGHT_HILITE|BOTTOM_HILITE );
		set_astbar_hilite( bar, BAR_STATE_FOCUSED, RIGHT_HILITE|BOTTOM_HILITE );
	}
    return bar;
}


/*************************************************************************/
/* Wharf folders :                                                       */

ASWharfFolder *create_wharf_folder( int button_count, ASWharfButton *parent )
{
    ASWharfFolder *aswf = NULL ;
    if( button_count > 0 )
    {
        register int i = button_count;
        aswf = safecalloc( 1, sizeof(ASWharfFolder));
        aswf->magic = MAGIC_WHARF_FOLDER ;
        aswf->buttons = safecalloc( i, sizeof(ASWharfButton));
        aswf->buttons_num = i ;
        while( --i >= 0 )
        {
            aswf->buttons[i].magic = MAGIC_WHARF_BUTTON ;
            aswf->buttons[i].parent = aswf ;
        }
        aswf->parent = parent ;
    }
    return aswf;
}

ASWharfFolder *
build_wharf_folder( WharfButton *list, ASWharfButton *parent, Bool vertical )
{
    ASWharfFolder *aswf = NULL ;
    int count = 0 ;
    WharfButton *wb = list ;
    while( wb )
    {
		Bool disabled = True ;
		int i = 0 ;
		if( get_flags( wb->set_flags, WHARF_BUTTON_TRANSIENT ) )
			disabled = False ;
		else			
		{	
			for( i = 0 ; i < wb->contents_num ; ++i )
			{
				FunctionData *function = wb->contents[i].function ;
LOCAL_DEBUG_OUT( "contents %d has function %p with func = %ld", i, function, function?function->func:-1 );
	        	if( function )
				{
					int func = function->func ;
					if( IsSwallowFunc(func) || IsExecFunc(func) )
					{
			   			disabled = (!is_executable_in_path (function->text));
						if( disabled )
							show_warning( "Application \"%s\" cannot be found in the PATH.", function->text );
					}else
						disabled = False ;
				}
				if( !disabled )
				{
					wb->selected_content = i ;
					break;
				}
			}
			if( wb->folder != NULL )
				disabled = False ;
		}

		if( wb->contents_num == 0 && disabled )
		{
			set_flags( wb->set_flags, WHARF_BUTTON_DISABLED );
			show_warning( "Button \"%s\" has no functions nor folder assigned to it. Button will be disabled", wb->title?wb->title:"-" );
		}else if( disabled )
		{
			set_flags( wb->set_flags, WHARF_BUTTON_DISABLED );
			show_warning( "None of Applications assigned to the button \"%s\" can be found in the PATH. Button will be disabled", wb->title?wb->title:"-" );
		}
		if( !disabled )
			++count ;
        wb = wb->next ;
    }

    if( (aswf = create_wharf_folder(count, parent)) != NULL  )
    {
        ASWharfButton *aswb = aswf->buttons;
        aswf->canvas = create_wharf_folder_canvas(aswf);
        if( vertical )
            set_flags( aswf->flags, ASW_Vertical );
        else
            clear_flags( aswf->flags, ASW_Vertical );

        wb = list;
        while( wb )
        {
			FunctionData *function = NULL ;

			if( get_flags( wb->set_flags, WHARF_BUTTON_DISABLED ) )
			{
				wb = wb->next ;
				continue;
			}

            aswb->name = mystrdup( wb->title );
			if( wb->contents )
				function = wb->contents[wb->selected_content].function ;

            if( function )
            {
                aswb->fdata = safecalloc( 1, sizeof(FunctionData) );
                dup_func_data( aswb->fdata, function );
                if( IsSwallowFunc(aswb->fdata->func) )
                {
                    set_flags( aswb->flags, ASW_SwallowTarget );
                    register_swallow_target( aswb->fdata->name, aswb );
                    if( aswb->fdata->func == F_MaxSwallow ||
                        aswb->fdata->func == F_MaxSwallowModule )
                        set_flags( aswb->flags, ASW_MaxSwallow );
                }
            }
			/* TODO:	Transient buttons are just spacers - they should not 
			 * 			have any balloons displayed on them , nor they should 
			 * 			interpret any clicks
			 */
			if( get_flags( wb->set_flags, WHARF_BUTTON_TRANSIENT ) )
			{
				LOCAL_DEBUG_OUT( "Markip button %p as transient", aswb );	  
				set_flags( aswb->flags, ASW_Transient );
			}

			if( get_flags( wb->set_flags, WHARF_BUTTON_SIZE ) )
			{
				if( wb->width > 0 )
					set_flags( aswb->flags, ASW_FixedWidth );
				if( wb->height > 0 )
					set_flags( aswb->flags, ASW_FixedHeight );
				aswb->desired_width  = wb->width ;
    	        aswb->desired_height = wb->height ;
			}

            aswb->canvas = create_wharf_button_canvas(aswb, aswf->canvas);
            aswb->bar = build_wharf_button_tbar(wb);

            if( !get_flags( aswb->flags, ASW_SwallowTarget ) )
            {
				if( aswb->desired_width == 0 )
	                aswb->desired_width = (Config->force_size.width == 0)?calculate_astbar_width( aswb->bar ) :Config->force_size.width;
				if( aswb->desired_height == 0 )
	            	aswb->desired_height = (Config->force_size.height== 0)?calculate_astbar_height( aswb->bar ):Config->force_size.height;
            }else
            {
    			if( aswb->desired_width == 0 )
	            	aswb->desired_width  = Config->force_size.width ;
    			if( aswb->desired_height == 0 )
	                aswb->desired_height = Config->force_size.height ;
            }

            if( aswb->desired_width == 0 )
                aswb->desired_width = 64 ;

            if( aswb->desired_height == 0 )
                aswb->desired_height = 64 ;

            if( wb->folder )
                aswb->folder = build_wharf_folder( wb->folder, aswb, vertical?False:True );

            ++aswb;
            wb = wb->next ;
        }
        XMapSubwindows( dpy, aswf->canvas->w );
    }
    return aswf;
}

void
destroy_wharf_folder( ASWharfFolder **paswf )
{
    ASWharfFolder *aswf ;
    if( paswf == NULL )
        return;

    if( (aswf = *paswf) == NULL )
        return;
    if( aswf->magic != MAGIC_WHARF_FOLDER )
        return;

    if( aswf->canvas )
        unmap_canvas_window( aswf->canvas );

    if( aswf->buttons )
    {
        int i = aswf->buttons_num;
        while( --i >= 0 )
        {
            ASWharfButton *aswb = &(aswf->buttons[i]);
            if( aswb->name )
                free( aswb->name );
            destroy_astbar(&(aswb->bar));
            if( aswb->canvas )
                unregister_object( aswb->canvas->w );
            destroy_ascanvas(&(aswb->canvas));
            destroy_func_data(&(aswb->fdata));
            destroy_wharf_folder(&(aswb->folder));
        }
        free( aswf->buttons );
    }
    if( aswf->canvas )
        unregister_object( aswf->canvas->w );
    destroy_ascanvas(&(aswf->canvas));

    memset(aswf, 0x00, sizeof(ASWharfFolder));
    free( aswf );
    *paswf = NULL ;
}
				
void set_folder_name( ASWharfFolder *aswf, Bool withdrawn )
{	
	if( withdrawn ) 
	{	
		char *withdrawn_name = safemalloc( strlen(MyName)+ 16 );
		sprintf( withdrawn_name, "%s%s", MyName, "Withdrawn" );
		set_client_names( aswf->canvas->w, withdrawn_name, withdrawn_name, CLASS_WHARF, withdrawn_name );
		free( withdrawn_name );
	}else
		set_client_names( aswf->canvas->w, MyName, MyName, CLASS_WHARF, MyName );
}

Bool
update_wharf_folder_shape( ASWharfFolder *aswf )
{
#ifdef SHAPE
    int i =  aswf->buttons_num;
    int set = 0 ;

    clear_flags( aswf->flags, ASW_Shaped);

    if( (get_flags( Config->flags, WHARF_SHAPE_TO_CONTENTS ) && get_flags( aswf->flags, ASW_NeedsShaping))||
         WharfState.shaped_style || get_flags( aswf->flags, ASW_UseBoundary) )
    {
		if( aswf->canvas->shape )
			flush_vector( aswf->canvas->shape );
		else
			aswf->canvas->shape = create_shape();

		while ( --i >= 0 )
        {
            register ASWharfButton *aswb = &(aswf->buttons[i]);
			LOCAL_DEBUG_OUT( "Adding shape of the button %d with geometry %dx%d%+d%+d, and geometry inside folder %dx%d%+d%+d", 
							 i, aswb->canvas->width, aswb->canvas->height, aswb->canvas->root_x, aswb->canvas->root_y,
							 aswb->folder_width, aswb->folder_height, aswb->folder_x, aswb->folder_y );
            if( aswb->canvas->width == aswb->folder_width && aswb->canvas->height == aswb->folder_height )
                if( combine_canvas_shape_at (aswf->canvas, aswb->canvas, aswb->folder_x, aswb->folder_y ) )
                    ++set;
            if( aswb->swallowed )
            {
                if( combine_canvas_shape_at (aswf->canvas, aswb->swallowed->current, aswb->folder_x, aswb->folder_y ) )
                    ++set;
            }
        }
		if( get_flags( aswf->flags, ASW_UseBoundary) )
		{
			XRectangle sr ;
			int do_subtract = 0 ;
			sr = aswf->boundary ;
			LOCAL_DEBUG_OUT( "boundary = %dx%d%+d%+d, canvas = %dx%d\n", 
						     sr.width, sr.height, sr.x, sr.y, aswf->canvas->width, aswf->canvas->height ); 
			
			if( sr.width < aswf->canvas->width ) 
			{	
				if( sr.x > 0 )
				{	 
					sr.width = sr.x ;
					sr.x = 0 ;
				}else 
				{
					sr.x = sr.width ;
					sr.width = aswf->canvas->width - sr.width ;
				}
				++do_subtract ;
			}
			
			if( sr.height < aswf->canvas->height ) 
			{	
				if( sr.y > 0 ) 
				{	
					sr.height = sr.y ;
					sr.y = 0 ;
				}else 
				{
					sr.y = sr.height ;
					sr.height = aswf->canvas->height - sr.height ;
				}
				++do_subtract ;
			}
#if 0
			fprintf( stderr, "%s: substr_boundary = %dx%d%+d%+d canvas = %dx%d%+d%+d\n", 
					 __FUNCTION__, sr.width, sr.height, sr.x, sr.y,  
					 aswf->canvas->width, aswf->canvas->height, aswf->canvas->root_x, aswf->canvas->root_y );
			fprintf( stderr, "shape = %p, used = %d\n", aswf->canvas->shape, PVECTOR_USED(aswf->canvas->shape) );
#endif			
			if( sr.width > 0 && sr.height > 0 && do_subtract > 0 ) 
				subtract_shape_rectangle( aswf->canvas->shape, &sr, 1, 0, 0, aswf->canvas->width, aswf->canvas->height );
		}	 

		update_canvas_display_mask (aswf->canvas, True);

        if( set > 0 )
            set_flags( aswf->flags, ASW_Shaped);
		return True;
    }
#endif
	return False;
}

void
update_wharf_folder_transprency( ASWharfFolder *aswf, Bool force )
{
	if( aswf )
	{
		int i = aswf->buttons_num;
		while( --i >= 0 )
		{
			ASWharfButton *aswb= &(aswf->buttons[i]);
			if( update_astbar_transparency( aswb->bar, aswb->canvas, force ) )
			{
				render_wharf_button( aswb );
				update_canvas_display( aswb->canvas );
			}
			if( aswb->folder && get_flags( aswb->folder->flags, ASW_Mapped )  );
				update_wharf_folder_transprency( aswb->folder, force );
		}
		/*update_wharf_folder_shape( aswf ); */
	}
}

void 
change_button_focus(ASWharfButton *aswb, Bool focused )
{
	
	if( aswb == NULL && focused && Scr.Look.MSWindow[BACK_FOCUSED] == NULL ) 
		return ;
	
	set_astbar_focused( aswb->bar, NULL, focused );			   
	render_wharf_button( aswb );
	update_canvas_display( aswb->canvas );
	update_wharf_folder_shape( aswb->parent );

	if( focused ) 
		WharfState.focused_button = aswb ;
	else if( WharfState.focused_button == aswb )
		WharfState.focused_button = NULL ;
}	 

void
update_wharf_folder_styles( ASWharfFolder *aswf, Bool force )
{
	if( aswf )
	{
		int i = aswf->buttons_num;
		while( --i >= 0 )
		{
			ASWharfButton *aswb= &(aswf->buttons[i]);
			set_astbar_style_ptr( aswb->bar, BAR_STATE_FOCUSED, Scr.Look.MSWindow[Scr.Look.MSWindow[BACK_FOCUSED]?BACK_FOCUSED:BACK_UNFOCUSED] );
			if( set_astbar_style_ptr( aswb->bar, BAR_STATE_UNFOCUSED, Scr.Look.MSWindow[BACK_UNFOCUSED] ))
			{
				render_wharf_button( aswb );
				update_canvas_display( aswb->canvas );
			}
			if( aswb->folder && get_flags( aswb->folder->flags, ASW_Mapped )  );
				update_wharf_folder_styles( aswb->folder, force );
		}
		update_wharf_folder_shape( aswf );
	}
}

void
map_wharf_folder( ASWharfFolder *aswf,
                  int gravity )
{
    XSizeHints      shints;
	ExtendedWMHints extwm_hints ;
    ASFlagType protocols = 0;

	set_folder_name( aswf, False );

    shints.flags = USSize|PWinGravity;
    if( get_flags( Config->set_flags, WHARF_GEOMETRY ) )
        shints.flags |= USPosition ;
    else
        shints.flags |= PPosition ;

    shints.win_gravity = gravity ;
	aswf->gravity = gravity ;

	extwm_hints.pid = getpid();
    extwm_hints.flags = EXTWM_PID|EXTWM_StateSkipTaskbar|EXTWM_TypeDock ;

    if( aswf != WharfState.root_folder )
	{
        XSetTransientForHint(dpy, aswf->canvas->w, WharfState.root_folder->canvas->w);
		extwm_hints.flags |=  EXTWM_TypeDialog ;
    }else
        protocols = AS_DoesWmDeleteWindow ;

    set_client_hints( aswf->canvas->w, NULL, &shints, AS_DoesWmDeleteWindow, &extwm_hints );

//	ASSync(False);
//	sleep_a_millisec (10);
	/* showing window to let user see that we are doing something */
    map_canvas_window(aswf->canvas, True);
#ifdef SHAPE
	XShapeCombineRectangles ( dpy, aswf->canvas->w, ShapeBounding, 0, 0, &(aswf->boundary), 1, ShapeSet, Unsorted);
#endif
    LOCAL_DEBUG_OUT( "mapping folder window for folder %p", aswf );
    /* final cleanup */
//    ASSync( False );
//    sleep_a_millisec (10);                                 /* we have to give AS a chance to spot us */
}

void
place_wharf_buttons( ASWharfFolder *aswf, int *total_width_return, int *total_height_return )
{
    int max_width = 0, max_height = 0 ;
    int x = 0, y = 0 ;
    int i;
    Bool fit_contents = get_flags(Config->flags, WHARF_FIT_CONTENTS);
    Bool needs_shaping = False ;
	Bool reverse_order = get_flags( aswf->flags, ASW_ReverseOrder )?aswf->buttons_num-1:-1;

    *total_width_return  = 0 ;
	*total_height_return = 0 ;

    LOCAL_DEBUG_OUT( "flags 0x%lX, reverse_order = %d", aswf->flags, reverse_order );
    if( get_flags( aswf->flags, ASW_Vertical ) )
    {
        int columns = (aswf == WharfState.root_folder)?Config->columns:1;
        int buttons_per_column = (aswf->buttons_num + columns - 1) / columns, bc = 0 ;

        for( i = 0 ; i < aswf->buttons_num ; ++i )
        {
            ASWharfButton *aswb = &(aswf->buttons[reverse_order>=0?reverse_order-i:i]);
            int height ;

            if( bc == 0 )
            {
                register int k = i+buttons_per_column ;
                if( k > aswf->buttons_num )
                    k = aswf->buttons_num ;
                max_width = 0;
                max_height = 0;
                while( --k >= i )
                {
                    register ASWharfButton *aswb = &(aswf->buttons[reverse_order>=0?reverse_order-k:k]);
                    if( max_width < aswb->desired_width )
                        max_width = aswb->desired_width ;
                    if( max_height < aswb->desired_height && !get_flags( aswb->flags, ASW_MaxSwallow ) )
                        max_height = aswb->desired_height ;
                }
                LOCAL_DEBUG_OUT( "max_size(%dx%d)", max_width, max_height );
            }
            height = (get_flags( aswb->flags, ASW_MaxSwallow ) || fit_contents )?aswb->desired_height:max_height ;
            if( get_flags(Config->flags, WHARF_SHAPE_TO_CONTENTS) )
            {
                int dx = max_width - aswb->desired_width ;
                int dy = height - aswb->desired_height ;
                if( get_flags( Config->align_contents, ALIGN_RIGHT ) == 0 )
                    dx = 0 ;
                else if( get_flags( Config->align_contents, ALIGN_LEFT ))
                    dx = dx>>1 ;
                if( get_flags( Config->align_contents, ALIGN_BOTTOM ) == 0 )
                    dy = 0 ;
                else if( get_flags( Config->align_contents, ALIGN_TOP ))
                    dy = dy>>1 ;
                aswb->folder_x = x+dx ;
                aswb->folder_y = y+dy;
                aswb->folder_width = aswb->desired_width ;
                aswb->folder_height = aswb->desired_height;
                if( aswb->desired_width != max_width )
                    needs_shaping = True;
            }else
            {
                aswb->folder_x = x;
                aswb->folder_y = y;
                aswb->folder_width = max_width ;
                aswb->folder_height = height;
            }
            moveresize_canvas( aswb->canvas, aswb->folder_x, aswb->folder_y, aswb->folder_width, aswb->folder_height );
            y += height ;
            if( ++bc >= buttons_per_column )
            {
                *total_width_return += max_width ;
                if( *total_height_return < y )
                    *total_height_return = y ;
                x += max_width ;
                y = 0 ;
                bc = 0;
            }
        }
        if( columns * buttons_per_column > aswf->buttons_num )
        {
            *total_width_return += max_width ;
            if( *total_height_return < y )
                *total_height_return = y ;
        }

    }else
    {
        int rows = (aswf == WharfState.root_folder)?Config->rows:1;
        int buttons_per_row = (aswf->buttons_num + rows - 1) / rows, br = 0 ;

        for( i = 0 ; i < aswf->buttons_num ; ++i )
        {
            ASWharfButton *aswb = &(aswf->buttons[reverse_order>=0?reverse_order-i:i]);
            int width ;

            if( br == 0 )
            {
                register int k = i+buttons_per_row ;
                if( k > aswf->buttons_num )
                    k = aswf->buttons_num ;
                max_width = 0;
                max_height = 0;
                while( --k >= i )
                {
                    register ASWharfButton *aswb = &(aswf->buttons[reverse_order>=0?reverse_order-k:k]);
                    if( max_width < aswb->desired_width && !get_flags( aswb->flags, ASW_MaxSwallow ) )
                        max_width = aswb->desired_width ;
                    if( max_height < aswb->desired_height )
                        max_height = aswb->desired_height ;
                }
                LOCAL_DEBUG_OUT( "max_size(%dx%d)", max_width, max_height );
            }

            width = (get_flags( aswb->flags, ASW_MaxSwallow )|| fit_contents )?aswb->desired_width:max_width ;
            if( get_flags(Config->flags, WHARF_SHAPE_TO_CONTENTS) )
            {
                int dx = width - aswb->desired_width ;
                int dy = max_height - aswb->desired_height ;
                if( get_flags( Config->align_contents, ALIGN_RIGHT ) == 0 )
                    dx = 0 ;
                else if( get_flags( Config->align_contents, ALIGN_LEFT ))
                    dx = dx>>1 ;
                if( get_flags( Config->align_contents, ALIGN_BOTTOM ) == 0 )
                    dy = 0 ;
                else if( get_flags( Config->align_contents, ALIGN_TOP ))
                    dy = dy>>1 ;
                aswb->folder_x = x+dx ;
                aswb->folder_y = y+dy;
                aswb->folder_width = aswb->desired_width ;
                aswb->folder_height = aswb->desired_height;
                if( aswb->desired_height != max_height )
                    needs_shaping = True;
            }else
            {
                aswb->folder_x = x ;
                aswb->folder_y = y ;
                aswb->folder_width = width ;
                aswb->folder_height = max_height;
            }
            moveresize_canvas( aswb->canvas, aswb->folder_x, aswb->folder_y, aswb->folder_width, aswb->folder_height );
            x += width;
            if( ++br >= buttons_per_row )
            {
                if( *total_width_return < x )
                    *total_width_return = x ;
                *total_height_return += max_height ;
                x = 0 ;
                y += max_height ;
                br = 0;
            }
        }
        if( rows * buttons_per_row > aswf->buttons_num )
        {
            if( *total_width_return < x )
                *total_width_return = x ;
            *total_height_return += max_height ;
        }
    }
    LOCAL_DEBUG_OUT( "total_size_return(%dx%d)", *total_width_return, *total_height_return );

    ASSync( False );
	WharfState.buttons_render_pending = aswf->buttons_num ;
    if( needs_shaping )
        set_flags( aswf->flags, ASW_NeedsShaping );
    else
        clear_flags( aswf->flags, ASW_NeedsShaping );
}


void
clamp_animation_rect( ASWharfFolder *aswf, int from_width, int from_height, int to_width, int to_height, XRectangle *rect )
{
	if( get_flags( aswf->flags, ASW_Vertical ) )
	{	
		if( aswf->gravity == SouthWestGravity || aswf->gravity == SouthEastGravity )
			rect->y = max(to_height,from_height) - (short)rect->height ;
		if( rect->y < 0 ) 
		{	
			rect->y = 0 ; 
			rect->height = to_height ;
		}
	}else 
	{	
		if( aswf->gravity == NorthEastGravity || aswf->gravity == SouthEastGravity )
			rect->x = max(to_width,from_width) - (short)rect->width ;
		if( rect->x < 0 ) 
		{	
			rect->x = 0 ; 
			rect->width = to_width ;
		}
	}
}	 

void
animate_wharf_loop(ASWharfFolder *aswf, int from_width, int from_height, int to_width, int to_height )	
{
	int i, steps ;
	XRectangle rect ;

	steps = get_flags( Config->set_flags, WHARF_ANIMATE_STEPS )?Config->animate_steps:12;
	LOCAL_DEBUG_OUT( "steps = %d", steps );
	for( i = 0 ; i < steps ; ++i )
	{
		rect.x = rect.y = 0 ;		
		rect.width = get_flags( aswf->flags, ASW_Vertical )?to_width:from_width+((to_width-from_width)/steps)*(i+1) ;
		rect.height = get_flags( aswf->flags, ASW_Vertical )?from_height+((to_height-from_height)/steps)*(i+1):to_height ;

		clamp_animation_rect( aswf, from_width, from_height, to_width, to_height, &rect );
		
		LOCAL_DEBUG_OUT("boundary = %dx%d%+d%+d, canvas = %dx%d\n", 
						 rect.width, rect.height, rect.x, rect.y, 
						 aswf->canvas->width, aswf->canvas->height );
		if( rect.x+rect.width > aswf->canvas->width ||
			rect.y+rect.height > aswf->canvas->height ) 
		{
			return;			
		} 

		   
		aswf->boundary = rect ;
		update_wharf_folder_shape( aswf );
		ASSync(False);
		
		if( get_flags( Config->set_flags, WHARF_ANIMATE_DELAY ) && Config->animate_delay > 0 )
 			sleep_a_millisec(Config->animate_delay*60);	  
		else
			sleep_a_millisec(50);
	}	 

	rect.x = rect.y = 0 ;
	rect.width = to_width ;
	rect.height = to_height ;
	clamp_animation_rect( aswf, from_width, from_height, to_width, to_height, &rect );
	
	LOCAL_DEBUG_OUT("boundary = %dx%d%+d%+d, canvas = %dx%d\n", 
					rect.width, rect.height, rect.x, rect.y, aswf->canvas->width, aswf->canvas->height );

	aswf->boundary = rect ;
	update_wharf_folder_shape( aswf );
}

void
animate_wharf( ASWharfFolder *aswf, int *new_width_return, int *new_height_return )
{
    int new_width = aswf->canvas->width ;
    int new_height = aswf->canvas->height ;

    if( aswf->animation_dir < 0 )
    {
        if( get_flags( aswf->flags, ASW_Vertical ) )
        {
            if( aswf->animation_steps <= 1 )
                new_height = 0 ;
            else
            {
                new_height = (new_height*(aswf->animation_steps-1))/aswf->animation_steps ;
                if( new_height == aswf->canvas->height )
                    --new_height ;
            }
        }else
        {
            if( aswf->animation_steps <= 1 )
                new_width =0 ;
            else
            {
                new_width = (new_width*(aswf->animation_steps-1))/aswf->animation_steps;
                if( new_width == aswf->canvas->width )
                    --new_width;
            }
        }
    }else
    {
        new_width = aswf->total_width ;
        new_height = aswf->total_height ;
        if( get_flags( aswf->flags, ASW_Vertical ) )
        {
            if( aswf->animation_steps <= 1 )
                new_height = aswf->total_height ;
            else
            {
                new_height = aswf->canvas->height+((aswf->total_height - aswf->canvas->height)/aswf->animation_steps)  ;
                if( new_height == aswf->canvas->height && new_height < aswf->total_height )
                    ++new_height;
            }
        }else
        {
            if( aswf->animation_steps <= 1 )
                new_width = aswf->total_width ;
            else
            {
                new_width = aswf->canvas->width+((aswf->total_width - aswf->canvas->width)/aswf->animation_steps)  ;
                if( new_width == aswf->canvas->width && new_width < aswf->total_width )
                    ++new_width;
            }
        }
    }
    --(aswf->animation_steps);

    *new_width_return = new_width ;
    *new_height_return = new_height ;
}

Bool
display_wharf_folder( ASWharfFolder *aswf, int left, int top, int right, int bottom )
{
    Bool east  = get_flags( Config->geometry.flags, XNegative);
    Bool south = get_flags( Config->geometry.flags, YNegative);
    int x, y, width = 0, height = 0;
    int total_width = 0, total_height = 0;
    if( AS_ASSERT( aswf ) ||
        (get_flags( aswf->flags, ASW_Mapped ) && !get_flags( aswf->flags, ASW_Withdrawn )) )
        return False;

	if( aswf != WharfState.root_folder )
	{
  		if( get_flags( aswf->flags, ASW_Vertical ) )
	    {
		  	if( south )
				set_flags( aswf->flags, ASW_ReverseOrder );
	    }else
  		{
			if( east )
				set_flags( aswf->flags, ASW_ReverseOrder );
		}
	}
    place_wharf_buttons( aswf, &total_width, &total_height );

    if( total_width == 0 || total_height == 0 )
        return False;;
    if( total_width > Scr.MyDisplayWidth )
        total_width = Scr.MyDisplayWidth;
    if( total_height > Scr.MyDisplayHeight )
        total_height = Scr.MyDisplayHeight;

    aswf->total_width = total_width ;
    aswf->total_height = total_height ;

    if( left < 0 )
        east = False;
    else if( left < total_width )
        east = !( Scr.MyDisplayWidth > right && (Scr.MyDisplayWidth-right > total_width || left < Scr.MyDisplayWidth-right));

    if( east )
    {
        if( left < total_width )
            left = total_width ;
    }else if( Scr.MyDisplayWidth-right  < total_width )
        right = Scr.MyDisplayWidth - total_width ;

    if( south )
    {
        if( top < total_height )
            top = total_height ;
    }else if( Scr.MyDisplayHeight - bottom < total_height )
        bottom = Scr.MyDisplayHeight - total_width ;

    if( south && top < total_height )
        south = !( Scr.MyDisplayHeight > bottom && (Scr.MyDisplayHeight-bottom > total_height || top < Scr.MyDisplayHeight-bottom));

#if 0    
	if( aswf->animation_steps > 0 )
    {
        aswf->animation_dir = 1;
        aswf->canvas->width = aswf->canvas->height = 1 ;
        animate_wharf( aswf, &width, &height );
    }
#endif
    if( width == 0 )
        width = total_width ;
    if( height == 0 )
        height = total_height ;
    LOCAL_DEBUG_OUT( "animation_steps(%d)->size(%dx%d)->total_size(%dx%d)", aswf->animation_steps, width, height, total_width, total_height );

    if( get_flags( aswf->flags, ASW_Vertical ) )
    {
        x = east? right - width: left ;
        y = south? top - height : bottom ;
        if( top != bottom )
            y += south?5:-5 ;
    }else
    {
        x = east? left - width : right ;
        y = south? bottom - height: top ;
        if( left != right)
            x += east?5:-5 ;
    }
	LOCAL_DEBUG_OUT("calculated pos(%+d%+d), east(%d), south(%d), total_size(%dx%d)", x, y, east, south, total_width, total_height );
	if( east )
	{
		if( x + width > Scr.MyDisplayWidth )
			x = Scr.MyDisplayWidth - width ;
	}else
	{
		if( x + total_width > Scr.MyDisplayWidth )
			x = Scr.MyDisplayWidth - total_width ;
	}
	if( south )
	{
		if( y + height > Scr.MyDisplayHeight )
			y = Scr.MyDisplayHeight - height ;
	}else
	{
		if( y + total_height > Scr.MyDisplayHeight )
			y = Scr.MyDisplayHeight - total_height ;
	}
    /* if user has configured us so that we'll have to overlap ourselves -
	   then its theirs fault - we cannot account for all situations */

	LOCAL_DEBUG_OUT("corrected  pos(%+d%+d)", x, y );
    LOCAL_DEBUG_OUT( "flags 0x%lX, reverse_order = %d", aswf->flags, get_flags( aswf->flags, ASW_ReverseOrder)?aswf->buttons_num-1:-1 );

	moveresize_canvas( aswf->canvas, x, y, width, height );
	set_wharf_clip_area( aswf, left, top );
    if( get_flags(Config->flags, WHARF_ANIMATE ) )
    {
		set_flags(aswf->flags,ASW_UseBoundary|ASW_AnimationPending );
		aswf->animate_from_w = get_flags( aswf->flags, ASW_Vertical )?aswf->canvas->width:0; 
		aswf->animate_from_h = get_flags( aswf->flags, ASW_Vertical )?0:aswf->canvas->height;
		aswf->animate_to_w = width;
		aswf->animate_to_h = height;
		aswf->boundary.x = aswf->boundary.y = 0 ; 
		if( get_flags( aswf->flags, ASW_Vertical ) )
		{		
			if( south )
				aswf->boundary.y = height-1 ; 
			aswf->boundary.height = 1 ;
			aswf->boundary.width = 1 ;
		}else
		{
			if( east )
				aswf->boundary.x = width-1 ; 
			aswf->boundary.height = 1 ;
			aswf->boundary.width = 1 ;

/* 			aswf->boundary.width = (aswf->gravity == NorthWestGravity || aswf->gravity == SouthWestGravity )?aswf->canvas->width:1 ;
			aswf->boundary.height = (aswf->boundary.width == 1)?aswf->canvas->height:1 ;
 */ 
		}	 
#ifdef SHAPE		
		LOCAL_DEBUG_OUT("boundary pos(%dx%d%+d%+d) shaping window %lX", aswf->boundary.width, aswf->boundary.height, aswf->boundary.x, aswf->boundary.y, aswf->canvas->w );
		/* fprintf( stderr, "setting boundary to 1x1\n" );  */
	    XShapeCombineRectangles ( dpy, aswf->canvas->w, ShapeBounding, 0, 0, &(aswf->boundary), 1, ShapeSet, Unsorted);
#endif
    }
    
	map_wharf_folder( aswf, east?(south?SouthEastGravity:NorthEastGravity):
                                 (south?SouthWestGravity:NorthWestGravity) );

	set_flags( aswf->flags, ASW_Mapped );
    clear_flags( aswf->flags, ASW_Withdrawn );
	ASSync(False);

	if( aswf->canvas->width == width && aswf->canvas->height == height )
	{
		animate_wharf_loop( aswf, aswf->animate_from_w, aswf->animate_from_h, aswf->animate_to_w, aswf->animate_to_h );
		clear_flags(aswf->flags,ASW_UseBoundary|ASW_AnimationPending );
	}	 

    return True;
}

static inline void unmap_wharf_folder( ASWharfFolder *aswf );

static inline void
unmap_wharf_subfolders( ASWharfFolder *aswf )
{
    int i = aswf->buttons_num;
    while ( --i >= 0 )
    {
        if( aswf->buttons[i].folder &&
            get_flags( aswf->buttons[i].folder->flags, ASW_Mapped ) )
            unmap_wharf_folder( aswf->buttons[i].folder );
    }
}

static inline void
unmap_wharf_folder( ASWharfFolder *aswf )
{
LOCAL_DEBUG_OUT( "unmapping canvas %p at %dx%d%+d%+d", aswf->canvas, aswf->canvas->width, aswf->canvas->height, aswf->canvas->root_x, aswf->canvas->root_y );
    unmap_canvas_window( aswf->canvas );
    /*moveresize_canvas( aswf->canvas, -1000, -1000, 1, 1 ); to make sure we get ConfigureNotify next time we map the folder again */
    clear_flags( aswf->flags, ASW_Mapped );

	unmap_wharf_subfolders( aswf );
}

static inline void
withdraw_wharf_subfolders( ASWharfFolder *aswf );

void
withdraw_wharf_folder( ASWharfFolder *aswf )
{
LOCAL_DEBUG_OUT( "withdrawing folder %p", aswf );
    if( AS_ASSERT(aswf) )
        return;
LOCAL_DEBUG_OUT( "folder->flags(%lX)", aswf->flags );
    if( !get_flags( aswf->flags, ASW_Mapped ) )
        return ;

	withdraw_wharf_subfolders( aswf );
	ASSync(False);

    if( get_flags(Config->flags, WHARF_ANIMATE ) )
    {
		set_flags(aswf->flags,ASW_UseBoundary );
		aswf->boundary.x = aswf->boundary.y = 0 ; 
		aswf->boundary.width = aswf->canvas->width ;
		aswf->boundary.height = aswf->canvas->height ;
		
		if( get_flags(aswf->flags,ASW_Vertical) )
			animate_wharf_loop(aswf, aswf->canvas->width, aswf->canvas->height, aswf->canvas->width, 1 );
		else
			animate_wharf_loop(aswf, aswf->canvas->width, aswf->canvas->height, 1, aswf->canvas->height );
    }
LOCAL_DEBUG_OUT( "unmapping folder %p", aswf );
    unmap_wharf_folder( aswf );
    ASSync( False );
	sleep_a_millisec( 10 );
}

static inline void
withdraw_wharf_subfolders( ASWharfFolder *aswf )
{
    int i = aswf->buttons_num;
    while ( --i >= 0 )
    {
        if( aswf->buttons[i].folder &&
            get_flags( aswf->buttons[i].folder->flags, ASW_Mapped ) )
            withdraw_wharf_folder( aswf->buttons[i].folder );
    }
}



Bool display_main_folder()
{
    int left = Config->geometry.x;
    int top = Config->geometry.y;

    if( get_flags( Config->geometry.flags, XNegative ))
        left += Scr.MyDisplayWidth ;

    if( get_flags( Config->geometry.flags, YNegative ))
        top += Scr.MyDisplayHeight ;
    return display_wharf_folder( WharfState.root_folder, left, top, left, top );
}

Bool
check_pending_swallow( ASWharfFolder *aswf )
{
    if( aswf )
    {
        int i = aswf->buttons_num ;
        /* checking buttons : */
        while( --i >= 0 )
            if( get_flags(aswf->buttons[i].flags, ASW_SwallowTarget ) )
                return True;
        /* checking subfolders : */
        i = aswf->buttons_num ;
        while( --i >= 0 )
            if( aswf->buttons[i].folder )
                if( check_pending_swallow( aswf->buttons[i].folder ) )
                    return True;
    }
    return False;
}

void
exec_pending_swallow( ASWharfFolder *aswf )
{
    if( aswf )
    {
        int i = aswf->buttons_num ;
        while( --i >= 0 )
        {
            if( get_flags(aswf->buttons[i].flags, ASW_SwallowTarget ) &&
                aswf->buttons[i].fdata &&
                aswf->buttons[i].swallowed == NULL )
            {
                SendCommand( aswf->buttons[i].fdata, 0);
            }
            if( aswf->buttons[i].folder )
                exec_pending_swallow( aswf->buttons[i].folder );
        }
    }
}

void
grab_swallowed_canvas_btns( ASCanvas *canvas, Bool action, Bool withdraw )
{
	register int i = 0 ;
	register unsigned int *mods = lock_mods;

	LOCAL_DEBUG_OUT( "%p,%d,%d", canvas, action, withdraw );

	if( AS_ASSERT(canvas) )
		return;
    do
    {
        /* grab button 1 if this button performs an action */
        if( action )
		{	
            XGrabButton (dpy, Button1, mods[i],
                        canvas->w,
                        False, ButtonPressMask | ButtonReleaseMask,
                        GrabModeAsync, GrabModeAsync, None, None);
#if !defined(NO_DEBUG_OUTPUT)			
			ASSync(False);
			fprintf( stderr, "line = %d, mods = 0x%X\n", __LINE__, mods[i] );
#endif
		}
        /* grab button 3 if this is the root folder */
        if (withdraw )
		{	
            XGrabButton (dpy, Button3, mods[i],
                        canvas->w,
                        False, ButtonPressMask | ButtonReleaseMask,
                        GrabModeAsync, GrabModeAsync, None, None);
#if !defined(NO_DEBUG_OUTPUT)			   
			ASSync(False);
			fprintf( stderr, "line = %d, canvas = %p, window = %lX, i = %d, mods = 0x%X\n", __LINE__, canvas, canvas->w, i, mods[i] );
#endif
		}
		if( mods[i] == 0 )
			return;
    }while (++i < MAX_LOCK_MODS );

	if( action )
	{	
        XGrabButton (dpy, Button1, 0,
                    canvas->w,
                    False, ButtonPressMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None);
#if !defined(NO_DEBUG_OUTPUT)			   		
		ASSync(False);
		fprintf( stderr, "line = %d, mods = 0\n", __LINE__ );
#endif
	}
    /* grab button 3 if this is the root folder */
    if (withdraw )
	{	
        XGrabButton (dpy, Button3, 0,
                    canvas->w,
                    False, ButtonPressMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None);
#if !defined(NO_DEBUG_OUTPUT)			   			
			ASSync(False);
			fprintf( stderr, "line = %d, mods = 0", __LINE__ );
#endif
	}

}

void
update_wharf_folder_size( ASWharfFolder *aswf)
{
    unsigned int total_width = 1, total_height = 1;
    if( !get_flags( aswf->flags, ASW_Mapped) )
		return;		
	place_wharf_buttons( aswf, &total_width, &total_height );

    if( total_width != 0 && total_height != 0 )
    {
        if( total_width > Scr.MyDisplayWidth )
            total_width = Scr.MyDisplayWidth;
        if( total_height > Scr.MyDisplayHeight )
            total_height = Scr.MyDisplayHeight;

        aswf->total_width = total_width ;
        aswf->total_height = total_height ;
    }
    resize_canvas( aswf->canvas, total_width, total_height );
}

void
send_swallowed_configure_notify(ASWharfButton *aswb)
{
LOCAL_DEBUG_CALLER_OUT( "%p,%p", aswb, aswb->swallowed );
    if( aswb->swallowed )
    {
		send_canvas_configure_notify(aswb->canvas, aswb->swallowed->current );
    }
}



void
check_swallow_window( ASWindowData *wd )
{
    ASWharfFolder *aswf = NULL ;
    ASWharfButton *aswb = NULL ;
    Window w;
    int try_num = 0 ;
	Bool withdraw_btn ;
    ASCanvas *nc ;
    int swidth, sheight ;

    if( wd == NULL && !get_flags( wd->state_flags, AS_Mapped))
        return;
    LOCAL_DEBUG_OUT( "name(\"%s\")->icon_name(\"%s\")->res_class(\"%s\")->res_name(\"%s\")",
                     wd->window_name, wd->icon_name, wd->res_class, wd->res_name );
    if( (aswb = fetch_swallow_target( wd->window_name )) == NULL )
        if( (aswb = fetch_swallow_target( wd->icon_name )) == NULL )
            if( (aswb = fetch_swallow_target( wd->res_class )) == NULL )
                if( (aswb = fetch_swallow_target( wd->res_name )) == NULL )
                    return ;
    LOCAL_DEBUG_OUT( "swallow target is %p, swallowed = %p", aswb, aswb->swallowed );
	aswf = aswb->parent ;
    if( aswb->swallowed != NULL )
        return;
    /* do the actuall swallowing here : */
    grab_server();
    /* first lets check if window is still not swallowed : it should have no more then 2 parents before root */
    w = get_parent_window( wd->client );
    LOCAL_DEBUG_OUT( "first parent %lX, root %lX", w, Scr.Root );
	while( w == Scr.Root && ++try_num  < 10 )
	{/* we should wait for AfterSTep to complete AddWindow protocol */
	    /* do the actuall swallowing here : */
    	ungrab_server();
		sleep_a_millisec(200*try_num);
		grab_server();
		w = get_parent_window( wd->client );
		LOCAL_DEBUG_OUT( "attempt %d:first parent %lX, root %lX", try_num, w, Scr.Root );
	}
	if( w == Scr.Root )
	{
		ungrab_server();
		return ;
	}
    if( w != None )
        w = get_parent_window( w );
    LOCAL_DEBUG_OUT( "second parent %lX, root %lX", w, Scr.Root );
    if( w != Scr.Root )
	{
		ungrab_server();
		return ;
	}
    withdraw_btn = (WITHDRAW_ON_EDGE(Config) &&
					(&(aswf->buttons[0]) == aswb || &(aswf->buttons[aswf->buttons_num-1]) == aswb)) ||
                    WITHDRAW_ON_ANY(Config) ;
    /* its ok - we can swallow it now : */
    /* create swallow object : */
    aswb->swallowed = safecalloc( 1, sizeof(ASSwallowed ));
    /* first thing - we reparent window and its icon if there is any */
    nc = aswb->swallowed->normal = create_ascanvas_container( wd->client );
    XReparentWindow( dpy, wd->client, aswb->canvas->w, (aswb->canvas->width - nc->width)/2, (aswb->canvas->height - nc->height)/2 );
    register_object( wd->client, (ASMagic*)aswb );
    XSelectInput (dpy, wd->client, StructureNotifyMask|LeaveWindowMask|EnterWindowMask);
	ASSync(False);
    ungrab_server();
	ASSync(False);
    sleep_a_millisec(100);
	grab_swallowed_canvas_btns( nc, (aswb->folder!=NULL), withdraw_btn && aswb->parent == WharfState.root_folder);

    if( get_flags( wd->flags, AS_ClientIcon ) && !get_flags( wd->flags, AS_ClientIconPixmap) &&
		wd->icon != None )
    {
        ASCanvas *ic = create_ascanvas_container( wd->icon );
        aswb->swallowed->iconic = ic;
        XReparentWindow( dpy, wd->icon, aswb->canvas->w, (aswb->canvas->width-ic->width)/2, (aswb->canvas->height-ic->height)/2 );
        register_object( wd->icon, (ASMagic*)aswb );
        XSelectInput (dpy, wd->icon, StructureNotifyMask);
		ASSync(False);
        grab_swallowed_canvas_btns(  ic, (aswb->folder!=NULL), withdraw_btn && aswb->parent == WharfState.root_folder);
    }
    aswb->swallowed->current = ( get_flags( wd->state_flags, AS_Iconic ) &&
                                    aswb->swallowed->iconic != NULL )?
                                aswb->swallowed->iconic:aswb->swallowed->normal;
    handle_canvas_config( aswb->swallowed->current );
    LOCAL_DEBUG_OUT( "client(%lX)->icon(%lX)->current(%lX)", wd->client, wd->icon, aswb->swallowed->current->w );

    if( get_flags( aswb->flags, ASW_MaxSwallow ) ||
		(Config->force_size.width == 0 && !get_flags(aswb->flags, ASW_FixedWidth)))
        aswb->desired_width = aswb->swallowed->current->width;
    if( get_flags( aswb->flags, ASW_MaxSwallow ) ||
		(Config->force_size.height == 0 && !get_flags(aswb->flags, ASW_FixedHeight)) )
        aswb->desired_height = aswb->swallowed->current->height;
    swidth = min( aswb->desired_width, aswb->swallowed->current->width );
    sheight = min( aswb->desired_height, aswb->swallowed->current->height );
    moveresize_canvas( aswb->swallowed->current,
                       make_tile_pad( get_flags(Config->align_contents,PAD_LEFT),
                                      get_flags(Config->align_contents,PAD_RIGHT),
                                      aswb->canvas->width, swidth      ),
                       make_tile_pad( get_flags(Config->align_contents,PAD_TOP),
                                      get_flags(Config->align_contents,PAD_BOTTOM),
                                      aswb->canvas->height, sheight    ),
                       swidth, sheight );
    map_canvas_window( aswb->swallowed->current, True );
    send_swallowed_configure_notify(aswb);
	update_wharf_folder_size( aswf );
}


/*************************************************************************/
/* Event handling                                                        */
/*************************************************************************/
void
on_wharf_button_confreq( ASWharfButton *aswb, ASEvent *event )
{
	if( aswb && aswb->swallowed )
	{
		XConfigureRequestEvent *cre = &(event->x.xconfigurerequest);
	    XWindowChanges xwc;
        unsigned long xwcm;
		int re_width = aswb->desired_width ;
		int re_height = aswb->desired_height ;

        xwcm = CWX | CWY | CWWidth | CWHeight | (cre->value_mask&CWBorderWidth);

		if( get_flags( cre->value_mask, CWWidth )  )
		{
			if( get_flags( aswb->flags, ASW_MaxSwallow ) ||
				(Config->force_size.width == 0 && !get_flags(aswb->flags, ASW_FixedWidth)))
			{
        		aswb->desired_width = cre->width;
			}
			re_width = cre->width ;
		}
		if( get_flags( cre->value_mask, CWHeight )  )
		{
	    	if( get_flags( aswb->flags, ASW_MaxSwallow ) ||
				(Config->force_size.height == 0 && !get_flags(aswb->flags, ASW_FixedHeight)) )
			{
        		aswb->desired_height = cre->height;
			}
			re_height = cre->height ;
		}

		xwc.width = min( aswb->desired_width, re_width );
   		xwc.height = min( aswb->desired_height, re_height );

		xwc.x = make_tile_pad( get_flags(Config->align_contents,PAD_LEFT),
                               get_flags(Config->align_contents,PAD_RIGHT),
                               aswb->canvas->width, xwc.width );
		xwc.y = make_tile_pad( get_flags(Config->align_contents,PAD_TOP),
                               get_flags(Config->align_contents,PAD_BOTTOM),
                               aswb->canvas->height, xwc.height );

		xwc.border_width = cre->border_width;

		LOCAL_DEBUG_OUT( "Configuring swallowed window %lX to req(%dx%d%+d%+d bw = %d)- actual(%dx%d%+d%+d), (flags=%lX)", (unsigned long)aswb->swallowed->current->w, cre->width, cre->height, cre->x, cre->y, cre->border_width, xwc.width, xwc.height, xwc.x, xwc.y, xwcm );
        XConfigureWindow (dpy, aswb->swallowed->current->w, xwcm, &xwc);
		return;
	}
}

Bool
on_wharf_button_moveresize( ASWharfButton *aswb, ASEvent *event )
{
    ASFlagType changes = handle_canvas_config (aswb->canvas );
    ASFlagType swallowed_changes = 0 ;

    if( aswb->swallowed )
    {
        ASCanvas* sc = aswb->swallowed->current;
        swallowed_changes = handle_canvas_config ( sc );
        if( get_flags(swallowed_changes, CANVAS_RESIZED ) )
        {
            update_wharf_folder_size( aswb->parent );
        }
        if( get_flags(changes, CANVAS_RESIZED ) )
        {
		    int swidth = min( aswb->desired_width, aswb->swallowed->current->width );
    		int sheight = min( aswb->desired_height, aswb->swallowed->current->height );

			moveresize_canvas( aswb->swallowed->current,
                       make_tile_pad( get_flags(Config->align_contents,PAD_LEFT),
                                      get_flags(Config->align_contents,PAD_RIGHT),
                                      aswb->canvas->width, swidth      ),
                       make_tile_pad( get_flags(Config->align_contents,PAD_TOP),
                                      get_flags(Config->align_contents,PAD_BOTTOM),
                                      aswb->canvas->height, sheight    ),
                       swidth, sheight );
        }
        if( !get_flags(swallowed_changes, CANVAS_RESIZED ) && changes != 0 )
            send_swallowed_configure_notify(aswb);
    }

    if( aswb->folder_width != aswb->canvas->width ||
        aswb->folder_height != aswb->canvas->height )
        return False;

    if( get_flags(changes, CANVAS_RESIZED ) )
        set_astbar_size( aswb->bar, aswb->canvas->width, aswb->canvas->height );

    if( changes != 0 )                         /* have to always do that whenever canvas is changed */
        update_astbar_transparency( aswb->bar, aswb->canvas, False );

    if( changes != 0 && (DoesBarNeedsRendering(aswb->bar) || is_canvas_needs_redraw( aswb->canvas )))
        render_wharf_button( aswb );

#ifndef SHAPE
    swallowed_changes = 0 ;                    /* if no shaped extentions - ignore the changes */
#endif
    if( is_canvas_dirty( aswb->canvas ) || swallowed_changes != 0 )
    {
        update_canvas_display( aswb->canvas );
#ifdef SHAPE
        if( get_flags( aswb->canvas->state, CANVAS_SHAPE_SET ) && aswb->swallowed )
        {
			update_wharf_folder_shape( aswb->parent );
        }
#endif
    }
    return True;
}

void
set_wharf_clip_area( ASWharfFolder *aswf, int x, int y )
{
	if( aswf != NULL ) 
	{
		int w = aswf->total_width ;
		int h = aswf->total_height ;
		XRectangle *target_area ;
		ASImage **target_root_image ;
		
		x += (int)aswf->canvas->bw ;
		y += (int)aswf->canvas->bw ;

		LOCAL_DEBUG_OUT( "folder: %dx%d%+d%+d", w, h, x, y );

		target_area = &(aswf->root_clip_area);
		target_root_image = &(aswf->root_image);
		LOCAL_DEBUG_OUT( "target is normal (%dx%d%+d%+d)", target_area->width, target_area->height, target_area->x, target_area->y );

		if( target_area->x     != x || target_area->width != w ||
			target_area->y     != y || target_area->height != h )
		{	
			target_area->x = x ; 
			target_area->y = y ; 
			target_area->width = w ; 
			target_area->height = h ; 
			if( *target_root_image ) 
			{
				safe_asimage_destroy( *target_root_image );
				if( Scr.RootImage == *target_root_image )
					Scr.RootImage = NULL ;
				*target_root_image = NULL ;
			}	 
		}
	}
}

void
set_withdrawn_clip_area( ASWharfFolder *aswf, int x, int y, unsigned int w, unsigned int h )
{
	if( aswf != NULL ) 
	{
		XRectangle *target_area ;
		ASImage **target_root_image ;
		
		x += (int)aswf->canvas->bw ;
		y += (int)aswf->canvas->bw ;

		LOCAL_DEBUG_OUT( "folder: %dx%d%+d%+d", w, h, x, y);

		target_area = &(WharfState.withdrawn_root_clip_area);
		target_root_image = &(WharfState.withdrawn_root_image);
		LOCAL_DEBUG_OUT( "target is withdrawn (%dx%d%+d%+d)", target_area->width, target_area->height, target_area->x, target_area->y );

		if( target_area->x     != x || target_area->width != w ||
			target_area->y     != y || target_area->height != h )
		{	
			target_area->x = x ; 
			target_area->y = y ; 
			target_area->width = w ; 
			target_area->height = h ; 
			if( *target_root_image ) 
			{
				safe_asimage_destroy( *target_root_image );
				if( Scr.RootImage == *target_root_image )
					Scr.RootImage = NULL ;
				*target_root_image = NULL ;
			}	 
		}
	}
}


void 
clear_root_image_cache( ASWharfFolder *aswf )
{
	if( aswf ) 
	{
        int i = aswf->buttons_num ;
		if( aswf->root_image ) 
		{	
			safe_asimage_destroy( aswf->root_image );
			if( Scr.RootImage == aswf->root_image )
				Scr.RootImage = NULL ;
			aswf->root_image = NULL ;
		}
		while( --i >= 0 ) 
			if( aswf->buttons[i].folder != NULL ) 
		 		clear_root_image_cache( aswf->buttons[i].folder );
	}		   
}	  

Bool render_wharf_button( ASWharfButton *aswb )
{
	Bool result ; 
	ASWharfFolder *aswf = aswb->parent ;
	Bool withdrawn_root = ( aswf == WharfState.root_folder && get_flags( aswf->flags, ASW_Withdrawn) );
	if ( withdrawn_root )
	{
	 	Scr.RootImage = WharfState.withdrawn_root_image ;
		Scr.RootClipArea = WharfState.withdrawn_root_clip_area ; 
	}else
	{			  
		Scr.RootImage = aswf->root_image ; 
		Scr.RootClipArea = aswf->root_clip_area ; 
	}
	LOCAL_DEBUG_OUT( "rendering button %p for folder %p. Root Image = %p", aswb, aswf, Scr.RootImage );
	result = render_astbar( aswb->bar, aswb->canvas );
	ASSync( False );
	--WharfState.buttons_render_pending ;

	if ( withdrawn_root )
	 	WharfState.withdrawn_root_image = Scr.RootImage ;
	else
		aswf->root_image  = Scr.RootImage ;
	
	return result;
}	 
	

#if 0                          /* keep here just in case */
		void 
		add_wharf_folder_to_area( ASWharfFolder *aswf, int *from_x, int *from_y, int *to_x, int *to_y )
		{
			if( aswf != NULL && get_flags( aswf->flags, ASW_Mapped ) ) 
			{
				int x1 = aswf->canvas->root_x+(int)aswf->canvas->bw ;
				int y1 = aswf->canvas->root_y+(int)aswf->canvas->bw ;
				int x2 = aswf->total_width ;
				int y2 = aswf->total_height ;
        		int i = aswf->buttons_num ;

				if( aswf->gravity == NorthEastGravity || aswf->gravity == SouthEastGravity )
					x1 = x1+aswf->canvas->width - x2 ; 
				if( aswf->gravity == SouthWestGravity || aswf->gravity == SouthEastGravity )
					y1 = y1+aswf->canvas->height - y2 ; 
				x2 += x1 ;
				y2 += y1 ;
				LOCAL_DEBUG_OUT( "folder: from %+d%+d to %+d%+d, all: from %+d%+d to %+d%+d", 
						 		x1, y1, x2, y2, *from_x, *from_y, *to_x, *to_y );
				if ( x2 > 0 && x1 < (int)Scr.MyDisplayWidth &&
			 		y2 > 0 && y1 < (int)Scr.MyDisplayHeight )
				{
					x1 = AS_CLAMP( 0, x1, Scr.MyDisplayWidth );
					x2 = AS_CLAMP( 0, x2, Scr.MyDisplayWidth );
					y1 = AS_CLAMP( 0, y1, Scr.MyDisplayHeight );
					y2 = AS_CLAMP( 0, y2, Scr.MyDisplayHeight );
					if( x1 < *from_x ) *from_x = x1 ; 				   
					if( x2 > *to_x ) *to_x = x2 ; 				   
					if( y1 < *from_y ) *from_y = y1 ; 				   
					if( y2 > *to_y ) *to_y = y2 ; 				   

					LOCAL_DEBUG_OUT( "CLAMPED: folder: from %+d%+d to %+d%+d, all: from %+d%+d to %+d%+d", 
							 		x1, y1, x2, y2, *from_x, *from_y, *to_x, *to_y );
				}
				while( --i >= 0 ) 
					if( aswf->buttons[i].folder != NULL ) 
		 				add_wharf_folder_to_area( aswf->buttons[i].folder, 
										  		from_x, from_y, to_x, to_y );
			}
		}	 

		void
		update_root_clip_area()
		{
			/* TODO: update root clip area to the max area occupied by all mapped folders */
			int from_x = Scr.MyDisplayWidth, from_y = Scr.MyDisplayHeight ;
			int to_x = 0, to_y = 0;
			ASWharfFolder *aswf = WharfState.root_folder ; 
			add_wharf_folder_to_area( aswf, &from_x, &from_y, &to_x, &to_y );
			Scr.RootClipArea.x = from_x;
    		Scr.RootClipArea.y = from_y;
    		Scr.RootClipArea.width  = (to_x > from_x)?to_x - from_x:1;
    		Scr.RootClipArea.height = (to_y > from_y)?to_y - from_y:1;
    		if( Scr.RootImage )
    		{
        		safe_asimage_destroy( Scr.RootImage );
        		Scr.RootImage = NULL ;
    		}
		}
#endif

void
do_wharf_animate_iter( void *vdata )
{
	ASWharfFolder *aswf = (ASWharfFolder*)vdata ;
	
    if( aswf != NULL && aswf->animation_steps > 0 )
    {
        int new_width = 1, new_height = 1;
        animate_wharf( aswf, &new_width, &new_height );
        if( new_width == 0 || new_height == 0 /*||
            (new_width == aswf->canvas->width && new_height == aswf->canvas->height )*/)
            unmap_wharf_folder( aswf );
        else
        {
            LOCAL_DEBUG_OUT( "resizing folder from %dx%d to %dx%d", aswf->canvas->width, aswf->canvas->height, new_width, new_height );
            resize_canvas( aswf->canvas, new_width, new_height) ;
            ASSync( False ) ;
			if( get_flags( Config->set_flags, WHARF_ANIMATE_DELAY ) && Config->animate_delay > 0 )
			{	
				timer_new (Config->animate_delay*60, do_wharf_animate_iter, vdata);	  
            }else
				timer_new (60, do_wharf_animate_iter, vdata);	  
        }
	}		  
}


void on_wharf_moveresize( ASEvent *event )
{
    ASMagic *obj = NULL;

    obj = fetch_object( event->w ) ;
    if( obj == NULL )
        return;
    if( obj->magic == MAGIC_WHARF_BUTTON )
    {
        ASWharfButton *aswb = (ASWharfButton*)obj;
		
		/* need to check if there were any ConfigureNotify 's for our folder 
		 * and if so - go process them first */
		ASEvent parent_event;
    	while( ASCheckTypedWindowEvent(aswb->parent->canvas->w, ConfigureNotify,&(parent_event.x)) )
    	{
     		parent_event.client = NULL ;
            setup_asevent_from_xevent( &parent_event );
            DispatchEvent( &parent_event );
			timer_handle ();
        	ASSync(False);
    	}

		LOCAL_DEBUG_OUT("Handling button resizefor button %p", aswb );
        on_wharf_button_moveresize( aswb, event );
    }else if( obj->magic == MAGIC_WHARF_FOLDER )
    {
        ASWharfFolder *aswf = (ASWharfFolder*)obj;
        ASFlagType changes = handle_canvas_config (aswf->canvas );
		LOCAL_DEBUG_OUT("Handling folder resize for folder %p, mapped = %lX", aswf, get_flags( aswf->flags, ASW_Mapped ) );
        if( aswf->animation_steps == 0 && get_flags( aswf->flags, ASW_Mapped ) && aswf->animation_dir < 0 )
        {
            unmap_wharf_folder( aswf );
        }else if( changes != 0 )
        {
            int i = aswf->buttons_num ;
			Bool withdrawn = False ;
LOCAL_DEBUG_OUT("animation_steps = %d", aswf->animation_steps );

			withdrawn = (aswf->canvas->width == 1 || aswf->canvas->height == 1 ||
						 (aswf->canvas->root_x == -10000 && aswf->canvas->root_y == -10000) );

#ifdef SHAPE
            if( get_flags( changes, CANVAS_RESIZED ) && get_flags(aswf->flags,ASW_AnimationPending ) )
				XShapeCombineRectangles ( dpy, aswf->canvas->w, ShapeBounding, 0, 0, &(aswf->boundary), 1, ShapeSet, Unsorted);
#endif

			if( !withdrawn )
			{	
				if( !get_flags( aswf->flags, ASW_Withdrawn ))
				{	
					set_wharf_clip_area( aswf, aswf->canvas->root_x, aswf->canvas->root_y );
					while( --i >= 0 )
    	            	on_wharf_button_moveresize( &(aswf->buttons[i]), event );
				}else if( aswf->withdrawn_button != NULL )
					on_wharf_button_moveresize( aswf->withdrawn_button, event );
			}

#if 1			   
            if( get_flags( changes, CANVAS_RESIZED ))
			{
				LOCAL_DEBUG_OUT("AnimationPending ? = %lX", get_flags(aswf->flags,ASW_AnimationPending));
				if( get_flags(aswf->flags,ASW_AnimationPending ) )
				{	
					LOCAL_DEBUG_OUT("animate from = %dx%d, to = %dx%d", aswf->animate_from_w, aswf->animate_from_h, aswf->animate_to_w, aswf->animate_to_h);
					animate_wharf_loop( aswf, aswf->animate_from_w, aswf->animate_from_h, aswf->animate_to_w, aswf->animate_to_h );
					clear_flags(aswf->flags,ASW_UseBoundary|ASW_AnimationPending );
				}else if( !withdrawn )
				{
					/* fprintf(stderr, "clearing or applying boundary\n");	  */
					if( !update_wharf_folder_shape( aswf ) ) 
					{	
						/* fprintf(stderr, "forcing boundary\n");	  */
						update_canvas_display_mask(aswf->canvas, True);
					}
				}
			}
#endif
		
        }
    }
}

void
press_wharf_button( ASWharfButton *aswb, int state )
{
    if( WharfState.pressed_button &&
        WharfState.pressed_button != aswb )
    {
        set_astbar_pressed( WharfState.pressed_button->bar, WharfState.pressed_button->canvas, False );
        WharfState.pressed_button = NULL ;
    }
    if( aswb &&
        WharfState.pressed_button != aswb )
    {
        set_astbar_pressed( aswb->bar, aswb->canvas, True );
        WharfState.pressed_state = state ;
        WharfState.pressed_button = aswb ;
    }
}

void
release_pressure()
{
    ASWharfButton *pressed = WharfState.pressed_button ;
LOCAL_DEBUG_OUT( "pressed button is %p", pressed );
    if( pressed )
    {
        if( pressed->folder )
        {
LOCAL_DEBUG_OUT( "pressed button has folder %p (%s)", pressed->folder, get_flags( pressed->folder->flags, ASW_Mapped )?"Mapped":"Unmapped" );
            if( get_flags( pressed->folder->flags, ASW_Mapped ) )
                withdraw_wharf_folder( pressed->folder );
            else
                display_wharf_folder( pressed->folder, pressed->canvas->root_x, pressed->canvas->root_y,
                                                       pressed->canvas->root_x+pressed->canvas->width,
                                                       pressed->canvas->root_y+pressed->canvas->height  );
        }else if( pressed->fdata )
        {
#if defined(LOCAL_DEBUG) && !defined(NO_DEBUG_OUTPUT)
            print_func_data(__FILE__, __FUNCTION__, __LINE__, pressed->fdata);
#endif
            if( !get_flags( pressed->flags, ASW_SwallowTarget ) || pressed->swallowed == NULL )
            {  /* send command to the AS-proper : */
				ASWharfFolder *parentf = pressed->parent ;
				ASWharfButton *parentb = NULL ;
                SendCommand( pressed->fdata, 0);
				while( parentf != WharfState.root_folder )
				{
					parentb = parentf->parent ;
	                withdraw_wharf_folder( parentf );
					if( parentb == NULL )
						break;
					parentf = parentb->parent ;
				}
            }
        }
        set_astbar_pressed( pressed->bar, pressed->canvas, False );
        WharfState.pressed_button = NULL ;
    }
}

void
on_wharf_pressed( ASEvent *event )
{
    ASMagic *obj = fetch_object( event->w ) ;
    if( obj == NULL )
        return;
    if( obj->magic == MAGIC_WHARF_BUTTON )
    {
        ASWharfButton *aswb = (ASWharfButton*)obj;
        ASWharfFolder *aswf = aswb->parent;

		if( get_flags( aswb->flags, ASW_Transient ) )
			return ;
		        
		if( event->x.xbutton.button == Button3 && aswf == WharfState.root_folder )
		{	
            if( (WITHDRAW_ON_EDGE(Config) && (&(aswf->buttons[0]) == aswb || &(aswf->buttons[aswf->buttons_num-1]) == aswb)) ||
                 WITHDRAW_ON_ANY(Config))
        	{
            	if( get_flags( WharfState.root_folder->flags, ASW_Withdrawn) )
				{
					/* update our name to normal */	  
					set_folder_name( WharfState.root_folder, False );
					LOCAL_DEBUG_OUT( "un - withdrawing folder%s","" );
                	display_main_folder();
            	}else
            	{
                	int wx = 0, wy = 0, wwidth, wheight;
                	int i = aswf->buttons_num ;
                
					if( Config->withdraw_style < WITHDRAW_ON_ANY_BUTTON_AND_SHOW )
                    	aswb = &(aswf->buttons[0]);

					withdraw_wharf_subfolders( aswf );
					/* update our name to withdrawn */	  					
					set_folder_name( aswf, True );

                	wwidth = aswb->desired_width ;
                	wheight = aswb->desired_height ;
                	if( get_flags( Config->geometry.flags, XNegative ) )
                    	wx = Scr.MyDisplayWidth - wwidth ;
                	if( get_flags( Config->geometry.flags, YNegative ) )
                    	wy = Scr.MyDisplayHeight - wheight ;

					set_flags(aswf->flags,ASW_UseBoundary );
					animate_wharf_loop(aswf, aswf->canvas->width, aswf->canvas->height, wwidth, wheight );
					clear_flags(aswf->flags,ASW_UseBoundary );

					LOCAL_DEBUG_OUT( "withdrawing folder to %dx%d%+d%+d", wwidth, wheight, wx, wy );
                	XRaiseWindow( dpy, aswb->canvas->w );
                	while ( --i >= 0 )
                	{
                    	if( &(aswf->buttons[i]) != aswb && 
							aswf->buttons[i].canvas->root_x == aswf->canvas->root_x && 
							aswf->buttons[i].canvas->root_y == aswf->canvas->root_y)
                    	{
                        	aswf->buttons[i].folder_x = wwidth ;
                        	aswf->buttons[i].folder_y = wheight ;
                        	move_canvas( aswf->buttons[i].canvas, wwidth, wheight );
                    	}
                	}
                	set_flags( aswf->flags, ASW_Withdrawn );
					set_withdrawn_clip_area( aswf, wx, wy, wwidth, wheight );
                	moveresize_canvas( aswf->canvas, wx, wy, wwidth, wheight );
//					ASSync(False);
//					MapConfigureNotifyLoop();

                	aswb->folder_x = 0;
                	aswb->folder_y = 0;
                	aswb->folder_width = wwidth ;
                	aswb->folder_height = wheight ;
                	moveresize_canvas( aswb->canvas, 0, 0, wwidth, wheight );

					aswf->withdrawn_button = aswb ;
            	}
            	return;
        	}
		}
        press_wharf_button( aswb, event->x.xbutton.state );
    }
}


