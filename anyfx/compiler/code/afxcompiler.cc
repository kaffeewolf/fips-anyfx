//------------------------------------------------------------------------------
/**
    AnyFX compiler functions
    
    (C) 2013 Gustav Sterbrant
*/
//------------------------------------------------------------------------------
#include "cmdlineargs.h"
#include "afxcompiler.h"
#include "typechecker.h"
#include "generator.h"
#include "header.h"
#include <fstream>
#include <algorithm>
#include <locale>
#include <iostream>

#include "antlr4-runtime.h"
#include "antlr4-common.h"
#include "parser4/AnyFXLexer.h"
#include "parser4/AnyFXParser.h"
#include "parser4/AnyFXBaseListener.h"
#include "parser4/anyfxerrorhandlers.h"

using namespace antlr4;

#if __linux__
#include <X11/X.h>
#include <X11/Xlib.h>
#include <GL/glx.h>
#undef Success
#elif APPLE
#include <libproc.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLTypes.h>
#include <OpenGL/OpenGL.h>
#endif

#include "mcpp_lib.h"
#include "mcpp_out.h"
#include "glslang/Public/ShaderLang.h"

#if __WIN32__
#define WIN32_LEAN_AND_MEAN 1
#include "windows.h"
//------------------------------------------------------------------------------
/**
*/
LRESULT CALLBACK
AnyFXWinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

    HDC hDc;
    HGLRC hRc;      
    HACCEL hAccel;
    HINSTANCE hInst;
    HWND hWnd;
#elif __linux__
    Display* dsp;
    Window root;
    GLint attrs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None};
    XVisualInfo* vi;
    Colormap cmap;
    XSetWindowAttributes swa;
    Window win;
    GLXContext glc;
    XWindowAttributes gwa;
    XEvent xev;
#elif APPLE
    CGLContextObj ctx; 
    CGLPixelFormatObj pix; 
    GLint npix; 
    CGLPixelFormatAttribute attribs[] = { 
        (CGLPixelFormatAttribute) 0
    }; 
#endif

//------------------------------------------------------------------------------
/**
*/
bool
AnyFXPreprocess(const std::string& file, const std::vector<std::string>& defines, const std::string& vendor, std::string& output)
{
    std::string fileName = file.substr(file.rfind("/")+1, file.length()-1);
	std::string vend = "-DVENDOR=" + vendor;

	const char* constArgs[] =
	{
		"",			// first argument is supposed to be application system path, but is omitted since we run mcpp directly
		"-W 0",
		"-a",
		vend.c_str()
	};
	const unsigned numConstArgs = sizeof(constArgs) / sizeof(char*);
	const unsigned numTotalArgs = numConstArgs + defines.size() + 1;
	const char** args = new const char*[numConstArgs + defines.size() + 1];
	memcpy(args, constArgs, sizeof(constArgs));

    unsigned i;
    for (i = 0; i < defines.size(); i++)
    {
        args[numConstArgs + i] = defines[i].c_str();
    }
    args[numTotalArgs-1] = file.c_str();

	// run preprocessing
    mcpp_use_mem_buffers(1);
    int result = mcpp_lib_main(numTotalArgs, (char**)args);
    if (result != 0)
    {
        delete[] args;
        return false;
    }
    else
    {
        char* preprocessed = mcpp_get_mem_buffer((OUTDEST)0);
        output.append(preprocessed);
		delete[] args;
        return true;
    }
}

//------------------------------------------------------------------------------
/**
*/
std::vector<std::string>
AnyFXGenerateDependencies(const std::string& file, const std::vector<std::string>& defines)
{
	std::vector<std::string> res;

	const char* constArgs[] =
	{
		"",			// first argument is supposed to be application system path, but is omitted since we run mcpp directly
		"-M"
	};
	const unsigned numConstArgs = sizeof(constArgs) / sizeof(char*);
	const unsigned numTotalArgs = numConstArgs + defines.size() + 1;
	const char** args = new const char*[numConstArgs + defines.size() + 1];
	memcpy(args, constArgs, sizeof(constArgs));

	unsigned i;
	for (i = 0; i < defines.size(); i++)
	{
		args[numConstArgs + i] = defines[i].c_str();
	}
	args[numTotalArgs - 1] = file.c_str();

	// run preprocessing
	mcpp_use_mem_buffers(1);
	int result = mcpp_lib_main(numTotalArgs, (char**)args);
	if (result == 0)
	{
		std::string output = mcpp_get_mem_buffer((OUTDEST)0);

		// grah, remove the padding and the Makefile stuff, using std::string...
		size_t colon = output.find_first_of(':')+1;
		output = output.substr(colon);
		size_t newline = output.find_first_of('\n');
		while (newline != output.npos)
		{
			std::string line = output.substr(0, newline);
			if (!line.empty())
			{
				while (!line.empty() && (line.front() == ' '))								line = line.substr(1);
				while (!line.empty() && (line.back() == ' ' || line.back() == '\\'))		line = line.substr(0, line.size() - 1);
				res.push_back(line);
				output = output.substr(newline + 1);
				newline = output.find_first_of('\n');
			}
			else
				break;
		}
	}
	delete[] args;

	return res;
}

