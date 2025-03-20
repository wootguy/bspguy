cmake_minimum_required(VERSION 3.6)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

project(bspguy)

set(BUILD_TESTING OFF)
set(XZ_TOOL_XZ OFF)
set(XZ_TOOL_XZDEC OFF)
set(XZ_TOOL_LZMADEC OFF)
set(XZ_TOOL_LZMAINFO OFF)
if (UNIX)
	set(XZ_SMALL ON) # fixes build error for 32bit
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/xz)

if (UNIX)
	#set_target_properties(liblzma PROPERTIES COMPILE_OPTIONS "-m32" LINK_FLAGS "-m32")
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/xz/src/liblzma/api)

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
	src/bsp/colors.h		src/bsp/colors.cpp
	
	# Math and stuff
	src/util/util.h			src/util/util.cpp
	src/util/vectors.h		src/util/vectors.cpp
	src/util/mat4x4.h		src/util/mat4x4.cpp
	src/util/Polygon3D.h	src/util/Polygon3D.cpp
	src/util/Line2D.h		src/util/Line2D.cpp
	src/util/mstream.h		src/util/mstream.cpp
	src/globals.h			src/globals.cpp
	
	# Navigation meshes
	src/nav/NavMesh.h				src/nav/NavMesh.cpp
	src/nav/NavMeshGenerator.h		src/nav/NavMeshGenerator.cpp
	src/nav/LeafNavMeshGenerator.h	src/nav/LeafNavMeshGenerator.cpp
	src/nav/LeafNavMesh.h			src/nav/LeafNavMesh.cpp
	src/nav/PolyOctree.h			src/nav/PolyOctree.cpp
	src/nav/LeafOctree.h			src/nav/LeafOctree.cpp
	
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
	src/editor/AppSettings.h		src/editor/AppSettings.cpp
	src/editor/MdlRenderer.h		src/editor/MdlRenderer.cpp
	src/editor/SprRenderer.h		src/editor/SprRenderer.cpp
	src/editor/BaseRenderer.h		src/editor/BaseRenderer.cpp
	src/editor/studio.h
	
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
	src/util/tinyfiledialogs.c
	src/util/tinyfiledialogs.h
	src/util/lzma_util.h src/util/lzma_util.cpp
)

include_directories(src)
include_directories(src/bsp)
include_directories(src/cli)
include_directories(src/data)
include_directories(src/editor)
include_directories(src/gl)
include_directories(src/qtools)
include_directories(src/util)
include_directories(src/nav)
include_directories(imgui)
include_directories(imgui/examples)
include_directories(imgui/backends)
include_directories(glew/include)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} glfw liblzma)

add_definitions(-DGLEW_STATIC)

if(MSVC)
	# no warnings for release builds
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W0")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /W1")

	# parallel compile
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	# compile using the static runtime	
	set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		
	add_subdirectory(glfw)
	
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT bspguy)
	
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(GLEW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/Win32/glew32s.lib)
	else()
		set(GLEW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/x64/glew32s.lib)
	endif()
	
	target_link_libraries(${PROJECT_NAME} opengl32 ${GLEW_PATH})
	
	source_group("Header Files\\bsp" FILES	src/bsp/BspMerger.h
											src/bsp/Bsp.h
											src/bsp/bsplimits.h
											src/bsp/bsptypes.h
											src/bsp/Entity.h
											src/bsp/Keyvalue.h
											src/bsp/Wad.h
											src/bsp/colors.h
											src/bsp/remap.h)
											
	source_group("Source Files\\bsp" FILES	src/bsp/BspMerger.cpp
											src/bsp/Bsp.cpp
											src/bsp/bsptypes.cpp
											src/bsp/Entity.cpp
											src/bsp/Keyvalue.cpp
											src/bsp/Wad.cpp
											src/bsp/colors.cpp
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
												src/editor/AppSettings.h
												src/editor/MdlRenderer.h
												src/editor/SprRenderer.h
												src/editor/BaseRenderer.h
												src/editor/Clipper.h)
											
	source_group("Source Files\\editor" FILES	src/editor/BspRenderer.cpp
												src/editor/LightmapNode.cpp
												src/editor/Renderer.cpp
												src/editor/Fgd.cpp
												src/editor/Gui.cpp
												src/editor/PointEntRenderer.cpp
												src/editor/Command.cpp
												src/editor/AppSettings.cpp
												src/editor/MdlRenderer.cpp
												src/editor/SprRenderer.cpp
												src/editor/BaseRenderer.cpp
												src/editor/Clipper.cpp)
											
	source_group("Header Files\\qtools" FILES	src/qtools/rad.h
												src/qtools/vis.h
												src/qtools/winding.h)
												
	source_group("Source Files\\qtools" FILES	src/qtools/rad.cpp
												src/qtools/vis.cpp
												src/qtools/winding.cpp)
												
	source_group("Header Files\\util" FILES		src/util/util.h
												src/util/vectors.h
												src/util/Polygon3D.h
												src/util/Line2D.h
												src/util/mstream.h
												src/util/lzma_util.h
												src/util/mat4x4.h)
												
	source_group("Source Files\\util" FILES		src/util/util.cpp
												src/util/vectors.cpp
												src/util/Polygon3D.cpp
												src/util/Line2D.cpp
												src/util/mstream.cpp
												src/util/lzma_util.cpp
												src/util/mat4x4.cpp)
												
	source_group("Header Files\\nav" FILES		src/nav/NavMesh.h
												src/nav/NavMeshGenerator.h
												src/nav/LeafNavMeshGenerator.h
												src/nav/LeafNavMesh.h
												src/nav/PolyOctree.h
												src/nav/LeafOctree.h)
												
	source_group("Source Files\\nav" FILES		src/nav/NavMesh.cpp
												src/nav/NavMeshGenerator.cpp
												src/nav/LeafNavMeshGenerator.cpp
												src/nav/LeafNavMesh.cpp
												src/nav/PolyOctree.cpp
												src/nav/LeafOctree.cpp)
	
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
	target_link_libraries(${PROJECT_NAME} GL GLU X11 Xxf86vm Xrandr pthread Xi GLEW stdc++fs ${CMAKE_DL_LIBS})
	set(CMAKE_CXX_FLAGS "-Wall -std=c++11")
	set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
	set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -w -Wfatal-errors")
endif()
