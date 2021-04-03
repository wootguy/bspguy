cmake_minimum_required(VERSION 3.6)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

project(bspguy)

set(SOURCE_FILES 
	src/main.cpp
	src/types.h
	
	# command line
	src/cli/CommandLine.h	src/cli/CommandLine.cpp
	src/cli/ProgressMeter.h	src/cli/ProgressMeter.cpp
	
	# BSP and related structures
	src/bsp/BspMerger.h		src/bsp/BspMerger.cpp
	src/bsp/Bsp.h			src/bsp/Bsp.cpp
	src/bsp/bsplimits.h
	src/bsp/bsptypes.h		src/bsp/bsptypes.cpp
	src/bsp/Entity.h		src/bsp/Entity.cpp
	src/bsp/Keyvalue.h		src/bsp/Keyvalue.cpp
	src/bsp/Wad.h			src/bsp/Wad.cpp
	src/bsp/remap.h			src/bsp/remap.cpp
	
	# Math and stuff
	src/util/util.h			src/util/util.cpp
	src/util/vectors.h		src/util/vectors.cpp
	src/util/mat4x4.h		src/util/mat4x4.cpp
	
	# OpenGL rendering
	src/gl/shaders.h			src/gl/shaders.cpp
	src/gl/primitives.h			src/gl/primitives.cpp
	src/gl/Shader.h				src/gl/Shader.cpp
	src/gl/ShaderProgram.h		src/gl/ShaderProgram.cpp
	src/gl/VertexBuffer.h		src/gl/VertexBuffer.cpp
	src/gl/Texture.h			src/gl/Texture.cpp
	src/editor/LightmapNode.h	src/editor/LightmapNode.cpp
	
	# 3D editor
	src/editor/Renderer.h			src/editor/Renderer.cpp
	src/editor/Gui.h				src/editor/Gui.cpp
	src/editor/BspRenderer.h		src/editor/BspRenderer.cpp
	src/editor/PointEntRenderer.h	src/editor/PointEntRenderer.cpp
	src/editor/Fgd.h				src/editor/Fgd.cpp
	src/editor/Clipper.h			src/editor/Clipper.cpp
	src/editor/Command.h			src/editor/Command.cpp
	
	# map compiler code
	src/qtools/rad.h		src/qtools/rad.cpp
	src/qtools/vis.h		src/qtools/vis.cpp
	src/qtools/winding.h	src/qtools/winding.cpp
	
	# library files
	imgui/imgui.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_demo.cpp
	imgui/backends/imgui_impl_glfw.cpp
	imgui/backends/imgui_impl_opengl3.cpp
	src/util/lodepng.h
	src/util/lodepng.cpp
)

include_directories(src)
include_directories(src/bsp)
include_directories(src/cli)
include_directories(src/data)
include_directories(src/editor)
include_directories(src/gl)
include_directories(src/qtools)
include_directories(src/util)
include_directories(imgui)
include_directories(imgui/examples)
include_directories(imgui/backends)
include_directories(glew/include)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} glfw)

add_definitions(-DGLEW_STATIC)

if(MSVC)
	add_subdirectory(glfw)
	
	# compile using the static runtime
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /wd4244 /wd4018")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /wd4244 /wd4018 /W0")
	
	# Disable C++ exceptions
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT bspguy)
	
	target_link_libraries(${PROJECT_NAME} opengl32 ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/x64/glew32s.lib)
	
	source_group("Header Files\\bsp" FILES	src/bsp/BspMerger.h
											src/bsp/Bsp.h
											src/bsp/bsplimits.h
											src/bsp/bsptypes.h
											src/bsp/Entity.h
											src/bsp/Keyvalue.h
											src/bsp/Wad.h
											src/bsp/remap.h)
											
	source_group("Source Files\\bsp" FILES	src/bsp/BspMerger.cpp
											src/bsp/Bsp.cpp
											src/bsp/bsptypes.cpp
											src/bsp/Entity.cpp
											src/bsp/Keyvalue.cpp
											src/bsp/Wad.cpp
											src/bsp/remap.cpp)
	
	source_group("Header Files\\cli" FILES	src/cli/CommandLine.h
											src/cli/ProgressMeter.h)
											
	source_group("Source Files\\cli" FILES	src/cli/CommandLine.cpp
											src/cli/ProgressMeter.cpp)
	
	source_group("Header Files\\gl" FILES	src/gl/Shader.h
											src/gl/ShaderProgram.h
											src/gl/VertexBuffer.h
											src/gl/Texture.h
											src/gl/primitives.h
											src/gl/shaders.h)
											
	source_group("Source Files\\gl" FILES	src/gl/Shader.cpp
											src/gl/ShaderProgram.cpp
											src/gl/VertexBuffer.cpp
											src/gl/Texture.cpp
											src/gl/primitives.cpp
											src/gl/shaders.cpp)
											
	source_group("Header Files\\editor" FILES	src/editor/BspRenderer.h
												src/editor/LightmapNode.h
												src/editor/Renderer.h
												src/editor/Fgd.h
												src/editor/Gui.h
												src/editor/PointEntRenderer.h
												src/editor/Command.h
												src/editor/Clipper.h)
											
	source_group("Source Files\\editor" FILES	src/editor/BspRenderer.cpp
												src/editor/LightmapNode.cpp
												src/editor/Renderer.cpp
												src/editor/Fgd.cpp
												src/editor/Gui.cpp
												src/editor/PointEntRenderer.cpp
												src/editor/Command.cpp
												src/editor/Clipper.cpp)
											
	source_group("Header Files\\qtools" FILES	src/qtools/rad.h
												src/qtools/vis.h
												src/qtools/winding.h)
												
	source_group("Source Files\\qtools" FILES	src/qtools/rad.cpp
												src/qtools/vis.cpp
												src/qtools/winding.cpp)
												
	source_group("Header Files\\util" FILES		src/util/util.h
												src/util/vectors.h
												src/util/mat4x4.h)
												
	source_group("Source Files\\util" FILES		src/util/util.cpp
												src/util/vectors.cpp
												src/util/mat4x4.cpp)
	
	source_group("Header Files\\util\\lib" FILES	src/util/lodepng.h)
	
	source_group("Source Files\\util\\lib" FILES	imgui/imgui.cpp
													imgui/imgui_tables.cpp
													imgui/imgui_widgets.cpp
													imgui/imgui_draw.cpp
													imgui/imgui_demo.cpp
													imgui/backends/imgui_impl_glfw.cpp
													imgui/backends/imgui_impl_opengl3.cpp
													src/util/lodepng.cpp)

else()
	target_link_libraries(${PROJECT_NAME} GL GLU X11 Xxf86vm Xrandr pthread Xi GLEW stdc++fs)
	set(CMAKE_CXX_FLAGS "-Wall -std=c++11")
	set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
	set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -w -Wfatal-errors")
endif()
