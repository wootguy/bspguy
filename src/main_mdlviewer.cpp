/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
****/
// todo:
// - clean up this mess

// updates:
// 1-4-98		fixed initialization
// 23-11-2018	moved from GLUT to GLFW, reimplemented zooming

// External Libraries
#include <GLFW/glfw3.h>

// Default Libraries
#include <stdio.h>

#include "mdlviewer/studio_render.h"

char g_version_string[] = " TEST MDLVIEWER ";
bool g_verbose = true;


#pragma warning( disable : 4244 ) // conversion from 'double ' to 'float ', possible loss of data
#pragma warning( disable : 4305 ) // truncation from 'const double ' to 'float '

#define APPLICATION_NAME "Half-Life 1 Model Viewer"
#define SCREEN_WIDTH  512
#define SCREEN_HEIGHT 512

void perspectiveGL(GLdouble fovY, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void FramesPerSecond();


float			gldepthmin = 0;
float			gldepthmax = 10.0;

static int		pstyle;
static int		translate = 1;
static int		mesh = 1;
static float	transx = 0, transy = 0, transz = -2, rotx = 235, roty = -90;
static float	amplitude = 0.03;
static float	freq = 5.0f;
static float	phase = .00003;
static int		originalxpos = -1, originalypos = -1;
static int		show_t = 1;
static int		motion;

#define PAN	1
#define ROT	2
#define ZOOM 3

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
	glDepthFunc(GL_LEQUAL);
	glDepthRange(gldepthmin, gldepthmax);
	glDepthMask(1);
}

static StudioModel tempmodel;

void mdlviewer_display()
{
	R_Clear();

	tempmodel.SetBlending(0, 0.0);
	tempmodel.SetBlending(1, 0.0);

	static float prev;
	float curr = glfwGetTime() ;
	tempmodel.AdvanceFrame(curr - prev);
	prev = curr;

	tempmodel.DrawModel();
}

void mdlviewer_init( char *modelname )
{
	tempmodel.Init(modelname);
	tempmodel.SetSequence(0);

	tempmodel.SetController( 0, 0.0 );
	tempmodel.SetController( 1, 0.0 );
	tempmodel.SetController( 2, 0.0 );
	tempmodel.SetController( 3, 0.0 );
	tempmodel.SetMouth( 0 );

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
	perspectiveGL(50., 1., .1, 10.);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.247, 0.247, 0.247, 0);
}

void mdlviewer_nextsequence( void )
{
	int iSequence = tempmodel.GetSequence( );
	if (iSequence == tempmodel.SetSequence(iSequence + 1 ))
	{
		tempmodel.SetSequence( 0 );
	}
}

void pan(int x, int y) 
{
    transx += (x-originalxpos)/500.;
    transy -= (y- originalypos)/500.;
	originalxpos = x;
	originalypos = y;
}

void zoom(int x, int y) 
{
    transz +=  (x-originalxpos)/20.;
	originalxpos = x;
}

void rotate(int x, int y) 
{
    rotx += x-originalxpos;
	if (rotx > 360.)
	{
		rotx -= 360.;
	}
    else if (rotx < -360.)
	{
		rotx += 360.;
	}
    roty += y-originalypos;

	if (roty > 360.)
	{
		roty -= 360.;
	}
	else if (roty < -360.)
	{
		roty += 360.;
	}
	originalxpos = x;
	originalypos = y;
}

void cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
	if (motion == PAN)
	{
		pan(xpos, ypos);
	}
	else if (motion == ROT)
	{
		rotate(xpos, ypos);
	}
	else if (motion == ZOOM)
	{
		zoom(xpos, ypos);
	}
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	// Cursor position
	double xpos;
	double ypos;

	if (action == GLFW_PRESS)
	{
		switch (button)
		{
		case GLFW_MOUSE_BUTTON_LEFT:
			// Getting cursor position
			glfwGetCursorPos(window, &xpos, &ypos);

			motion = PAN;
			cursorPosCallback(window, originalxpos = xpos, originalypos = ypos);
			break;
		case GLFW_MOUSE_BUTTON_RIGHT:
			// Getting cursor position
			glfwGetCursorPos(window, &xpos, &ypos);
			
			motion = ROT;
			cursorPosCallback(window, originalxpos = xpos, originalypos = ypos);
			break;
		case GLFW_MOUSE_BUTTON_MIDDLE:
			// Getting cursor position
			glfwGetCursorPos(window, &xpos, &ypos);

			motion = ZOOM;
			cursorPosCallback(window, originalxpos = xpos, originalypos = ypos);
			break;
		}
	}
	else if (action == GLFW_RELEASE)
	{
		motion = 0;
	}
}

