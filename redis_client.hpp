#pragma once

#include <mutex>
#include <stdio.h>
#include <hiredis/hiredis.h>
#include <boost/filesystem.hpp>
#include "exceptions.hpp"

namespace eosio {

class redis_client
{
public:
   redis_client(const std::string& host, int32_t port, const boost::filesystem::path& filename):ofs(filename)
   {
      ctx = redisConnect(host.c_str(), port);
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
   void write_to_file( const char* format, Args ... args ) {
      char buf[65536];
      int len = snprintf( buf, 65536, format, args ... );

      if (len < 0 ) {
         elog("dump redis command to file error");
         return;
      }

      if (len == 65536 ) {
         elog("dump redis command over 65536 bytes to file");
         return;
      }

      if (len >= 0 && len < 65536) {
         ilog("dump redis command to file");
         std::string cmd(buf, len);
         ofs << cmd;
         ofs << "\n-----------------------\n";
      }
   }

   template< typename ... Args >
   void redis_set( const char* format, Args ... args ) {

      void* re = nullptr;
      
      {
         std::unique_lock<std::mutex> lock(mtx);

         if (dump) {
            write_to_file( format, args ... );
            return;
         }

         try {
            if (ctx->err) {
               elog("redis context error: ${e}", ("e", ctx->errstr));
               EOS_THROW(chain::redis_exception, "redis context error");
            }

            re = redisCommand( ctx, format, args ... );

            if (re == NULL || ctx->err) {
               elog("redis context error: ${e}", ("e", ctx->errstr));
               EOS_THROW(chain::redis_exception, "redis context error");
            }

         } catch ( ... ) {
            dump = true;
            write_to_file( format, args ... );
            throw;
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

         re = redisCommand( ctx, format, args ... );

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
   boost::filesystem::ofstream ofs;
   bool dump = false;
};

}
