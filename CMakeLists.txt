cmake_minimum_required(VERSION 3.18...4.0)
project(visualization.zunradio)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(Kodi REQUIRED)

set(ZUNRADIO_SOURCES src/Scena.cpp)
set(ZUNRADIO_HEADERS src/Scena.h)

if(WIN32)
  set(SHADER_FILES src/ScenaVertexShader.hlsl
                   src/ScenaPixelShader.hlsl)
  set(SHADER_INCLUDES)
  list(APPEND ZUNRADIO_SOURCES src/Main_dx.cpp)
  foreach(SHADER_FILE ${SHADER_FILES})
    get_filename_component(FileName ${SHADER_FILE} NAME_WE)
    if(${FileName} MATCHES "(.+Vertex.+)")
      set(ShaderType vs)
    elseif(${FileName} MATCHES "(.+Pixel.+)")
      set(ShaderType ps)
    else()
      message(FATAL_ERROR ${SHADER_FILE}": Unknown shader type.")
    endif()
    set(IncFile ${CMAKE_CURRENT_BINARY_DIR}/${FileName}.inc)
    list(APPEND SHADER_INCLUDES ${IncFile})
    add_custom_command(
      OUTPUT ${IncFile}
      COMMAND fxc /nologo /T${ShaderType}_4_0_level_9_3 /Vn ${FileName}Code /Fh ${IncFile} ${SHADER_FILE}
      MAIN_DEPENDENCY ${SHADER_FILE}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      VERBATIM)
  endforeach()
  add_custom_target(generate ALL DEPENDS ${SHADER_INCLUDES})
  set(INCLUDES ${CMAKE_CURRENT_BINARY_DIR})
  set(DEPLIBS d3d11.lib)
else()
  if(APP_RENDER_SYSTEM STREQUAL "gl" OR NOT APP_RENDER_SYSTEM)
    find_package(OpenGl REQUIRED)
    set(DEPLIBS ${OPENGL_LIBRARIES})
    set(INCLUDES ${OPENGL_INCLUDE_DIR})
    add_definitions(${OPENGL_DEFINITIONS})
  elseif(APP_RENDER_SYSTEM STREQUAL "gles")
    find_package(OpenGLES REQUIRED)
    set(DEPLIBS ${OPENGLES_LIBRARIES})
    set(INCLUDES ${OPENGLES_INCLUDE_DIR})
    add_definitions(${OPENGLES_DEFINITIONS})
  endif()
  list(APPEND ZUNRADIO_SOURCES src/Main_gl.cpp)
endif()

include_directories(${INCLUDES}
                    ${PROJECT_SOURCE_DIR}/src
                    ${KODI_INCLUDE_DIR}/..) # same include path as the official visualizations

build_addon(visualization.zunradio ZUNRADIO DEPLIBS)

if(WIN32)
  add_dependencies(visualization.zunradio generate)
endif()

include(CPack)
