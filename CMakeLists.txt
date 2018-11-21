file(GLOB HEADERS "include/eosio/abi_cache_plugin/*.hpp")
add_library( abi_cache_plugin
             abi_cache_plugin.cpp
             ${HEADERS} )

target_link_libraries( abi_cache_plugin appbase chain_plugin eosio_chain fc hiredis)
target_include_directories( abi_cache_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )
