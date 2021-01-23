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


project(mymsg)

# Threads
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Boost libraries
set(CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} "/usr/local/boost_1_75_0") 
set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} "/usr/local/boost_1_75_0/libs")
set(Boost_USE_MULTITHREAD ON)
set(Boost_USE_STATIC_LIBS OFF)

find_package(Boost 1.70.0 REQUIRED COMPONENTS system)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
  link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

# OpenSSL
find_package(OpenSSL 1.1.1 REQUIRED)
if(OpenSSL_FOUND)
  set(OPENSSL_LIBRARIES ${OPENSSL_SSL_LIBRARIES} ${OPENSSL_CRYPTO_LIBRARIES} )
endif(OpenSSL_FOUND)

# SQLAPI++
set(SQLAPI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/SQLAPI)
include_directories(${SQLAPI_DIR}/include)
link_directories(${SQLAPI_DIR}/lib)

# Include to use nlohmann/json and spdlog in tree root
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(USED_LIBS ${Boost_SYSTEM_LIBRARY}
              ${OPENSSL_SSL_LIBRARIES}
              ${OPENSSL_CRYPTO_LIBRARIES}
              ${SQLAPI_DIR}/lib/libsqlapi.so
              ${SQLAPI_DIR}/lib/libsqlapiu.so)


add_executable(server server.cpp)
target_link_libraries(server PRIVATE Threads::Threads ${USED_LIBS} dl)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE Threads::Threads ${USED_LIBS} dl)

