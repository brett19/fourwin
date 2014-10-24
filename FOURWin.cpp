// FOURWin.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Four.h"

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

/*******************************************************************************************************************************************
Defines
*******************************************************************************************************************************************/
// Windows class name to register
#define	WINDOW_CLASS_NAME _T("FOURWindows")

// Name of the application
#define APPLICATION_NAME _T("FOUR")

// Title to display for errors.
#define ERROR_TITLE _T("Error")

// Width and height of the window
#define WINDOW_WIDTH	(1920/2)
#define WINDOW_HEIGHT	(1080/2)

/*******************************************************************************************************************************************
Global variables
*******************************************************************************************************************************************/

// Variable set by the message handler to finish the demo
static bool g_hasUserQuit = false;

/*******************************************************************************************************************************************
Helper Functions
*******************************************************************************************************************************************/

/*!*****************************************************************************************************************************************
@Function		HandleWindowMessages
@Input			nativeWindow                Handle to the window
@Input			message                     The message to handle
@Input			windowParameters            Additional message information
@Input			longWindowParameters        Additional message information
@Return		Result code to send to the OS
@Description	Processes event messages for the main window
*******************************************************************************************************************************************/
LRESULT CALLBACK HandleWindowMessages(HWND nativeWindow, UINT message, WPARAM windowParameters, LPARAM longWindowParameters)
{
	switch (message)
	{
	case WM_SYSCOMMAND:
		// Handle 2 system messages: screen saving and monitor power. We need to prevent them whilst we're rendering for a short time.
	{
		switch (windowParameters)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
		{
			// Return 0 to let Windows know we don't want to sleep or turn the monitor off right now.
			return 0;
		}
		}
		break;
	}
	case WM_CLOSE:
		// Handle the close message when a user clicks the quit icon of the window
	{
		// Tell the demo that it should stop rendering.
		g_hasUserQuit = true;

		// Post a quit message
		PostQuitMessage(0);

		// Return 1 to let Windows know the message has been successfully handled
		return 1;
	}
	}

	// Calls the default window procedure for messages we did not handle
	return DefWindowProc(nativeWindow, message, windowParameters, longWindowParameters);
}

