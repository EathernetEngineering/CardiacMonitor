cmake_minimum_required(VERSION 3.5)

project(CeeCardiacMonitor LANGUAGES C CXX)

list(APPEND SOURCES main.cc serial.cc graph.c graphics.c audio.c)
list(APPEND INCLUDEDIRS /opt/vc/include)
list(APPEND LIBRARYDIRS /opt/vc/lib)
list(APPEND LIBRARIES m pthread rt asound bcm_host brcmEGL brcmGLESv2)

add_executable(CardiacMonitor ${SOURCES})

set_property(TARGET CardiacMonitor PROPERTY CXX_STANDARD 20)
set_property(TARGET CardiacMonitor PROPERTY CXX_STANDARD_REQUIRED 20)

target_include_directories(CardiacMonitor PRIVATE ${INCLUDEDIRS})
target_link_directories(CardiacMonitor PRIVATE ${LIBRARYDIRS})
target_link_libraries(CardiacMonitor PRIVATE ${LIBRARIES})


