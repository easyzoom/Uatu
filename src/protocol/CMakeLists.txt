# Try system/vcpkg install first, fall back to FetchContent
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
    find_package(nlohmann_json 3 QUIET)
endif()
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(uatu_protocol
    json_codec.cpp
)
target_link_libraries(uatu_protocol
    PUBLIC  uatu_includes
    PRIVATE nlohmann_json::nlohmann_json
)
