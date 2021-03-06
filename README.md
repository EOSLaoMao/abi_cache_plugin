# abi_cache_plugin

Nodeos plugin for cache contract abi information.

This plugin will insert all abi into redis. Multiple abi of same account stored as a `<abi_sequence>-<abi_binary_data>` [Hash](https://redislabs.com/ebook/part-1-getting-started/chapter-1-getting-to-know-redis/1-2-what-redis-data-structures-look-like/1-2-4-hashes-in-redis/). Full archive of abi make it convenient to deserialize `action_trace` data, also make it possible to process blockchain history data in parallel.

## Install

### Install redis client `hiredis`

```bash
# e.g.
sudo apt install libhiredis-dev
```

### Embed `abi_cache_plugin` into `nodeos`

1. Get `abi_cache_plugin` source code.

```bash
git clone https://github.com/EOSLaoMao/abi_cache_plugin.git plugins/abi_cache_plugin
cd plugins/abi_cache_plugin
git submodule update --init --recursive
```

2. Add subdirectory to `plugins/CMakeLists.txt`.

```cmake
...
add_subdirectory(mongo_db_plugin)
add_subdirectory(login_plugin)
add_subdirectory(login_plugin)
add_subdirectory(abi_cache_plugin) # add this line.
...
```

3. Add following line to `programs/nodeos/CMakeLists.txt`.

```cmake
target_link_libraries( ${NODE_EXECUTABLE_NAME}
        PRIVATE appbase
        PRIVATE -Wl,${whole_archive_flag} login_plugin               -Wl,${no_whole_archive_flag}
        PRIVATE -Wl,${whole_archive_flag} history_plugin             -Wl,${no_whole_archive_flag}
        ...
        # add this line.
        PRIVATE -Wl,${whole_archive_flag} abi_cache_plugin           -Wl,${no_whole_archive_flag}
        ...
```

### Usage

The plugin will cache the contract abi both in nodeos process and redis. If redis host is not specified then redis cache is disabled.

```text
--abi-cache-thread-pool-size arg (=4) The size of the data processing thread pool.
--abi-cache-redis-host arg            Redis host, If not specified then Redis cache is disabled.
--abi-cache-redis-port arg (=6379)    Redis port.
```
