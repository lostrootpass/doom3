#pragma hdrstop
#include "../../idlib/precompiled.h"

#include "win_local.h"
#include "rc/doom_resource.h"
#include "../../renderer/tr_local.h"

/*
========================
GetDeviceName
========================
*/
idStr GetDeviceName( const int deviceNum ) {
	DISPLAY_DEVICE	device = {};
	device.cb = sizeof( device );
	if ( !EnumDisplayDevices(
			0,			// lpDevice
			deviceNum,
			&device,
			0 /* dwFlags */ ) ) {
		return false;
	}

	// get the monitor for this display
	if ( ! (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP ) ) {
		return false;
	}

	return idStr( device.DeviceName );
}


/*
========================
GetDisplayCoordinates
========================
*/
bool GetDisplayCoordinates( const int deviceNum, int & x, int & y, int & width, int & height, int & displayHz ) {
	idStr deviceName = GetDeviceName( deviceNum );
	if ( deviceName.Length() == 0 ) {
		return false;
	}

	DISPLAY_DEVICE	device = {};
	device.cb = sizeof( device );
	if ( !EnumDisplayDevices(
			0,			// lpDevice
			deviceNum,
			&device,
			0 /* dwFlags */ ) ) {
		return false;
	}

	DISPLAY_DEVICE	monitor;
	monitor.cb = sizeof( monitor );
	if ( !EnumDisplayDevices(
			deviceName.c_str(),
			0,
			&monitor,
			0 /* dwFlags */ ) ) {
		return false;
	}

	DEVMODE	devmode;
	devmode.dmSize = sizeof( devmode );
	if ( !EnumDisplaySettings( deviceName.c_str(),ENUM_CURRENT_SETTINGS, &devmode ) ) {
		return false;
	}

	common->Printf( "display device: %i\n", deviceNum );
	common->Printf( "  DeviceName  : %s\n", device.DeviceName );
	common->Printf( "  DeviceString: %s\n", device.DeviceString );
	common->Printf( "  StateFlags  : 0x%x\n", device.StateFlags );
	common->Printf( "  DeviceID    : %s\n", device.DeviceID );
	common->Printf( "  DeviceKey   : %s\n", device.DeviceKey );
	common->Printf( "      DeviceName  : %s\n", monitor.DeviceName );
	common->Printf( "      DeviceString: %s\n", monitor.DeviceString );
	common->Printf( "      StateFlags  : 0x%x\n", monitor.StateFlags );
	common->Printf( "      DeviceID    : %s\n", monitor.DeviceID );
	common->Printf( "      DeviceKey   : %s\n", monitor.DeviceKey );
	common->Printf( "          dmPosition.x      : %i\n", devmode.dmPosition.x );
	common->Printf( "          dmPosition.y      : %i\n", devmode.dmPosition.y );
	common->Printf( "          dmBitsPerPel      : %i\n", devmode.dmBitsPerPel );
	common->Printf( "          dmPelsWidth       : %i\n", devmode.dmPelsWidth );
	common->Printf( "          dmPelsHeight      : %i\n", devmode.dmPelsHeight );
	common->Printf( "          dmDisplayFlags    : 0x%x\n", devmode.dmDisplayFlags );
	common->Printf( "          dmDisplayFrequency: %i\n", devmode.dmDisplayFrequency );

	x = devmode.dmPosition.x;
	y = devmode.dmPosition.y;
	width = devmode.dmPelsWidth;
	height = devmode.dmPelsHeight;
	displayHz = devmode.dmDisplayFrequency;

	return true;
}


