#pragma once
#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/functional/hash.hpp>

#include <unordered_map>

#include "exceptions.hpp"

// account & abi_sequence pair as key
typedef std::pair<uint64_t, uint32_t> key_pair;

namespace eosio {

class abi_serializer_cache
{
public:
   void abi_to_serializer(abi_serializer& abis, const account_name &name, const chain::bytes& abi_bytes);
   void insert( const account_name &name, fc::unsigned_int& abi_sequence, const chain::bytes& abi);
   void insert( const account_name &name, fc::unsigned_int& abi_sequence, const abi_serializer& abis);
   optional<abi_serializer> find( const account_name &name,  fc::unsigned_int& abi_sequence);
   fc::microseconds abi_serializer_max_time;

private:
   // use account & abi_sequence pair as cache map key
   std::unordered_map< key_pair, fc::optional<abi_serializer>, boost::hash<key_pair> > abi_cache_map;

   boost::shared_mutex cache_mtx;
};


void abi_serializer_cache::abi_to_serializer(abi_serializer& abis, const account_name &name, const chain::bytes& abi_bytes) {
   abi_def abi = fc::raw::unpack<chain::abi_def>( abi_bytes );

   if( name == chain::config::system_account_name ) {
      // redefine eosio setabi.abi from bytes to abi_def
      // Done so that abi is stored as abi_def instead of as bytes
      auto itr = std::find_if( abi.structs.begin(), abi.structs.end(),
                                 []( const auto& s ) { return s.name == "setabi"; } );
      if( itr != abi.structs.end() ) {
         auto itr2 = std::find_if( itr->fields.begin(), itr->fields.end(),
                                    []( const auto& f ) { return f.name == "abi"; } );
         if( itr2 != itr->fields.end() ) {
            if( itr2->type == "bytes" ) {
               itr2->type = "abi_def";
               // unpack setabi.abi as abi_def instead of as bytes
               abis.add_specialized_unpack_pack( "abi_def",
                     std::make_pair<abi_serializer::unpack_function, abi_serializer::pack_function>(
                           []( fc::datastream<const char*>& stream, bool is_array, bool is_optional ) -> fc::variant {
                              EOS_ASSERT( !is_array && !is_optional, chain::abi_cache_plugin_exception, "unexpected abi_def");
                              chain::bytes temp;
                              fc::raw::unpack( stream, temp );
                              return fc::variant( fc::raw::unpack<abi_def>( temp ) );
                           },
                           []( const fc::variant& var, fc::datastream<char*>& ds, bool is_array, bool is_optional ) {
                              EOS_ASSERT( false, chain::abi_cache_plugin_exception, "never called" );
                           }
                     ) );
            }
         }
      }
   }

   abis.set_abi( abi, abi_serializer_max_time );
}


void abi_serializer_cache::insert( const account_name &name, fc::unsigned_int& abi_sequence,
                                   const abi_serializer& abis ) {
   if( name.good()) {
      try {
         boost::unique_lock<boost::shared_mutex> lock(cache_mtx);
         abi_cache_map.emplace( std::make_pair(name.value, abi_sequence.value), abis);

      } FC_CAPTURE_AND_LOG((name))
   }
}

void abi_serializer_cache::insert( const account_name &name, fc::unsigned_int& abi_sequence,
                                   const chain::bytes& abi_bytes ) {
   try {
      abi_serializer abis;
      abi_to_serializer(abis, name, abi_bytes);

      insert(name, abi_sequence, abis);

   } FC_CAPTURE_AND_LOG((name))
}

optional<abi_serializer> abi_serializer_cache::find( const account_name &name, fc::unsigned_int& abi_sequence ) {
   if( name.good()) {
      try {
         boost::upgrade_lock< boost::shared_mutex > lock(cache_mtx);
         auto itr = abi_cache_map.find( std::make_pair(name.value, abi_sequence.value) );
         if( itr != abi_cache_map.end() ) {
            return itr->second;
         }
      } FC_CAPTURE_AND_LOG((name))
   }
   return optional<abi_serializer>();
}

}