void init(char *arg) 
{
	mdlviewer_init( arg );

	glEnable(GL_TEXTURE_2D);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	perspectiveGL(50., 1., .1, 10.);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glClearColor(0.247, 0.247, 0.247, 0);
}

void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glPushMatrix();

	glTranslatef(transx, transy, transz);

	glRotatef(rotx, 0., 1., 0.);
	glRotatef(roty, 1., 0., 0.);

	glScalef(0.01, 0.01, 0.01);
	glCullFace(GL_FRONT);
	glEnable(GL_DEPTH_TEST);

	mdlviewer_display();

	glPopMatrix();
}

void resize(GLFWwindow* window, int width, int height)
{
	// Set the viewport to be the entire window
	glViewport(0, 0, width, height);
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (action == GLFW_PRESS)
	{		
		if (key == GLFW_KEY_P)
		{
			printf("Translation: %f, %f %f\n", transx, transy, transz );
		}
		
		if (key == GLFW_KEY_ESCAPE)
		{
			glfwSetWindowShouldClose(window, GL_TRUE);
		}
		
		if (key == GLFW_KEY_SPACE)
		{
			mdlviewer_nextsequence();
		}
	}
}

int main(int argc, char** argv)
{
	GLFWwindow *window;

	// Initialize the library
	if (!glfwInit())
	{
		return -1;
	}
	else
	{
		printf("Welcome to the Half-Life 1 model viewer\n");
		printf("---------------------------------------\n");
		printf("Log:\n");
		printf("GLFW successfully initialized\n");
		printf("---------------------------------------\n");
		printf("Help:\n");
		printf("Pan - left mouse\n");
		printf("Rotate - right mouse\n");
		printf("Zoom - middle mouse\n");
		printf("Show translation - P\n");
		printf("---------------------------------------\n");
	}

	// Create a windowed mode window and its OpenGL context
    window = glfwCreateWindow(SCREEN_WIDTH, SCREEN_HEIGHT, APPLICATION_NAME, NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return -1;
    }

	// Make the window's context current
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, resize);

	// Events
	glfwSetKeyCallback(window, keyCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);

	// Initialize .mdl file
	init( argv[1] );

	// Loop until the user closes the window
	while (!glfwWindowShouldClose(window))
	{
		// Render here
		display();

		// Swap front and back buffers
		glfwSwapBuffers(window);

		// Poll for and process events
		glfwPollEvents();
	}

	glfwTerminate();
	return 0;
}

// Replaces gluPerspective. Sets the frustum to perspective mode.
// fovY     - Field of vision in degrees in the y direction
// aspect   - Aspect ratio of the viewport
// zNear    - The near clipping distance
// zFar     - The far clipping distance
void perspectiveGL(GLdouble fovY, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
	const GLdouble pi = 3.1415926535897932384626433832795;
	GLdouble fW, fH;

	fH = tan((fovY / 2) / 180 * pi) * zNear;
	fH = tan(fovY / 360 * pi) * zNear;
	fW = fH * aspect;
	glFrustum(-fW, fW, -fH, fH, zNear, zFar);
}

/*
// Old GLUT main()
int main(int argc, char** argv)
{
    glutInitWindowSize(SCREEN_WIDTH, SCREEN_HEIGHT);
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA|GLUT_DOUBLE);
    (void)glutCreateWindow(argv[0]);
    init( argv[1] );
	glutSetWindowTitle("Half-Life 1 Model Viewer");
    glutDisplayFunc(display);
    glutKeyboardFunc(key);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutMainLoop();
    return 0;
}
*/