/*!*****************************************************************************************************************************************
@Function		TestEGLError
@Input			nativeWindow                Handle to the window
@Input			functionLastCalled          Function which triggered the error
@Return		True if no EGL error was detected
@Description	Tests for an EGL error and prints it in a message box.
*******************************************************************************************************************************************/
bool TestEGLError(HWND nativeWindow, const char* functionLastCalled)
{
	/*	eglGetError returns the last error that occurred using EGL, not necessarily the status of the last called function. The user has to
	check after every single EGL call or at least once every frame. Usually this would be for debugging only, but for this example
	it is enabled always.
	*/
	EGLint lastError = eglGetError();
	if (lastError != EGL_SUCCESS)
	{
		TCHAR stringBuffer[256];
		_stprintf_s(stringBuffer, _T("%s failed (%x).\n"), functionLastCalled, lastError);
		MessageBox(nativeWindow, stringBuffer, ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		TestGLError
@Input			nativeWindow                Handle to the window
@Input			functionLastCalled          Function which triggered the error
@Return		True if no EGL error was detected
@Description	Tests for an EGL error and prints it in a message box.
*******************************************************************************************************************************************/
bool TestGLError(HWND nativeWindow, const char* functionLastCalled)
{

	/*	glGetError returns the last error that occurred using OpenGL ES, not necessarily the status of the last called function. The user
	has to check after every single OpenGL ES call or at least once every frame. Usually this would be for debugging only, but for this
	example it is enabled always
	*/
	GLenum lastError = glGetError();
	if (lastError != GL_NO_ERROR)
	{
		TCHAR stringBuffer[256];
		_stprintf_s(stringBuffer, _T("%s failed (%x).\n"), functionLastCalled, lastError);
		MessageBox(nativeWindow, stringBuffer, ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	return true;
}

/*******************************************************************************************************************************************
Application Functions
*******************************************************************************************************************************************/

/*!*****************************************************************************************************************************************
@Function		CreateWindowAndDisplay
@Input			applicationInstance		    Specific instance of the application
@Output		nativeWindow			    Native window type to create
@Output		deviceContext			    Native device context to create
@Return		Whether the function succeeded or not.
@Description	Creates a native window and display for the application to render into.
*******************************************************************************************************************************************/
bool CreateWindowAndDisplay(HINSTANCE applicationInstance, HWND &nativeWindow, HDC &deviceContext)
{
	// Describe the native window in a window class structure
	WNDCLASS nativeWindowDescription;
	nativeWindowDescription.style = CS_HREDRAW | CS_VREDRAW;
	nativeWindowDescription.lpfnWndProc = HandleWindowMessages;
	nativeWindowDescription.cbClsExtra = 0;
	nativeWindowDescription.cbWndExtra = 0;
	nativeWindowDescription.hInstance = applicationInstance;
	nativeWindowDescription.hIcon = 0;
	nativeWindowDescription.hCursor = 0;
	nativeWindowDescription.lpszMenuName = 0;
	nativeWindowDescription.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	nativeWindowDescription.lpszClassName = WINDOW_CLASS_NAME;

	// Register the windows class with the OS.
	ATOM registerClass = RegisterClass(&nativeWindowDescription);
	if (!registerClass)
	{
		MessageBox(0, _T("Failed to register the window class"), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
	}

	// Create a rectangle describing the area of the window
	RECT windowRectangle;
	SetRect(&windowRectangle, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	AdjustWindowRectEx(&windowRectangle, WS_CAPTION | WS_SYSMENU, false, 0);

	// Create the window from the available information
	nativeWindow = CreateWindow(WINDOW_CLASS_NAME, APPLICATION_NAME, WS_VISIBLE | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
		windowRectangle.right - windowRectangle.left, windowRectangle.bottom - windowRectangle.top,
		NULL, NULL, applicationInstance, NULL);
	if (!nativeWindow)
	{
		MessageBox(0, _T("Failed to create the window"), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	// Get the associated device context from the window
	deviceContext = GetDC(nativeWindow);
	if (!deviceContext)
	{
		MessageBox(nativeWindow, _T("Failed to create the device context"), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		CreateEGLDisplay
@Input			deviceContext               The device context used by the application
@Output		eglDisplay				    EGLDisplay created from deviceContext
@Return		Whether the function succeeded or not.
@Description	Creates an EGLDisplay from a native device context, and initialises it.
*******************************************************************************************************************************************/
bool CreateEGLDisplay(HDC deviceContext, EGLDisplay &eglDisplay)
{
	/*	Get an EGL display.
	EGL uses the concept of a "display" which in most environments corresponds to a single physical screen. After creating a native
	display for a given windowing system, EGL can use this handle to get a corresponding EGLDisplay handle to it for use in rendering.
	Should this fail, EGL is usually able to provide access to a default display.
	*/
	eglDisplay = eglGetDisplay(deviceContext);
	if (eglDisplay == EGL_NO_DISPLAY)
	{
		eglDisplay = eglGetDisplay((EGLNativeDisplayType)EGL_DEFAULT_DISPLAY);
	}

	// If a display still couldn't be obtained, return an error.
	if (eglDisplay == EGL_NO_DISPLAY)
	{
		MessageBox(0, _T("Failed to get an EGLDisplay"), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	/*	Initialize EGL.
	EGL has to be initialized with the display obtained in the previous step. All EGL functions other than eglGetDisplay
	and eglGetError need an initialised EGLDisplay.
	If an application is not interested in the EGL version number it can just pass NULL for the second and third parameters, but they
	are queried here for illustration purposes.
	*/
	EGLint eglMajorVersion, eglMinorVersion;
	if (!eglInitialize(eglDisplay, &eglMajorVersion, &eglMinorVersion))
	{
		MessageBox(0, _T("Failed to initialise the EGLDisplay"), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		ChooseEGLConfig
@Input			eglDisplay                  The EGLDisplay used by the application
@Output		eglConfig                   The EGLConfig chosen by the function
@Return		Whether the function succeeded or not.
@Description	Chooses an appropriate EGLConfig and return it.
*******************************************************************************************************************************************/
bool ChooseEGLConfig(EGLDisplay eglDisplay, EGLConfig& eglConfig)
{
	/*	Specify the required configuration attributes.
	An EGL "configuration" describes the capabilities an application requires and the type of surfaces that can be used for drawing.
	Each implementation exposes a number of different configurations, and an application needs to describe to EGL what capabilities it
	requires so that an appropriate one can be chosen. The first step in doing this is to create an attribute list, which is an array
	of key/value pairs which describe particular capabilities requested. In this application nothing special is required so we can query
	the minimum of needing it to render to a window, and being OpenGL ES 2.0 capable.
	*/
	const EGLint configurationAttributes[] =
	{
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	/*	Find a suitable EGLConfig
	eglChooseConfig is provided by EGL to provide an easy way to select an appropriate configuration. It takes in the capabilities
	specified in the attribute list, and returns a list of available configurations that match or exceed the capabilities requested.
	Details of all the possible attributes and how they are selected for by this function are available in the EGL reference pages here:
	http://www.khronos.org/registry/egl/sdk/docs/man/xhtml/eglChooseConfig.html
	It is also possible to simply get the entire list of configurations and use a custom algorithm to choose a suitable one, as many
	advanced applications choose to do. For this application however, taking the first EGLConfig that the function returns suits
	its needs perfectly, so we limit it to returning a single EGLConfig.
	*/
	EGLint configsReturned;
	if (!eglChooseConfig(eglDisplay, configurationAttributes, &eglConfig, 1, &configsReturned) || (configsReturned != 1))
	{
		MessageBox(0, _T("eglChooseConfig() failed."), ERROR_TITLE, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		CreateEGLSurface
@Input			nativeWindow                A native window that's been created
@Input			eglDisplay                  The EGLDisplay used by the application
@Input			eglConfig                   An EGLConfig chosen by the application
@Output		eglSurface					The EGLSurface created from the native window.
@Return		Whether the function succeeds or not.
@Description	Creates an EGLSurface from a native window
*******************************************************************************************************************************************/
bool CreateEGLSurface(HWND nativeWindow, EGLDisplay eglDisplay, EGLConfig eglConfig, EGLSurface& eglSurface)
{
	/*	Create an EGLSurface for rendering.
	Using a native window created earlier and a suitable eglConfig, a surface is created that can be used to render OpenGL ES calls to.
	There are three main surface types in EGL, which can all be used in the same way once created but work slightly differently:
	- Window Surfaces  - These are created from a native window and are drawn to the screen.
	- Pixmap Surfaces  - These are created from a native windowing system as well, but are offscreen and are not displayed to the user.
	- PBuffer Surfaces - These are created directly within EGL, and like Pixmap Surfaces are offscreen and thus not displayed.
	The offscreen surfaces are useful for non-rendering contexts and in certain other scenarios, but for most applications the main
	surface used will be a window surface as performed below.
	*/
	eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, nativeWindow, NULL);
	if (eglSurface == EGL_NO_SURFACE)
	{
		eglGetError(); // Clear error
		eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, NULL, NULL);
	}

	// Check for any EGL Errors
	if (!TestEGLError(nativeWindow, "eglCreateWindowSurface"))
	{
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		SetupEGLContext
@Input			eglDisplay                  The EGLDisplay used by the application
@Input			eglConfig                   An EGLConfig chosen by the application
@Input			eglSurface					The EGLSurface created from the native window.
@Output		eglContext                  The EGLContext created by this function
@Input			nativeWindow                A native window, used to display error messages
@Return		Whether the function succeeds or not.
@Description	Sets up the EGLContext, creating it and then installing it to the current thread.
*******************************************************************************************************************************************/
bool SetupEGLContext(EGLDisplay eglDisplay, EGLConfig eglConfig, EGLSurface eglSurface, EGLContext& eglContext, HWND nativeWindow)
{
	/*	Create a context.
	EGL has to create what is known as a context for OpenGL ES. The concept of a context is OpenGL ES's way of encapsulating any
	resources and state. What appear to be "global" functions in OpenGL actually only operate on the current context. A context
	is required for any operations in OpenGL ES.
	Similar to an EGLConfig, a context takes in a list of attributes specifying some of its capabilities. However in most cases this
	is limited to just requiring the version of the OpenGL ES context required - In this case, OpenGL ES 2.0.
	*/
	EGLint contextAttributes[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	// Create the context with the context attributes supplied
	eglContext = eglCreateContext(eglDisplay, eglConfig, NULL, contextAttributes);
	if (!TestEGLError(nativeWindow, "eglCreateContext"))
	{
		return false;
	}

	/*	Make OpenGL ES the current API.
	After creating the context, EGL needs a way to know that any subsequent EGL calls are going to be affecting OpenGL ES,
	rather than any other API (such as OpenVG).
	*/
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!TestEGLError(nativeWindow, "eglBindAPI"))
	{
		return false;
	}

	/*	Bind the context to the current thread.
	Due to the way OpenGL uses global functions, contexts need to be made current so that any function call can operate on the correct
	context. Specifically, make current will bind the context to the thread it's called from, and unbind it from any others. To use
	multiple contexts at the same time, users should use multiple threads and synchronise between them.
	*/
	eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
	if (!TestEGLError(nativeWindow, "eglMakeCurrent"))
	{
		return false;
	}

	return true;
}

/*!*****************************************************************************************************************************************
@Function		RenderScene
@Input			shaderProgram               The shader program used to render the scene
@Input			eglDisplay                  The EGLDisplay used by the application
@Input			eglSurface					The EGLSurface created from the native window.
@Input			nativeWindow                A native window, used to display error messages
@Return		Whether the function succeeds or not.
@Description	Renders the scene to the framebuffer. Usually called within a loop.
*******************************************************************************************************************************************/
bool RenderScene(EGLDisplay eglDisplay, EGLSurface eglSurface, HWND nativeWindow)
{
	// The message handler setup for the window system will signal this variable when the window is closed, so close the application.
	if (g_hasUserQuit)
	{
		return false;
	}




	fourRender();



	/*	Present the display data to the screen.
	When rendering to a Window surface, OpenGL ES is double buffered. This means that OpenGL ES renders directly to one frame buffer,
	known as the back buffer, whilst the display reads from another - the front buffer. eglSwapBuffers signals to the windowing system
	that OpenGL ES 2.0 has finished rendering a scene, and that the display should now draw to the screen from the new data. At the same
	time, the front buffer is made available for OpenGL ES 2.0 to start rendering to. In effect, this call swaps the front and back
	buffers.
	*/
	if (!eglSwapBuffers(eglDisplay, eglSurface))
	{
		TestEGLError(nativeWindow, "eglSwapBuffers");
		return false;
	}

	// Check for messages from the windowing system. These will pass through the callback registered earlier.
	MSG eventMessage;
	PeekMessage(&eventMessage, nativeWindow, NULL, NULL, PM_REMOVE);
	TranslateMessage(&eventMessage);
	DispatchMessage(&eventMessage);

	return true;
}

/*!*****************************************************************************************************************************************
@Function		ReleaseEGLState
@Input			eglDisplay                   The EGLDisplay used by the application
@Description	Releases all resources allocated by EGL
*******************************************************************************************************************************************/
void ReleaseEGLState(EGLDisplay eglDisplay)
{
	// To release the resources in the context, first the context has to be released from its binding with the current thread.
	eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	// Terminate the display, and any resources associated with it (including the EGLContext)
	eglTerminate(eglDisplay);
}

/*!*****************************************************************************************************************************************
@Function		ReleaseWindowAndDisplay
@Input			nativeWindow                The native window to release
@Input			deviceContext               The native display to release
@Description	Releases all resources allocated by the windowing system
*******************************************************************************************************************************************/
void ReleaseWindowAndDisplay(HWND nativeWindow, HDC deviceContext)
{
	// Release the device context.
	if (deviceContext)
	{
		ReleaseDC(nativeWindow, deviceContext);
	}

	// Destroy the window
	if (nativeWindow)
	{
		DestroyWindow(nativeWindow);
	}
}

/*!*****************************************************************************************************************************************
@Function		WinMain
@Input			applicationInstance         Application instance created by the Operating System
@Input			previousInstance            Always NULL
@Input			commandLineString           Command line string passed from the Operating System, ignored.
@Input			showCommand                 Specifies how the window is to be shown, ignored.
@Return		Result code to send to the Operating System
@Description	Main function of the program, executes other functions.
*******************************************************************************************************************************************/
int _tmain(int argc, _TCHAR* argv[])
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	//_CrtSetBreakAlloc(358);

	// Windows variables
	HWND				nativeWindow = NULL;
	HDC					deviceContext = NULL;

	// EGL variables
	EGLDisplay			eglDisplay = NULL;
	EGLConfig			eglConfig = NULL;
	EGLSurface			eglSurface = NULL;
	EGLContext			eglContext = NULL;

	HINSTANCE applicationInstance = GetModuleHandle(NULL);

	// Setup the windowing system, getting a window and a display
	if (!CreateWindowAndDisplay(applicationInstance, nativeWindow, deviceContext))
	{
		goto cleanup;
	}

	// Create and Initialise an EGLDisplay from the native display
	if (!CreateEGLDisplay(deviceContext, eglDisplay))
	{
		goto cleanup;
	}

	// Choose an EGLConfig for the application, used when setting up the rendering surface and EGLContext
	if (!ChooseEGLConfig(eglDisplay, eglConfig))
	{
		goto cleanup;
	}

	// Create an EGLSurface for rendering from the native window
	if (!CreateEGLSurface(nativeWindow, eglDisplay, eglConfig, eglSurface))
	{
		goto cleanup;
	}

	// Setup the EGL Context from the other EGL constructs created so far, so that the application is ready to submit OpenGL ES commands
	if (!SetupEGLContext(eglDisplay, eglConfig, eglSurface, eglContext, nativeWindow))
	{
		goto cleanup;
	}

	// INITIALIZE
	fourSetup();
	fourResize(WINDOW_WIDTH, WINDOW_HEIGHT);

	// Renders a triangle for 800 frames using the state setup in the previous function
	for (int i = 0; i < 80000; ++i)
	{
		if (!RenderScene(eglDisplay, eglSurface, nativeWindow))
		{
			break;
		}
	}

	// DeInitialise
	fourDestroy();

cleanup:
	// Release the EGL State
	ReleaseEGLState(eglDisplay);

	// Release the windowing system resources
	ReleaseWindowAndDisplay(nativeWindow, deviceContext);

	// Destroy the eglWindow
	return 0;
}

/*******************************************************************************************************************************************
End of file (OGLES2HelloAPI_Windows.cpp)
*******************************************************************************************************************************************/
