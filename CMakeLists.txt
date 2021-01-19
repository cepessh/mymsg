cmake_minimum_required(VERSION 3.10)

set( CMAKE_EXPORT_COMPILE_COMMANDS ON )
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

if (NOT build_suffix)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin.dbg)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin.rel)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/bin.relwithdbg)
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin.${build_suffix})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/bin.${build_suffix}.dbg)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/bin.${build_suffix}.rel)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/bin.${build_suffix}.relwithdbg)
endif()


PROJECT(mymsg)

add_subdirectory(nlohmann)

SET(THREADS_PREFER_PTHREAD_FLAG ON)
FIND_PACKAGE(Threads REQUIRED)
SET(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "/usr/local/boost_1_75_0") 
SET(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "/usr/local/boost_1_75_0/libs")
SET(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "/usr/local/mysql/connector-c++-/include")
SET(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "/usr/local/mysql/connector-c++-/lib64")
SET(Boost_USE_MULTITHREAD ON)
SET(Boost_USE_STATIC_LIBS OFF)

FIND_PACKAGE(OpenSSL 1.1.1 REQUIRED)
if( OpenSSL_FOUND)
  SET(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES} )
ENDIF(OpenSSL_FOUND)


find_package(Boost 1.70.0 REQUIRED COMPONENTS system)
IF(Boost_FOUND)
  INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF(Boost_FOUND)

INCLUDE_DIRECTORIES("/usr/local/mysql/connector-c++-/include")
LINK_DIRECTORIES("/usr/local/mysql/connector-c++-/lib64")

SET(SQLAPI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SQLAPI)
INCLUDE_DIRECTORIES(${SQLAPI_DIR}/include)
LINK_DIRECTORIES(${SQLAPI_DIR}/lib)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})

SET(USED_LIBS ${Boost_SYSTEM_LIBRARY}
              ${OPENSSL_SSL_LIBRARIES}
              ${OPENSSL_CRYPTO_LIBRARIES}
              ${SQLAPI_DIR}/lib/libsqlapi.so
              ${SQLAPI_DIR}/lib/libsqlapiu.so)

add_library(json INTERFACE)
add_library(spdlog INTERFACE)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE Threads::Threads ${USED_LIBS} dl)
target_link_libraries(server INTERFACE json spdlog)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE Threads::Threads ${USED_LIBS} dl)
target_link_libraries(client INTERFACE json spdlog)

