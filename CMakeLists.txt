cmake_minimum_required(VERSION 3.6)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

project(bspguy)

if (MSVC)
	# static runtime for all projects
	add_compile_options(/MT)
	add_definitions(-UHAVE___BUILTIN_ASSUME_ALIGNED) # probably only needed for v141_xp
endif()

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
	src/util/util.h				src/util/util.cpp
	src/util/vectors.h			src/util/vectors.cpp
	src/util/mat4x4.h			src/util/mat4x4.cpp
	src/util/Polygon3D.h		src/util/Polygon3D.cpp
	src/util/Line2D.h			src/util/Line2D.cpp
	src/util/mstream.h			src/util/mstream.cpp
	src/util/ThreadSafeInt.h	src/util/ThreadSafeInt.cpp
	src/util/bmp.h				src/util/bmp.cpp
	src/util/tga.h				src/util/tga.cpp
	src/globals.h				src/globals.cpp
	
	# Navigation meshes
	src/nav/NavMesh.h				src/nav/NavMesh.cpp
	src/nav/NavMeshGenerator.h		src/nav/NavMeshGenerator.cpp
	src/nav/LeafNavMeshGenerator.h	src/nav/LeafNavMeshGenerator.cpp
	src/nav/LeafNavMesh.h			src/nav/LeafNavMesh.cpp
	src/nav/PolyOctree.h			src/nav/PolyOctree.cpp
	src/nav/LeafOctree.h			src/nav/LeafOctree.cpp
	
	# OpenGL rendering
	src/gl/primitives.h			src/gl/primitives.cpp
	src/gl/Shader.h				src/gl/Shader.cpp
	src/gl/ShaderProgram.h		src/gl/ShaderProgram.cpp
	src/gl/VertexBuffer.h		src/gl/VertexBuffer.cpp
	src/gl/Texture.h			src/gl/Texture.cpp
	src/gl/TextureArray.h			src/gl/TextureArray.cpp
	
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
	src/editor/TextureAtlas.h		src/editor/TextureAtlas.cpp
	src/editor/studio.h
	
	# widgets
	src/editor/widgets/Widget.h			src/editor/widgets/Widget.cpp
	src/editor/widgets/DebugWidget.cpp
	src/editor/widgets/KeyvalueEditor.cpp
	src/editor/widgets/TransformWidget.cpp
	src/editor/widgets/EntityReport.cpp
	src/editor/widgets/FaceEditor.cpp
	src/editor/widgets/HelpWidget.cpp
	src/editor/widgets/AboutWidget.cpp
	src/editor/widgets/LogWidget.cpp
	src/editor/widgets/LimitsWidget.h	src/editor/widgets/LimitsWidget.cpp
	src/editor/widgets/SettingsWidget.cpp
	
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
	imgui/backends/imgui_impl_opengl2.cpp
	imgui/misc/freetype/imgui_freetype.cpp
	src/util/lodepng.h
	src/util/lodepng.cpp
	src/util/tinyfiledialogs.c
	src/util/tinyfiledialogs.h
	src/util/lzma_util.h src/util/lzma_util.cpp
	src/util/quant.h src/util/quant.cpp
)

set(SHADER_FILES
	src/shaders/vec3_vert.glsl
	src/shaders/vec3_frag.glsl
	src/shaders/vec3_depth_frag.glsl
	
	src/shaders/cvert_vert.glsl
	src/shaders/cvert_frag.glsl
	
	src/shaders/tvert_vert.glsl
	src/shaders/tvert_frag.glsl
	
	src/shaders/bsp_vert.glsl
	src/shaders/bsp_legacy_frag.glsl
	src/shaders/bsp_3dtex_frag.glsl
	src/shaders/bsp_arraytex_frag.glsl
	
	src/shaders/spr_vert.glsl
	src/shaders/spr_frag.glsl
	
	src/shaders/mdl_vert.glsl
	src/shaders/mdl_frag.glsl
	src/shaders/mdl_legacy_vert.glsl
)

# Custom command that always runs because cmake doesn't care if .glsl contents change.
# The command will check if the generated file needs updating or not
add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/__dummy_file.h  # fake file that never exists, causing this step to run every build
	${CMAKE_BINARY_DIR}/embedded_shaders.cpp
	COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/embed_shaders.cmake" "embedded_shaders.h" "embedded_shaders.cpp" ${SHADER_FILES}
)
add_custom_target(
	my_custom_target_that_always_runs ALL
	DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/__dummy_file.h
)

set(SOURCE_FILES ${SOURCE_FILES} "${CMAKE_BINARY_DIR}/embedded_shaders.cpp")

