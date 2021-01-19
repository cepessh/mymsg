cmake_minimum_required(VERSION 3.7)
project(sqltest)
include_directories("${CMAKE_SOURCE_DIR}/../include")
# v1 >
#link_directories("${CMAKE_SOURCE_DIR}/../lib")
# < v1
# v2 >
add_library (sqlapi STATIC IMPORTED)
set_target_properties(sqlapi PROPERTIES IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/../lib/libsqlapi.a")
# < v2
add_executable(sqltest sqltest.cpp)
target_link_libraries(sqltest sqlapi dl pthread)
# v3 
#target_link_libraries(sqltest "${CMAKE_SOURCE_DIR}/../lib/libsqlapi.a" dl pthread)

