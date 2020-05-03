cmake_minimum_required(VERSION 3.6)
project(bspguy)

set(SOURCE_FILES 
    src/main.cpp
	src/Bsp.h			src/Bsp.cpp
	src/Entity.h		src/Entity.cpp
	src/Keyvalue.h		src/Keyvalue.cpp
	src/Wad.h			src/Wad.cpp
    src/util.h			src/util.cpp
	src/types.h
	src/vectors.h		src/vectors.cpp
	src/lodepng.h		src/lodepng.cpp
	src/rad.h			src/rad.cpp
	src/winding.h		src/winding.cpp
	src/bsplimits.h
)

if(MSVC)
    # compile using the static runtime
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /wd4244 /wd4018")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd /wd4244 /wd4018")
	
	# Disable C++ exceptions
	string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT bspguy)
  
else()
    set(CMAKE_CXX_FLAGS "-Wall")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -fno-exceptions -w -Wfatal-errors")
endif()

include_directories(src)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})