//------------------------------------------------------------------------------
/**
    Compiles AnyFX effect.

    @param file			Input file to compile
    @param output		Output destination file
    @param target		Target language
	@param vendor		GPU vendor name
    @param defines		List of preprocessor definitions
    @param errorBuffer	Buffer containing errors, created in function but must be deleted manually
*/
bool
AnyFXCompile(const std::string& file, const std::string& output, const std::string& header_output, const std::string& target, const std::string& vendor, const std::vector<std::string>& defines, const std::vector<std::string>& flags, AnyFXErrorBlob** errorBuffer)
{
    std::string preprocessed;
    (*errorBuffer) = NULL;

    // if preprocessor is successful, continue parsing the actual code
	if (AnyFXPreprocess(file, defines, vendor, preprocessed))
    {
		ANTLRInputStream input;
		input.load(preprocessed);

		AnyFXLexer lexer(&input);
		lexer.setTokenFactory(AnyFXTokenFactory::DEFAULT);
		CommonTokenStream tokens(&lexer);
		AnyFXParser parser(&tokens);

		// get the name of the shader
		std::locale loc;
		size_t extension = file.rfind('.');
		size_t lastFolder = file.rfind('/') + 1;
		std::string effectName = file.substr(lastFolder, (file.length() - lastFolder) - (file.length() - extension));
		effectName[0] = std::toupper(effectName[0], loc);
		size_t undersc = effectName.find('_');
		while (undersc != std::string::npos)
		{
			effectName[undersc + 1] = std::toupper(effectName[undersc + 1], loc);
			effectName = effectName.erase(undersc, 1);
			undersc = effectName.find('_');
		}

		// setup preprocessor
		parser.preprocess();

		// remove all preprocessor crap left by mcpp
		size_t i;
		for (i = 0; i < parser.lines.size(); i++)
		{
			size_t start = std::get<2>(parser.lines[i]);
			size_t stop = preprocessed.find('\n', start);
			std::string fill(stop - start, ' ');
			preprocessed.replace(start, stop - start, fill);
		}

		AnyFXLexerHandler lexerErrorHandler;
		lexerErrorHandler.lines = parser.lines;
		AnyFXParserHandler parserErrorHandler;
		parserErrorHandler.lines = parser.lines;

		// reload the preprocessed data
		input.reset();
		input.load(preprocessed);
		lexer.setInputStream(&input);
		lexer.setTokenFactory(AnyFXTokenFactory::DEFAULT);
		lexer.addErrorListener(&lexerErrorHandler);
		tokens.setTokenSource(&lexer);
		parser.setTokenStream(&tokens);
		parser.addErrorListener(&parserErrorHandler);

        // create new effect
        Effect effect = parser.entry()->returnEffect;

        // stop the process if lexing or parsing fails
        if (!lexerErrorHandler.hasError && !parserErrorHandler.hasError)
        {
            // create header
            Header header;
            header.SetProfile(target);

			// handle shader-file level compile flags
			header.SetFlags(flags);

            // set effect header and setup effect
            effect.SetHeader(header);
			effect.SetName(effectName);
			effect.SetFile(file);
            effect.Setup();

			// set debug output dump if flag is supplied
			if (header.GetFlags() & Header::OutputGeneratedShaders)
			{
				effect.SetDebugOutputPath(output);
			}

            // create type checker
            TypeChecker typeChecker;

            // type check effect
            typeChecker.SetHeader(header);
            effect.TypeCheck(typeChecker);

            // compile effect
            int typeCheckerStatus = typeChecker.GetStatus();
            if (typeCheckerStatus == TypeChecker::Success || typeCheckerStatus == TypeChecker::Warnings)
            {
                // create code generator
                Generator generator;

                // generate code for effect
                generator.SetHeader(header);
                effect.Generate(generator);

                // set warnings as 'error' buffer
                if (typeCheckerStatus == TypeChecker::Warnings)
                {
                    unsigned warnings = typeChecker.GetWarningCount();
                    std::string errorMessage;
                    errorMessage = typeChecker.GetErrorBuffer();
                    errorMessage = errorMessage + Format("Type checking returned with %d warnings\n", warnings);

                    *errorBuffer = new AnyFXErrorBlob;
                    (*errorBuffer)->buffer = new char[errorMessage.size()];
                    (*errorBuffer)->size = errorMessage.size();
                    errorMessage.copy((*errorBuffer)->buffer, (*errorBuffer)->size);
                    (*errorBuffer)->buffer[(*errorBuffer)->size-1] = '\0';
                }

                if (generator.GetStatus() == Generator::Success)
                {
                    // create binary writer
                    BinWriter writer;
                    writer.SetPath(output);
                    if (writer.Open())
                    {
                        // compile and write to binary writer
                        effect.Compile(writer);

                        // close writer and finish file
                        writer.Close();

						// output header file
						{
							TextWriter headerWriter;

							// the path is going to be .fxb.h, but that's okay, it makes it super clear its generated from a shader
							headerWriter.SetPath(header_output);
							if (headerWriter.Open())
							{
								// call the effect to generate a header
								header.SetProfile("c");
								effect.SetHeader(header);
								effect.GenerateHeader(headerWriter);
								headerWriter.Close();
							}
						}

						mcpp_use_mem_buffers(1);	// clear mcpp
                        return true;
                    }
                    else
                    {
                        std::string errorMessage = Format("File '%s' could not be opened for writing\n", output.c_str());
                        *errorBuffer = new AnyFXErrorBlob;
                        (*errorBuffer)->buffer = new char[errorMessage.size()];
                        (*errorBuffer)->size = errorMessage.size();
                        errorMessage.copy((*errorBuffer)->buffer, (*errorBuffer)->size);
                        (*errorBuffer)->buffer[(*errorBuffer)->size-1] = '\0';

						// destroy compiler state and return
						mcpp_use_mem_buffers(1);	// clear mcpp
                        return false;
                    }
                }
                else
                {
                    unsigned errors = generator.GetErrorCount();
                    unsigned warnings = generator.GetWarningCount();
                    std::string errorMessage;
                    errorMessage = generator.GetErrorBuffer();
                    errorMessage = errorMessage + Format("Code generation failed with %d errors and %d warnings\n", errors, warnings);

                    *errorBuffer = new AnyFXErrorBlob;
                    (*errorBuffer)->buffer = new char[errorMessage.size()];
                    (*errorBuffer)->size = errorMessage.size();
                    errorMessage.copy((*errorBuffer)->buffer, (*errorBuffer)->size);
                    (*errorBuffer)->buffer[(*errorBuffer)->size-1] = '\0';

					// destroy compiler state and return
					mcpp_use_mem_buffers(1);	// clear mcpp
                    return false;
                }
            }
            else
            {
                unsigned errors = typeChecker.GetErrorCount();
                unsigned warnings = typeChecker.GetWarningCount();
                std::string errorMessage;
                errorMessage = typeChecker.GetErrorBuffer();
                errorMessage = errorMessage + Format("Type checking failed with %d errors and %d warnings\n", errors, warnings);

                *errorBuffer = new AnyFXErrorBlob;
                (*errorBuffer)->buffer = new char[errorMessage.size()];
                (*errorBuffer)->size = errorMessage.size();
                errorMessage.copy((*errorBuffer)->buffer, (*errorBuffer)->size);
                (*errorBuffer)->buffer[(*errorBuffer)->size-1] = '\0';

				// destroy compiler state and return
				mcpp_use_mem_buffers(1);	// clear mcpp
                return false;
            }
        }
        else
        {
            std::string errorMessage;
            errorMessage.append(lexerErrorHandler.errorBuffer);
            errorMessage.append(parserErrorHandler.errorBuffer);

            *errorBuffer = new AnyFXErrorBlob;
            (*errorBuffer)->buffer = new char[errorMessage.size()];
            (*errorBuffer)->size = errorMessage.size();
            errorMessage.copy((*errorBuffer)->buffer, (*errorBuffer)->size);
            (*errorBuffer)->buffer[(*errorBuffer)->size-1] = '\0';

			// destroy compiler state and return
			mcpp_use_mem_buffers(1);	// clear mcpp
            return false;
        }
    }
    else
    {
        char* err = mcpp_get_mem_buffer(ERR);
        if (err)
        {
            size_t size = strlen(err);
            *errorBuffer = new AnyFXErrorBlob;
            (*errorBuffer)->buffer = new char[size];
            (*errorBuffer)->size = size;
            memcpy((void*)(*errorBuffer)->buffer, (void*)err, size);
            (*errorBuffer)->buffer[size-1] = '\0';
			mcpp_use_mem_buffers(1);	// clear mcpp
        }

        return false;
    }	
}

