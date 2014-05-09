cmake_minimum_required (VERSION 2.6)
project ( j2k ) 

find_package( PkgConfig REQUIRED )

# This assumes that <ffmpeg_install_dir>/lib/pkgconfig is in
# system variable $PKG_CONFIG_PATH
pkg_check_modules( FFMPEG libavcodec libavformat libavutil libswscale )
# message( STATUS ${FFMPEG_INCLUDE_DIRS} )
# message( STATUS ${FFMPEG_LIBRARIES} )
include_directories( ${FFMPEG_INCLUDE_DIRS} )
link_directories( ${FFMPEG_LIBRARY_DIRS} )
set( LIBS ${LIBS} ${FFMPEG_LIBRARIES} )

add_executable( encode_j2k encode_j2k.c )
target_link_libraries( encode_j2k ${LIBS} )