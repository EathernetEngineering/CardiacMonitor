cmake_minimum_required(VERSION 3.5)

project(CeeCardiacMonitor LANGUAGES C CXX)

list(APPEND SOURCES main.cc graph.c graphics.c audio.c i2c.cc adc.cc)
list(APPEND INCLUDEDIRS /usr/include /usr/include/libdrm)
list(APPEND LIBRARYDIRS /usr/lib/arm-linux-gnueabihf)
list(APPEND LIBRARIES m pthread drm gbm rt asound bcm_host EGL GLESv2)

add_executable(CardiacMonitor ${SOURCES})

set_property(TARGET CardiacMonitor PROPERTY CXX_STANDARD 20)
set_property(TARGET CardiacMonitor PROPERTY CXX_STANDARD_REQUIRED 20)

target_include_directories(CardiacMonitor PRIVATE ${INCLUDEDIRS})
target_link_directories(CardiacMonitor PRIVATE ${LIBRARYDIRS})
target_link_libraries(CardiacMonitor PRIVATE ${LIBRARIES})


