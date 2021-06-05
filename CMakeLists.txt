cmake_minimum_required (VERSION 3.12)

set(This chatterfinity)
project(${This})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(headers)
set(sources)

list(APPEND headers
	"Connection.hpp"
	"Request.hpp"
	"Response.hpp"
	"Utility.hpp"
	"Logger.hpp"
)
	
list(APPEND sources 
	"main.cpp"
	"Request.cpp"
	"Response.cpp"
	"Connection.cpp"
)

add_executable(${This} ${sources} ${headers})

add_definitions(
	-DBOOST_DATE_TIME_NO_LIB 
	-DBOOST_REGEX_NO_LIB 
	-D_WIN32_WINNT=0x0601
	-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
	-D_SILENCE_CXX17_ADAPTOR_TYPEDEFS_DEPRECATION_WARNING
	-DRAPIDJSON_NOMEMBERITERATORCLASS
)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)

message(STATUS "OPENSSL_VERSION ${OPENSSL_VERSION}")

# add rapidjson to dependency
include("${CMAKE_SOURCE_DIR}/vendor/rapidjson.cmake")
add_dependencies(${This} rapidjson)

target_include_directories(${This} 
	PUBLIC "include"
	PUBLIC ${RAPIDJSON_INCLUDE_DIR}
	PUBLIC ${Boost_INCLUDE_DIRS} 
)

target_link_libraries(${This}
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
)

target_compile_options(${This} PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:Clang>:-Wall -Werror -Wextra>>
  $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU>:-Wall -Werror -Wextra>>
  $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:MSVC>:/W3>>
)

# copy *.pem to folder with binary
set(Configs
  "DigiCertHighAssuranceEVRootCA.crt.pem"
)

foreach(config ${Configs})
  message(STATUS "\t\tTrying to copy to */${config}")
  # Copy files for ctests
  # configure_file("${config}" "${config}" COPYONLY)
  # Copy files for Debug
  configure_file("${config}" "Debug/${config}" COPYONLY)
  # Copy files for Release
  configure_file("${config}" "Release/${config}" COPYONLY)
endforeach()