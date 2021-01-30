cmake_minimum_required(VERSION 3.10)

set( CMAKE_EXPORT_COMPILE_COMMANDS ON )
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "cmake")

if (NOT build_suffix)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
else()
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin.${build_suffix})
endif()


project(mymsg)

# Threads
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Boost libraries
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

SET(CMAKE_CXX_COMPILER g++-10)
ADD_DEFINITIONS(-DNDEBUG)
ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)
ADD_DEFINITIONS(-DBOOST_USE_ASAN)
ADD_DEFINITIONS(-DBOOST_USE_VALGRIND)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic ") # -Wconversion
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 ") # -fcoroutines ")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-unused")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3 -pthread -march=native")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -fno-omit-frame-pointer -O0 -pthread -march=x86-64")

add_executable(server server.cpp)
target_link_libraries(server PRIVATE Threads::Threads ${USED_LIBS} dl)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE Threads::Threads ${USED_LIBS} dl)

ADD_CUSTOM_COMMAND(OUTPUT .stamp
    COMMAND run-clang-tidy-9 -fix -header-filter='.*' -checks='-*,readability-*,modernize-*,-modernize-use-trailing-return-type'
)
ADD_CUSTOM_TARGET(tidy DEPENDS .stamp)