/*
====================
GFX_GetWindowDimensions
====================
*/
bool GFX_GetWindowDimensions( const glimpParms_t parms, int &x, int &y, int &w, int &h ) {
	//
	// compute width and height
	//
	if ( parms.fullScreen != 0 ) {
		if ( parms.fullScreen == -1 ) {
			// borderless window at specific location, as for spanning
			// multiple monitor outputs
			x = parms.x;
			y = parms.y;
			w = parms.width;
			h = parms.height;
		} else {
			// get the current monitor position and size on the desktop, assuming
			// any required ChangeDisplaySettings has already been done
			int displayHz = 0;
			if ( !GetDisplayCoordinates( parms.fullScreen - 1, x, y, w, h, displayHz ) ) {
				return false;
			}
		}
	} else {
		RECT	r;

		// adjust width and height for window border
		r.bottom = parms.height;
		r.left = 0;
		r.top = 0;
		r.right = parms.width;

		AdjustWindowRect (&r, WINDOW_STYLE|WS_SYSMENU, FALSE);

		w = r.right - r.left;
		h = r.bottom - r.top;

		x = parms.x;
		y = parms.y;
	}

	return true;
}



/*
====================
FakeWndProc

Only used to get wglExtensions
====================
*/
LONG WINAPI FakeWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam) {

	if ( uMsg == WM_DESTROY ) {
        PostQuitMessage(0);
	}

	if ( uMsg != WM_CREATE ) {
	    return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	const static PIXELFORMATDESCRIPTOR pfd = {
		sizeof(PIXELFORMATDESCRIPTOR),
		1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
		PFD_TYPE_RGBA,
		24,
		0, 0, 0, 0, 0, 0,
		8, 0,
		0, 0, 0, 0,
		24, 8,
		0,
		PFD_MAIN_PLANE,
		0,
		0,
		0,
		0,
	};
	int		pixelFormat;
	HDC hDC;
	HGLRC hGLRC;

    hDC = GetDC(hWnd);

    // Set up OpenGL
    pixelFormat = ChoosePixelFormat(hDC, &pfd);
    SetPixelFormat(hDC, pixelFormat, &pfd);
    hGLRC = qwglCreateContext(hDC);
    qwglMakeCurrent(hDC, hGLRC);

	// free things
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hGLRC);
    ReleaseDC(hWnd, hDC);

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


/*
====================
GFX_CreateWindowClasses
====================
*/
void GFX_CreateWindowClasses() {
	WNDCLASS wc;

	//
	// register the window class if necessary
	//
	if ( win32.windowClassRegistered ) {
		return;
	}

	memset( &wc, 0, sizeof( wc ) );

	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC) MainWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = win32.hInstance;
	wc.hIcon         = LoadIcon( win32.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor       = NULL;
	wc.hbrBackground = (struct HBRUSH__ *)COLOR_GRAYTEXT;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = WIN32_WINDOW_CLASS_NAME;

	if ( !RegisterClass( &wc ) ) {
		common->FatalError( "GLW_CreateWindow: could not register window class" );
	}
	common->Printf( "...registered window class\n" );

	// now register the fake window class that is only used
	// to get wgl extensions
	wc.style         = 0;
	wc.lpfnWndProc   = (WNDPROC) FakeWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = win32.hInstance;
	wc.hIcon         = LoadIcon( win32.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = (struct HBRUSH__ *)COLOR_GRAYTEXT;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = WIN32_FAKE_WINDOW_CLASS_NAME;

	if ( !RegisterClass( &wc ) ) {
		common->FatalError( "GLW_CreateWindow: could not register window class" );
	}
	common->Printf( "...registered fake window class\n" );

	win32.windowClassRegistered = true;
}

/*
========================
GetDisplayName
========================
*/
const char * GetDisplayName( const int deviceNum ) {
	static DISPLAY_DEVICE	device;
	device.cb = sizeof( device );
	if ( !EnumDisplayDevices(
			0,			// lpDevice
			deviceNum,
			&device,
			0 /* dwFlags */ ) ) {
		return NULL;
	}
	return device.DeviceName;
}

/*
====================
DMDFO
====================
*/
const char * DMDFO( int dmDisplayFixedOutput ) {
	switch( dmDisplayFixedOutput ) {
	case DMDFO_DEFAULT: return "DMDFO_DEFAULT";
	case DMDFO_CENTER: return "DMDFO_CENTER";
	case DMDFO_STRETCH: return "DMDFO_STRETCH";
	}
	return "UNKNOWN";
}

/*
====================
PrintDevMode
====================
*/
static void PrintDevMode( DEVMODE & devmode ) {
	common->Printf( "          dmPosition.x        : %i\n", devmode.dmPosition.x );
	common->Printf( "          dmPosition.y        : %i\n", devmode.dmPosition.y );
	common->Printf( "          dmBitsPerPel        : %i\n", devmode.dmBitsPerPel );
	common->Printf( "          dmPelsWidth         : %i\n", devmode.dmPelsWidth );
	common->Printf( "          dmPelsHeight        : %i\n", devmode.dmPelsHeight );
	common->Printf( "          dmDisplayFixedOutput: %s\n", DMDFO( devmode.dmDisplayFixedOutput ) );
	common->Printf( "          dmDisplayFlags      : 0x%x\n", devmode.dmDisplayFlags );
	common->Printf( "          dmDisplayFrequency  : %i\n", devmode.dmDisplayFrequency );
}

/*
====================
DumpAllDisplayDevices
====================
*/
void DumpAllDisplayDevices() {
	common->Printf( "\n" );
	for ( int deviceNum = 0 ; ; deviceNum++ ) {
		DISPLAY_DEVICE	device = {};
		device.cb = sizeof( device );
		if ( !EnumDisplayDevices(
				0,			// lpDevice
				deviceNum,
				&device,
				0 /* dwFlags */ ) ) {
			break;
		}

		common->Printf( "display device: %i\n", deviceNum );
		common->Printf( "  DeviceName  : %s\n", device.DeviceName );
		common->Printf( "  DeviceString: %s\n", device.DeviceString );
		common->Printf( "  StateFlags  : 0x%x\n", device.StateFlags );
		common->Printf( "  DeviceID    : %s\n", device.DeviceID );
		common->Printf( "  DeviceKey   : %s\n", device.DeviceKey );

		for ( int monitorNum = 0 ; ; monitorNum++ ) {
			DISPLAY_DEVICE	monitor = {};
			monitor.cb = sizeof( monitor );
			if ( !EnumDisplayDevices(
					device.DeviceName,
					monitorNum,
					&monitor,
					0 /* dwFlags */ ) ) {
				break;
			}

			common->Printf( "      DeviceName  : %s\n", monitor.DeviceName );
			common->Printf( "      DeviceString: %s\n", monitor.DeviceString );
			common->Printf( "      StateFlags  : 0x%x\n", monitor.StateFlags );
			common->Printf( "      DeviceID    : %s\n", monitor.DeviceID );
			common->Printf( "      DeviceKey   : %s\n", monitor.DeviceKey );

			DEVMODE	currentDevmode = {};
			if ( !EnumDisplaySettings( device.DeviceName,ENUM_CURRENT_SETTINGS, &currentDevmode ) ) {
				common->Printf( "ERROR:  EnumDisplaySettings(ENUM_CURRENT_SETTINGS) failed!\n" );
			}
			common->Printf( "          -------------------\n" );
			common->Printf( "          ENUM_CURRENT_SETTINGS\n" );
			PrintDevMode( currentDevmode );

			DEVMODE	registryDevmode = {};
			if ( !EnumDisplaySettings( device.DeviceName,ENUM_REGISTRY_SETTINGS, &registryDevmode ) ) {
				common->Printf( "ERROR:  EnumDisplaySettings(ENUM_CURRENT_SETTINGS) failed!\n" );
			}
			common->Printf( "          -------------------\n" );
			common->Printf( "          ENUM_CURRENT_SETTINGS\n" );
			PrintDevMode( registryDevmode );

			for ( int modeNum = 0 ; ; modeNum++ ) {
				DEVMODE	devmode = {};

				if ( !EnumDisplaySettings( device.DeviceName,modeNum, &devmode ) ) {
					break;
				}

				if ( devmode.dmBitsPerPel != 32 ) {
					continue;
				}
				if ( devmode.dmDisplayFrequency < 60 ) {
					continue;
				}
				if ( devmode.dmPelsHeight < 720 ) {
					continue;
				}
				common->Printf( "          -------------------\n" );
				common->Printf( "          modeNum             : %i\n", modeNum );
				PrintDevMode( devmode );
			}
		}
	}
	common->Printf( "\n" );
}