//------------------------------------------------------------------------------
/**
    Run before compilation
*/
void
AnyFXBeginCompile()
{
	//ShInitialize();
	glslang::InitializeProcess();
	/*
#if WIN32
    HDC hDc;
    HGLRC hRc;      
    HACCEL hAccel;
    HINSTANCE hInst;
    HWND hWnd;

    ACCEL acc[1];
    hAccel = CreateAcceleratorTable(acc, 1);
    hInst = GetModuleHandle(0);

    HICON icon = LoadIcon(NULL, IDI_APPLICATION);
    // register window class
    WNDCLASSEX wndClass;
    ZeroMemory(&wndClass, sizeof(wndClass));
    wndClass.cbSize        = sizeof(wndClass);
    wndClass.style         = CS_DBLCLKS | CS_OWNDC;
    wndClass.lpfnWndProc   = AnyFXWinProc;
    wndClass.cbClsExtra    = 0;
    wndClass.cbWndExtra    = sizeof(void*);   // used to hold 'this' pointer
    wndClass.hInstance     = hInst;
    wndClass.hIcon         = icon;
    wndClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH) GetStockObject(NULL_BRUSH);
    wndClass.lpszMenuName  = NULL;
    wndClass.lpszClassName = "AnyFX::Compiler";
    wndClass.hIconSm       = NULL;
    RegisterClassEx(&wndClass);

    DWORD windowStyle = WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;
    DWORD extendedStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

    RECT		windowRect;				// Grabs Rectangle Upper Left / Lower Right Values
    windowRect.left=(long)0;			// Set Left Value To 0
    windowRect.right=(long)0;		// Set Right Value To Requested Width
    windowRect.top=(long)0;				// Set Top Value To 0
    windowRect.bottom=(long)0;		// Set Bottom Value To Requested Height
    AdjustWindowRectEx(&windowRect, windowStyle, FALSE, extendedStyle);		// Adjust Window To True Requested Size

    // open window
    hWnd = CreateWindow("AnyFX::Compiler",
        "AnyFX Compiler",					
        windowStyle,					
        0,								
        0,								
        windowRect.right-windowRect.left,						
        windowRect.bottom-windowRect.top,						
        NULL,							
        NULL,                             
        hInst,                      
        NULL);          


    PIXELFORMATDESCRIPTOR pfd =
    {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    //Flags
        PFD_TYPE_RGBA,            //The kind of framebuffer. RGBA or palette.
        32,                        //Colordepth of the framebuffer.
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,                       //Number of bits for the depthbuffer
        8,                        //Number of bits for the stencilbuffer
        0,                        //Number of Aux buffers in the framebuffer.
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };

    hDc = GetDC(hWnd);
    int pixelFormat = ChoosePixelFormat(hDc, &pfd);
    SetPixelFormat(hDc, pixelFormat, &pfd);
    hRc = wglCreateContext(hDc);
    wglMakeCurrent(hDc, hRc);
#elif __linux__
    dsp = XOpenDisplay(NULL);
    if (dsp == NULL)
    {
        Emit("Could not connect to X.\n");
    }
    root = DefaultRootWindow(dsp);
    vi = glXChooseVisual(dsp, 0, attrs);
    if (vi == NULL)
    {
        Emit("Could not create visual.\n");
    }
    cmap = XCreateColormap(dsp, root, vi->visual, AllocNone);
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask;
    win = XCreateWindow(dsp, root, 0, 0, 1024, 768, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
    XStoreName(dsp, win, "AnyFX Compiler");
    XMapWindow(dsp, win);
    glc = glXCreateContext(dsp, vi, NULL, GL_TRUE);
    glXMakeCurrent(dsp, win, glc);

    XNextEvent(dsp, &xev);

    if (xev.type == Expose)
    {
        XGetWindowAttributes(dsp, win, &gwa);
        glXSwapBuffers(dsp, win);
    }
#elif APPLE
    CGLChoosePixelFormat(attribs, &pix, &npix); 
    CGLCreateContext(pix, NULL, &ctx); 
    CGLSetCurrentContext(ctx);
#endif

	if (glewInitialized != GLEW_OK)
	{
		glewInitialized = glewInit();
	}

#ifndef __ANYFX_COMPILER_LIBRARY__
	if (glewInitialized != GLEW_OK)
    {
        Emit("Glew failed to initialize!\n");
    }

    printf("AnyFX OpenGL capability report:\n");
    printf("Vendor:   %s\n", glGetString(GL_VENDOR)); 
    printf("Renderer: %s\n", glGetString(GL_RENDERER)); 
    printf("Version:  %s\n", glGetString(GL_VERSION)); 
    printf("GLSL:     %s\n\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
#endif
	*/
}

//------------------------------------------------------------------------------
/**
    Run after compilation
*/
void
AnyFXEndCompile()
{
	glslang::FinalizeProcess();
	//ShFinalize();
	/*
#if (WIN32)
    DestroyWindow(hWnd);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRc);
#elif (__linux__)
    glXMakeCurrent(dsp, None, NULL);
    glXDestroyContext(dsp, glc);
    XDestroyWindow(dsp, win);
    XCloseDisplay(dsp);
#elif (APPLE)
#endif
	*/
}
