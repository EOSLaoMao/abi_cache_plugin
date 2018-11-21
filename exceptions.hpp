#pragma once

#include <eosio/chain/exceptions.hpp>
#include <fc/exception/exception.hpp>
#include <appbase/application.hpp>
#include <boost/core/typeinfo.hpp>

namespace eosio {

namespace chain {

FC_DECLARE_DERIVED_EXCEPTION( abi_cache_plugin_exception,    chain_exception,
                              3230009, "abi_cache_plugin exception" )
   FC_DECLARE_DERIVED_EXCEPTION( redis_exception,   abi_cache_plugin_exception,
                                 3230010, "redis exception" )
} 

} // eosio::chain