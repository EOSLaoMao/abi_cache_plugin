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
   void redis_set( const char* format, Args ... args ) {

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
         freeReplyObject(reply);
         EOS_THROW(chain::redis_exception, "redis reply error");
      }

      freeReplyObject(reply);
      return;
   }

   template< typename ... Args >
   fc::optional<std::vector<char>> redis_get( const char* format, Args ... args ) {

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
         freeReplyObject(reply);
         EOS_THROW(chain::redis_exception, "redis reply error");
      }

      if ( reply->type == REDIS_REPLY_STRING ) {
         std::vector<char> ret(reply->str, reply->str + reply->len);
         freeReplyObject(re);
         return ret;
      }

      return fc::optional<std::vector<char>>();
   }

private:
   redisContext* ctx;
   std::mutex mtx;
};

}