include_directories(src)
include_directories(src/bsp)
include_directories(src/cli)
include_directories(src/data)
include_directories(src/editor)
include_directories(src/editor/widgets)
include_directories(src/gl)
include_directories(src/qtools)
include_directories(src/util)
include_directories(src/nav)
include_directories(src/lib)
include_directories(imgui)
include_directories(imgui/examples)
include_directories(imgui/backends)
include_directories(imgui/misc/freetype)
include_directories(glew/include)
include_directories(${CMAKE_BINARY_DIR})

add_definitions(-DGLEW_STATIC)

if(MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

	add_executable(${PROJECT_NAME} ${SOURCE_FILES} src/data/icons/app_icon.rc)
	target_link_libraries(${PROJECT_NAME} glfw liblzma)

	add_custom_command(
		TARGET ${PROJECT_NAME}
		PRE_BUILD
		COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/embed_shaders.cmake" "embedded_shaders.h" "embedded_shaders.cpp" "${SHADER_FILES}"
		${CMAKE_CURRENT_BINARY_DIR}/__dummy_file.h
	)
	add_dependencies(${PROJECT_NAME} my_custom_target_that_always_runs)

	# no warnings for release builds
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W0")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /W1")

	# parallel compile,
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /we4715")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /MP /we4715")
	
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG_MODE")
	
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /SUBSYSTEM:WINDOWS")
	set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /SUBSYSTEM:CONSOLE")
	
		
	add_subdirectory(glfw)
	add_subdirectory(freetype)
	
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT bspguy)
	
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(GLEW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/Win32/glew32s.lib)
	else()
		set(GLEW_PATH ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/x64/glew32s.lib)
	endif()
	
	target_link_libraries(${PROJECT_NAME} opengl32 freetype ${GLEW_PATH})
	
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
											src/gl/TextureArray.h
											src/gl/primitives.h
											${CMAKE_BINARY_DIR}/embedded_shaders.h
											src/gl/shaders.h)
											
	source_group("Source Files\\gl" FILES	src/gl/Shader.cpp
											src/gl/ShaderProgram.cpp
											src/gl/VertexBuffer.cpp
											src/gl/Texture.cpp
											src/gl/TextureArray.cpp
											src/gl/primitives.cpp
											${CMAKE_BINARY_DIR}/embedded_shaders.cpp
											src/gl/shaders.cpp)
											
	source_group("Source Files\\gl\\shaders" FILES ${SHADER_FILES})
											
	source_group("Header Files\\editor" FILES	src/editor/BspRenderer.h
												src/editor/TextureAtlas.h
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
												src/editor/TextureAtlas.cpp
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
												
	source_group("Header Files\\editor\\widgets" FILES	src/editor/widgets/Widget.h)
											
	source_group("Source Files\\editor\\widgets" FILES	src/editor/widgets/Widget.cpp
												src/editor/widgets/DebugWidget.cpp
												src/editor/widgets/KeyvalueEditor.cpp
												src/editor/widgets/TransformWidget.cpp
												src/editor/widgets/EntityReport.cpp
												src/editor/widgets/FaceEditor.cpp
												src/editor/widgets/HelpWidget.cpp
												src/editor/widgets/LogWidget.cpp
												src/editor/widgets/LimitsWidget.cpp
												src/editor/widgets/SettingsWidget.cpp
												src/editor/widgets/AboutWidget.cpp
	)
											
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
												src/util/ThreadSafeInt.h
												src/util/mat4x4.h
												src/util/bmp.h
												src/util/tga.h
												src/util/quant.h
												)
												
	source_group("Source Files\\util" FILES		src/util/util.cpp
												src/util/vectors.cpp
												src/util/Polygon3D.cpp
												src/util/Line2D.cpp
												src/util/mstream.cpp
												src/util/lzma_util.cpp
												src/util/ThreadSafeInt.cpp
												src/util/mat4x4.cpp
												src/util/bmp.cpp
												src/util/tga.cpp
												src/util/quant.cpp
												)
												
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
													imgui/backends/imgui_impl_opengl2.cpp
													imgui/misc/freetype/imgui_freetype.cpp
													src/util/tinyfiledialogs.c
													src/util/lodepng.cpp)

else()
	add_executable(${PROJECT_NAME} ${SOURCE_FILES})
	target_link_libraries(${PROJECT_NAME} glfw liblzma freetype GL GLU X11 Xxf86vm Xrandr pthread Xi GLEW stdc++fs ${CMAKE_DL_LIBS})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -fno-omit-frame-pointer -std=c++11")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG_MODE -g -O0")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
endif()
