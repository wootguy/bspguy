#include "Bsp.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>

class Renderer {
public:
	vector<Bsp*> maps;

	Renderer();
	~Renderer();

	void addMap(Bsp* map);

	void renderLoop();

private:
	GLFWwindow* window;

	void render();
};