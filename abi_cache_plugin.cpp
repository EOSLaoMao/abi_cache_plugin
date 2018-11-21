#include <eosio/abi_cache_plugin/abi_cache_plugin.hpp>
#include <eosio/chain/eosio_contract.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/types.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/signals2/connection.hpp>


#include <stack>
#include <utility>
#include <functional>

#include "ThreadPool/ThreadPool.h"
#include "exceptions.hpp"
#include "redis_client.hpp"
#include "abi_serializer_cache.hpp"

namespace eosio {

using chain::account_name;
using chain::action_name;
using chain::block_id_type;
using chain::permission_name;
using chain::transaction;
using chain::signed_transaction;
using chain::signed_block;
using chain::transaction_id_type;
using chain::packed_transaction;

static appbase::abstract_plugin& _abi_cache_plugin = app().register_plugin<abi_cache_plugin>();

class abi_cache_plugin_impl {
public:

   abi_cache_plugin_impl();
   ~abi_cache_plugin_impl();

   fc::optional<boost::signals2::scoped_connection> applied_transaction_connection;

   uint64_t global_sequence_height() { return min_sequence_heights; }
   void check_task_queue_size();

   void process_applied_transaction( chain::transaction_trace_ptr );
   void _process_applied_transaction( chain::transaction_trace_ptr, size_t thread_idx );

   optional<abi_serializer> get_abi_serializer( const account_name &name,  fc::unsigned_int& abi_sequence );

   std::unique_ptr<ThreadPoolA> thread_pool;
   size_t max_task_queue_size = 0;
   int queue_sleep_time = 0;

   bool redis_enabled = false;
   std::vector<std::unique_ptr<redis_client>> r_clients;

   abi_serializer_cache abi_cache;

   std::vector<uint64_t> sequence_heights;
   uint64_t min_sequence_heights;

