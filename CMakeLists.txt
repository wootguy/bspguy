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
	src/globals.h	src/globals.cpp
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
	
	# Math
	src/math/vectors.h			src/math/vectors.cpp
	src/math/mat4x4.h			src/math/mat4x4.cpp
	src/math/Polygon3D.h		src/math/Polygon3D.cpp
	src/math/Line2D.h			src/math/Line2D.cpp
	src/math/Clipper.h			src/math/Clipper.cpp
	
	# Misc utils
	src/util/util.h				src/util/util.cpp
	src/util/mstream.h			src/util/mstream.cpp
	src/util/ThreadSafeInt.h	src/util/ThreadSafeInt.cpp
	src/util/bmp.h				src/util/bmp.cpp
	src/util/tga.h				src/util/tga.cpp
	src/util/colors.h			src/util/colors.cpp
	src/util/render_utils.h		src/util/render_utils.cpp
	src/util/HashMap.h			src/util/HashMap.cpp
	src/util/resample.h			src/util/resample.cpp
	src/util/studio.h
	
	# Navigation meshes
	src/nav/NavMesh.h				src/nav/NavMesh.cpp
	src/nav/NavMeshGenerator.h		src/nav/NavMeshGenerator.cpp
	src/nav/LeafNavMeshGenerator.h	src/nav/LeafNavMeshGenerator.cpp
	src/nav/LeafNavMesh.h			src/nav/LeafNavMesh.cpp
	src/nav/PolyOctree.h			src/nav/PolyOctree.cpp
	src/nav/LeafOctree.h			src/nav/LeafOctree.cpp
	
	# OpenGL rendering
	src/gl/primitives.h			src/gl/primitives.cpp
	src/gl/FrameBuffer.h		src/gl/FrameBuffer.cpp
	src/gl/Shader.h				src/gl/Shader.cpp
	src/gl/ShaderProgram.h		src/gl/ShaderProgram.cpp
	src/gl/VertexBuffer.h		src/gl/VertexBuffer.cpp
	src/gl/Texture.h			src/gl/Texture.cpp
	src/gl/TextureArray.h		src/gl/TextureArray.cpp
	src/gl/TextureAtlas.h		src/gl/TextureAtlas.cpp
	
	# Core editor logic
	src/editor/Editor.h				src/editor/Editor.cpp
	src/editor/Fgd.h				src/editor/Fgd.cpp
	src/editor/Command.h			src/editor/Command.cpp
	src/editor/AppSettings.h		src/editor/AppSettings.cpp
	src/editor/PickInfo.h			src/editor/PickInfo.cpp
	
	# 3D viewport
	src/editor/viewport/BspRenderer.h		src/editor/viewport/BspRenderer.cpp
	src/editor/viewport/PointEntRenderer.h	src/editor/viewport/PointEntRenderer.cpp
	src/editor/viewport/StudioMdlRenderer.h		src/editor/viewport/StudioMdlRenderer.cpp
	src/editor/viewport/SprRenderer.h		src/editor/viewport/SprRenderer.cpp
	src/editor/viewport/BaseRenderer.h		src/editor/viewport/BaseRenderer.cpp
	src/editor/viewport/ModelRenderer.h		src/editor/viewport/ModelRenderer.cpp
	src/editor/viewport/NavRenderer.h		src/editor/viewport/NavRenderer.cpp
	
	# UI components
	src/editor/gui/Gui.h					src/editor/gui/Gui.cpp
	src/editor/gui/MenuBar.h				src/editor/gui/MenuBar.cpp
	
	# widgets
	src/editor/gui/widgets/Widget.h			src/editor/gui/widgets/Widget.cpp
	src/editor/gui/widgets/DebugWidget.cpp
	src/editor/gui/widgets/KeyvalueEditor.cpp
	src/editor/gui/widgets/TransformWidget.cpp
	src/editor/gui/widgets/EntityReport.cpp
	src/editor/gui/widgets/FaceEditor.cpp
	src/editor/gui/widgets/HelpWidget.cpp
	src/editor/gui/widgets/AboutWidget.cpp
	src/editor/gui/widgets/LogWidget.cpp
	src/editor/gui/widgets/LimitsWidget.h	src/editor/gui/widgets/LimitsWidget.cpp
	src/editor/gui/widgets/SettingsWidget.cpp
	src/editor/gui/widgets/RadWidget.cpp
	src/editor/gui/widgets/DedupModelsWidget.cpp
	src/editor/gui/widgets/MapMergeWidget.cpp
	src/editor/gui/widgets/FixExtentsWidget.cpp
	src/editor/gui/widgets/ModelMergeWidget.cpp
	src/editor/gui/widgets/LeafWidget.cpp
	
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
	src/lib/lodepng.h
	src/lib/lodepng.cpp
	src/lib/tinyfiledialogs.c
	src/lib/tinyfiledialogs.h
	src/lib/lzma_util.h src/lib/lzma_util.cpp
	src/util/quant.h src/util/quant.cpp
)

