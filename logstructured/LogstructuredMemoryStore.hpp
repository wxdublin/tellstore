/*
 * (C) Copyright 2015 ETH Zurich Systems Group (http://www.systems.ethz.ch/) and others.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contributors:
 *     Markus Pilman <mpilman@inf.ethz.ch>
 *     Simon Loesing <sloesing@inf.ethz.ch>
 *     Thomas Etter <etterth@gmail.com>
 *     Kevin Bocksrocker <kevin.bocksrocker@gmail.com>
 *     Lucas Braun <braunl@inf.ethz.ch>
 */

#pragma once

#include "Table.hpp"

#include <config.h>
#include <util/Allocator.hpp>
#include <util/PageManager.hpp>
#include <util/TableManager.hpp>
#include <util/VersionManager.hpp>

#include <crossbow/non_copyable.hpp>
#include <crossbow/string.hpp>

#include <cstdint>

namespace tell {
namespace commitmanager {
class SnapshotDescriptor;
} // namespace commitmanager

namespace store {

class ScanQuery;

/**
 * @brief A Storage implementation using a Log-Structured Memory approach as its data store
 */
struct LogstructuredMemoryStore : crossbow::non_copyable, crossbow::non_movable {
public:
    using Table = logstructured::Table;
    using GC = Table::GarbageCollector;

    static const char* implementationName() {
        return "Log-Structured Memory";
    }

    LogstructuredMemoryStore(const StorageConfig& config)
            : mPageManager(config.totalMemory),
              mGc(*this),
              mTableManager(*this, mMemoryManager, mPageManager, mVersionManager, mGc, config),
              mHashMap(config.hashMapCapacity) {
    }

    std::unique_ptr<MemoryConsumer> createMemoryConsumer() {
        return mMemoryManager.createConsumer();
    }

    bool createTable(const crossbow::string& name, const Schema& schema, uint64_t& idx) {
        return mTableManager.createTable(name, schema, idx, mVersionManager, mHashMap);
    }

    std::vector<const Table*> getTables() const {
        return mTableManager.getTables();
    }

    const Table* getTable(uint64_t id) const {
        return mTableManager.getTable(id);
    }

    const Table* getTable(const crossbow::string& name, uint64_t& id) const {
        return mTableManager.getTable(name, id);
    }

    template <typename Fun>
    int get(uint64_t tableId, uint64_t key, const commitmanager::SnapshotDescriptor& snapshot, Fun fun) {
        return mTableManager.get(tableId, key, snapshot, std::move(fun));
    }

    int update(uint64_t tableId, uint64_t key, size_t size, const char* data,
            const commitmanager::SnapshotDescriptor& snapshot) {
        return mTableManager.update(tableId, key, size, data, snapshot);
    }

    int insert(uint64_t tableId, uint64_t key, size_t size, const char* data,
            const commitmanager::SnapshotDescriptor& snapshot) {
        return mTableManager.insert(tableId, key, size, data, snapshot);
    }

    int remove(uint64_t tableId, uint64_t key, const commitmanager::SnapshotDescriptor& snapshot) {
        return mTableManager.remove(tableId, key, snapshot);
    }

    int revert(uint64_t tableId, uint64_t key, const commitmanager::SnapshotDescriptor& snapshot) {
        return mTableManager.revert(tableId, key, snapshot);
    }

    int scan(uint64_t tableId, ScanQuery* query) {
        return mTableManager.scan(tableId, query);
    }

    /**
     * We use this method mostly for test purposes. But
     * it might be handy in the future as well. If possible,
     * this should be implemented in an efficient way.
     */
    void forceGC() {
        mTableManager.forceGC();
    }

private:
    MemoryReclaimer mMemoryManager;
    PageManager mPageManager;
    GC mGc;
    VersionManager mVersionManager;
    TableManager<LogstructuredMemoryStore> mTableManager;

    Table::HashTable mHashMap;
};

} // namespace store
} // namespace tell
