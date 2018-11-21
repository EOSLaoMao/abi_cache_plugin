#pragma once

#include <mutex>
#include <hiredis/hiredis.h>

#include "exceptions.hpp"

namespace eosio {

class redis_client
{
public:
   redis_client(const std::string& ip, int32_t port) {
      ctx = redisConnect(ip.c_str(), port);
      if (ctx == NULL || ctx->err) {
         if (ctx) {
            elog("redis client construct error: ${e}", ("e", ctx->errstr));
         } else {
            elog("redis client construct error: Can't allocate redis context");
         }
         redisFree(ctx);
         EOS_THROW(chain::redis_exception, "redis client construct error");
      }
   }

   ~redis_client() {
      redisFree(ctx);
   }


   template< typename ... Args >
   bool redis_cmd(void* _dummy, const char* format, Args ... args ) {
      bool _dummy_val;
      return redis_cmd( _dummy_val, format, args ...  );
   }

   template< typename T, typename ... Args >
   bool redis_cmd( T& val, const char* format, Args ... args ) {

      void* re = nullptr;

      {
         std::unique_lock<std::mutex> lock(mtx);
         if (ctx->err) {
            elog("redis context error: ${e}", ("e", ctx->errstr));
            EOS_THROW(chain::redis_exception, "redis context error");
         }

         void* re = redisCommand( ctx, format, args ... );

         if (re == NULL || ctx->err) {
            elog("redis context error: ${e}", ("e", ctx->errstr));
            EOS_THROW(chain::redis_exception, "redis context error");
         }

      }

      auto reply = static_cast<redisReply*>(re);
      if ( reply->type == REDIS_REPLY_ERROR ) {
         elog("redis reply error: ${e}", ("e", reply->str));
         freeReplyObject(re);
         EOS_THROW(chain::redis_exception, "redis reply error");
      }

      return process_reply(val, re);
   }

   template< typename T >
   bool process_reply(T& val, void* re) {
      freeReplyObject(re);
      return true;
   }

private:
   redisContext* ctx;
   std::mutex mtx;
};

template< >
bool redis_client::process_reply(std::vector<char>& val, void* re) {

   auto reply = static_cast<redisReply*>(re);

   if ( reply->type == REDIS_REPLY_NIL ) {
      freeReplyObject(re);
      return false;
   } else if ( reply->type == REDIS_REPLY_STRING ) {
      val = std::vector<char>(reply->str, reply->str + reply->len);
      freeReplyObject(re);
      return true;
   } else {
      elog("redis reply not REDIS_REPLY_STRING");
      freeReplyObject(re);
      EOS_THROW(chain::redis_exception, "redis reply mismatch");
   }

   return false;
}

}
