#pragma once
#include <appbase/application.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp> 

namespace eosio {

using namespace appbase;

class abi_cache_plugin : public appbase::plugin<abi_cache_plugin> {
public:
   APPBASE_PLUGIN_REQUIRES((chain_plugin))
   abi_cache_plugin();
   virtual ~abi_cache_plugin();
 
   virtual void set_program_options(options_description&, options_description& cfg) override;
 
   void plugin_initialize(const variables_map& options);
   void plugin_startup();
   void plugin_shutdown();

   optional<abi_serializer> get_abi_serializer( const account_name &name, const fc::unsigned_int& abi_sequence );
   uint64_t global_sequence_height();

private:
   std::unique_ptr<class abi_cache_plugin_impl> my;
};

}
