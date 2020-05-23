cmake_minimum_required(VERSION 3.6)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

project(bspguy)

set(SOURCE_FILES 
    src/main.cpp
	src/Renderer.h		src/Renderer.cpp
	src/CommandLine.h	src/CommandLine.cpp
	src/BspMerger.h		src/BspMerger.cpp
	src/Bsp.h			src/Bsp.cpp
	src/Entity.h		src/Entity.cpp
	src/Keyvalue.h		src/Keyvalue.cpp
	src/ProgressMeter.h	src/ProgressMeter.cpp
	src/Wad.h			src/Wad.cpp
    src/remap.h			src/remap.cpp
    src/util.h			src/util.cpp
	src/types.h
	src/vectors.h		src/vectors.cpp
	src/lodepng.h		src/lodepng.cpp
	src/rad.h			src/rad.cpp
	src/vis.h			src/vis.cpp
	src/winding.h		src/winding.cpp
	src/bsplimits.h
	src/mat4x4.h		src/mat4x4.cpp
	
	src/gfx/shaders.h
	src/gfx/primitives.h		src/gfx/primitives.cpp
	src/gfx/Shader.h			src/gfx/Shader.cpp
	src/gfx/ShaderProgram.h		src/gfx/ShaderProgram.cpp
	src/gfx/VertexBuffer.h		src/gfx/VertexBuffer.cpp
	src/gfx/Texture.h			src/gfx/Texture.cpp
	src/gfx/BspRenderer.h		src/gfx/BspRenderer.cpp
	src/gfx/LightmapNode.h		src/gfx/LightmapNode.cpp
)

add_subdirectory(glfw)

include_directories(src)
include_directories(glew/include)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} glfw)

add_definitions(-DGLEW_STATIC)

if(MSVC)
    # compile using the static runtime
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /wd4244 /wd4018")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /wd4244 /wd4018")
	
	# Disable C++ exceptions
	string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT bspguy)
	
	target_link_libraries(${PROJECT_NAME} opengl32 ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/x64/glew32s.lib)
	
	source_group("Header Files\\gfx" FILES src/gfx/Shader.h src/gfx/ShaderProgram.h src/gfx/VertexBuffer.h 
										   src/gfx/Texture.h src/gfx/primitives.h src/gfx/shaders.h
										   src/gfx/BspRenderer.h src/gfx/LightmapNode.h)
	source_group("Source Files\\gfx" FILES src/gfx/Shader.cpp src/gfx/ShaderProgram.cpp src/gfx/VertexBuffer.cpp 
										   src/gfx/Texture.cpp src/gfx/primitives.cpp src/gfx/BspRenderer.cpp
										   src/gfx/LightmapNode.cpp) 
  
else()
    set(CMAKE_CXX_FLAGS "-Wall -std=c++11")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -w -Wfatal-errors")
endif()