set(SHADER_FILES
	src/shaders/vec3_vert.glsl
	src/shaders/vec3_frag.glsl
	
	src/shaders/cvert_vert.glsl
	src/shaders/cvert_frag.glsl
	
	src/shaders/elink_vert.glsl
	src/shaders/elink_frag.glsl
	
	src/shaders/clipnode_vert.glsl
	src/shaders/clipnode_frag.glsl
	
	src/shaders/tvert_vert.glsl
	src/shaders/tvert_frag.glsl
	
	src/shaders/bsp_vert.glsl
	src/shaders/bsp_frag.glsl
	
	src/shaders/spr_vert.glsl
	src/shaders/spr_frag.glsl
	
	src/shaders/mdl_vert.glsl
	src/shaders/mdl_frag.glsl
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
include_directories(src/editor/viewport)
include_directories(src/editor/gui)
include_directories(src/editor/gui/widgets)
include_directories(src/gl)
include_directories(src/qtools)
include_directories(src/util)
include_directories(src/math)
include_directories(src/nav)
include_directories(src/lib)
include_directories(imgui)
include_directories(imgui/examples)
include_directories(imgui/backends)
include_directories(imgui/misc/freetype)
include_directories(freetype/include)
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
											src/gl/FrameBuffer.h
											src/gl/VertexBuffer.h
											src/gl/Texture.h
											src/gl/TextureArray.h
											src/gl/TextureAtlas.h
											src/gl/primitives.h
											${CMAKE_BINARY_DIR}/embedded_shaders.h
											src/gl/shaders.h)
											
	source_group("Source Files\\gl" FILES	src/gl/Shader.cpp
											src/gl/ShaderProgram.cpp
											src/gl/FrameBuffer.cpp
											src/gl/VertexBuffer.cpp
											src/gl/Texture.cpp
											src/gl/TextureArray.cpp
											src/gl/TextureAtlas.cpp
											src/gl/primitives.cpp
											${CMAKE_BINARY_DIR}/embedded_shaders.cpp
											src/gl/shaders.cpp)
											
	source_group("Source Files\\gl\\shaders" FILES ${SHADER_FILES})
											
	source_group("Header Files\\editor" FILES	src/editor/Editor.h
												src/editor/Fgd.h
												src/editor/Gui.h
												src/editor/Command.h
												src/editor/PickInfo.h
												src/editor/AppSettings.h)
											
	source_group("Source Files\\editor" FILES	src/editor/Editor.cpp
												src/editor/Fgd.cpp
												src/editor/Command.cpp
												src/editor/PickInfo.cpp
												src/editor/AppSettings.cpp)
												
	source_group("Header Files\\editor\\viewport" FILES	src/editor/viewport/BspRenderer.h
												src/editor/viewport/PointEntRenderer.h
												src/editor/viewport/ModelRenderer.h
												src/editor/viewport/NavRenderer.h
												src/editor/viewport/StudioMdlRenderer.h
												src/editor/viewport/SprRenderer.h
												src/editor/viewport/BaseRenderer.h)
											
	source_group("Source Files\\editor\\viewport" FILES	
												src/editor/viewport/BspRenderer.cpp
												src/editor/viewport/PointEntRenderer.cpp
												src/editor/viewport/ModelRenderer.cpp
												src/editor/viewport/NavRenderer.cpp
												src/editor/viewport/StudioMdlRenderer.cpp
												src/editor/viewport/SprRenderer.cpp
												src/editor/viewport/BaseRenderer.cpp)
												
	source_group("Header Files\\editor\\gui\\widgets" FILES	src/editor/gui/widgets/Widget.h)
											
	source_group("Source Files\\editor\\gui\\widgets" FILES	src/editor/gui/widgets/Widget.cpp
												src/editor/gui/widgets/DebugWidget.cpp
												src/editor/gui/widgets/KeyvalueEditor.cpp
												src/editor/gui/widgets/TransformWidget.cpp
												src/editor/gui/widgets/EntityReport.cpp
												src/editor/gui/widgets/FaceEditor.cpp
												src/editor/gui/widgets/HelpWidget.cpp
												src/editor/gui/widgets/LogWidget.cpp
												src/editor/gui/widgets/LimitsWidget.cpp
												src/editor/gui/widgets/SettingsWidget.cpp
												src/editor/gui/widgets/AboutWidget.cpp
												src/editor/gui/widgets/RadWidget.cpp
												src/editor/gui/widgets/DedupModelsWidget.cpp
												src/editor/gui/widgets/MapMergeWidget.cpp
												src/editor/gui/widgets/FixExtentsWidget.cpp
												src/editor/gui/widgets/ModelMergeWidget.cpp
												src/editor/gui/widgets/LeafWidget.cpp
	)
	
	source_group("Header Files\\editor\\gui" FILES	src/editor/gui/MenuBar.h
											src/editor/gui/Gui.h
	)
											
	source_group("Source Files\\editor\\gui" FILES	src/editor/gui/MenuBar.cpp
											src/editor/gui/Gui.cpp
	)
											
	source_group("Header Files\\qtools" FILES	src/qtools/rad.h
												src/qtools/vis.h
												src/qtools/winding.h)
												
	source_group("Source Files\\qtools" FILES	src/qtools/rad.cpp
												src/qtools/vis.cpp
												src/qtools/winding.cpp)
												
	source_group("Header Files\\util" FILES		src/util/util.h
												src/util/mstream.h
												src/util/ThreadSafeInt.h
												src/util/bmp.h
												src/util/tga.h
												src/util/quant.h
												src/util/colors.h
												src/util/resample.h
												src/util/HashMap.h
												src/util/render_utils.h
												)
												
	source_group("Source Files\\util" FILES		src/util/util.cpp
												src/util/mstream.cpp
												src/util/ThreadSafeInt.cpp
												src/util/bmp.cpp
												src/util/tga.cpp
												src/util/quant.cpp
												src/util/colors.cpp
												src/util/resample.cpp
												src/util/HashMap.cpp
												src/util/render_utils.cpp
												)
												
	source_group("Header Files\\math" FILES src/math/vectors.h
												src/math/Polygon3D.h
												src/math/Clipper.h
												src/math/Line2D.h
												src/math/mat4x4.h
												)
												
	source_group("Source Files\\math" FILES src/math/vectors.cpp
												src/math/Polygon3D.cpp
												src/math/Clipper.cpp
												src/math/Line2D.cpp
												src/math/mat4x4.cpp
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
	
	source_group("Header Files\\lib" FILES	src/lib/lodepng.h 
											src/lib/lzma_util.h)
	
	source_group("Source Files\\lib" FILES	imgui/imgui.cpp
											imgui/imgui_tables.cpp
											imgui/imgui_widgets.cpp
											imgui/imgui_draw.cpp
											imgui/imgui_demo.cpp
											imgui/backends/imgui_impl_glfw.cpp
											imgui/backends/imgui_impl_opengl2.cpp
											imgui/misc/freetype/imgui_freetype.cpp
											src/lib/tinyfiledialogs.c
											src/lib/lzma_util.cpp
											src/lib/lodepng.cpp)

else()
	add_executable(${PROJECT_NAME} ${SOURCE_FILES})
	target_link_libraries(${PROJECT_NAME} glfw liblzma freetype GL GLU X11 Xxf86vm Xrandr pthread Xi GLEW stdc++fs ${CMAKE_DL_LIBS})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type -fno-omit-frame-pointer -std=c++11")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG_MODE -g -O0")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
endif()