   static const action_name setabi;
};

const action_name abi_cache_plugin_impl::setabi = chain::setabi::get_name();

abi_cache_plugin_impl::abi_cache_plugin_impl() {}

abi_cache_plugin_impl::~abi_cache_plugin_impl() {}

namespace {

void handle_exception(int line) {
   bool shutdown = true;

   try {
      throw;
   } catch (chain::abi_cache_plugin_exception& e) {
      elog("abi_cache_plugin Exception, line: ${l}, ${e}", ("l", line)("e", e.to_detail_string()));
   } catch (fc::exception& e) {
      elog("abi_cache_plugin FC Exception, line: ${l}, ${e}", ("l", line)("e", e.to_detail_string()));
   } catch (std::exception& e) {
      elog("abi_cache_plugin STD Exception, line: ${l}, ${e}", ("l", line)("e", e.what()));
   } catch (...) {
      elog("abi_cache_plugin Unknown exception");
   }

   if( shutdown ) {
      appbase::app().quit();
   }
}

}

void abi_cache_plugin_impl::process_applied_transaction( chain::transaction_trace_ptr t ) {
   check_task_queue_size();
   thread_pool->enqueue(
      [ t, this ](size_t thread_idx)
      {
         try {
            _process_applied_transaction( t, thread_idx );
         } catch ( ... ) {
            handle_exception(__LINE__);
         }
      }
   );

   // update processed action trace global sequence height
   uint64_t smallest = std::numeric_limits<uint64_t>().max();
   for ( auto v : sequence_heights) {
      smallest = std::min(smallest, v);
   }
   min_sequence_heights = smallest;

   if (redis_enabled) {
      // update global sequence height in redis
      thread_pool->enqueue(
         [ smallest, this ](size_t thread_idx)
         {
            try {
               r_clients[thread_idx]->redis_cmd(nullptr, "SET globalSeqHeight %s", std::to_string(smallest).c_str());
            } catch ( ... ) {
               handle_exception(__LINE__);
            }
         }
      );
   }

}

void abi_cache_plugin_impl::_process_applied_transaction( chain::transaction_trace_ptr t, size_t thread_idx ) {

   bool executed = t->receipt.valid() && t->receipt->status == chain::transaction_receipt_header::executed;
   uint64_t global_sequence;

   std::stack<std::reference_wrapper<chain::action_trace>> stack;
   for( auto& atrace : t->action_traces ) {
      stack.emplace(atrace);

      while ( !stack.empty() )
      {
         auto &atrace = stack.top().get();
         stack.pop();

         global_sequence = atrace.receipt.global_sequence;

         if( executed && atrace.receipt.receiver == chain::config::system_account_name && atrace.act.name == setabi ) {
            auto setabi = atrace.act.data_as<chain::setabi>();
            abi_cache.insert( setabi.account, atrace.receipt.abi_sequence, setabi.abi );

            if (redis_enabled) {
               // insert abi binary into redis
               r_clients[thread_idx]->redis_cmd(
                  nullptr, "HSET %s %s %b",
                  std::to_string(setabi.account.value).c_str(),
                  std::to_string(atrace.receipt.abi_sequence.value).c_str(),
                  reinterpret_cast<const char*>(setabi.abi.data()), setabi.abi.size());
            }
         }

         auto &inline_traces = atrace.inline_traces;
         for( auto it = inline_traces.rbegin(); it != inline_traces.rend(); ++it ) {
            stack.emplace(*it);
         }
      }
   }

   sequence_heights[thread_idx] = global_sequence;
}

optional<abi_serializer> abi_cache_plugin_impl::get_abi_serializer( const account_name &name, fc::unsigned_int& abi_sequence ) {
   try {
      auto res = abi_cache.find( name, abi_sequence );

      if ( res.valid() ) {
         return res;
      }
      
      if (redis_enabled) {
         chain::bytes abi;
         bool not_nil = false;

         size_t idx = name.value % r_clients.size();
         not_nil = r_clients[idx]->redis_cmd(
            abi, "HGET %s %s",
            std::to_string(name.value).c_str(),
            std::to_string(abi_sequence.value).c_str());

         if (not_nil) {
            abi_serializer abis;
            abi_cache.abi_to_serializer(abis, name, abi);
            abi_cache.insert(name, abi_sequence, abis);
            return abis;
         }
      }
   } FC_CAPTURE_AND_LOG((name))

   return optional<abi_serializer>();

}

void abi_cache_plugin_impl::check_task_queue_size() {
   auto task_queue_size = thread_pool->queue_size();
   if ( task_queue_size > max_task_queue_size ) {
      queue_sleep_time += 5;
      if( queue_sleep_time > 1000 )
         wlog("thread pool task queue size: ${q}", ("q", task_queue_size));
      boost::this_thread::sleep_for( boost::chrono::milliseconds( queue_sleep_time ));
   } else {
      queue_sleep_time -= 5;
      if( queue_sleep_time < 0 ) queue_sleep_time = 0;
   }
}

abi_cache_plugin::abi_cache_plugin():my(new abi_cache_plugin_impl()){}
abi_cache_plugin::~abi_cache_plugin(){}

void abi_cache_plugin::set_program_options(options_description&, options_description& cfg) {
   cfg.add_options()
      ("abi-cache-thread-pool-size", bpo::value<size_t>()->default_value(4),
      "The size of the data processing thread pool.")
      ("abi-cache-redis-ip", bpo::value<std::string>(),
      "Redis IP connection string If not specified then Redis cache is disabled.")
      ("abi-cache-redis-port", bpo::value<int>()->default_value(6379),
      "Redis port.");
}

void abi_cache_plugin::plugin_initialize(const variables_map& options) {
   try {
      ilog( "initializing abi_cache_plugin" );

      if( options.count( "abi-cache-task-queue-size" )) {
         my->max_task_queue_size = options.at( "abi-cache-task-queue-size" ).as<uint32_t>();
      }

      // hook up to signals on controller
      chain_plugin* chain_plug = app().find_plugin<chain_plugin>();
      EOS_ASSERT( chain_plug, chain::missing_chain_plugin_exception, "" );
      auto& chain = chain_plug->chain();

      if( options.count( "abi-serializer-max-time-ms" )) {
         uint32_t max_time = options.at( "abi-serializer-max-time-ms" ).as<uint32_t>();
         EOS_ASSERT(max_time > chain::config::default_abi_serializer_max_time_ms,
                     chain::plugin_config_exception, "--abi-serializer-max-time-ms required as default value not appropriate for parsing full blocks");
         my->abi_cache.abi_serializer_max_time = app().get_plugin<chain_plugin>().get_abi_serializer_max_time();
      }

      size_t thr_pool_size = options.at( "abi-cache-thread-pool-size" ).as<size_t>();

      for (int i = 0; i < thr_pool_size; i++) {
         my->sequence_heights.emplace_back(0);
      }

      if( options.count( "abi-cache-redis-ip" )) {
         my->redis_enabled = true;
         std::string redis_ip = options.at( "abi-cache-redis-ip" ).as<std::string>();
         int redis_port = options.at( "abi-cache-redis-port" ).as<int>();

         ilog("Redis connection, ${i}:${p}", ("i", redis_ip)("p", redis_port));
         for (int i = 0; i < thr_pool_size; i++) {
            my->r_clients.emplace_back( new redis_client(redis_ip.c_str(), redis_port) );
         }

      } else {
         wlog("Redis cache disabled");
      }

      ilog("init thread pool, size: ${tps}", ("tps", thr_pool_size));
      my->thread_pool.reset( new ThreadPoolA(thr_pool_size) );

      my->max_task_queue_size = thr_pool_size * 8192;

      my->applied_transaction_connection.emplace(
         chain.applied_transaction.connect( [&]( const chain::transaction_trace_ptr& t ) {
            my->process_applied_transaction( t );
         } ));

   }
   FC_LOG_AND_RETHROW()
}

optional<abi_serializer> abi_cache_plugin::get_abi_serializer( const account_name &name, fc::unsigned_int& abi_sequence ) {
   return my->get_abi_serializer( name, abi_sequence );
}

uint64_t abi_cache_plugin::global_sequence_height() {
   return my->min_sequence_heights;
}


void abi_cache_plugin::plugin_startup() {}

void abi_cache_plugin::plugin_shutdown() {
   my->applied_transaction_connection.reset();

   my.reset();
}


}
