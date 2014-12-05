#include <util/Record.hpp>
#include <util/LogOperations.hpp>
#include <ForceFeedback/ForceFeedback.h>
#include "dmrewrite.hpp"

namespace tell {
namespace store {
namespace dmrewrite {


DMRecord::DMRecord(const Schema& schema)
    : record(schema) {
}

const char* DMRecord::getRecordData(const SnapshotDescriptor& snapshot,
                                    const char* data,
                                    bool& isNewest,
                                    std::atomic<LogEntry*>** next /*= nullptr*/) {
    const char* res = multiVersionRecord.getRecord(snapshot, data + 8, isNewest);
    if (next) {
        *next = reinterpret_cast<std::atomic<LogEntry*>*>(const_cast<char*>(data));
    }
    if (isNewest) {
        // if the newest record is valid in the given snapshot, we need to check whether
        // there are newer versions on the log. Otherwise we are fine (even if there are
        // newer versions, we won't care).
        const LogEntry* loggedOperation = (*reinterpret_cast<const std::atomic<LogEntry*>*>(data)).load();
        while (loggedOperation) {
            // there are newer versions
            auto version = LoggedOperation::getVersion(loggedOperation->data());
            if (snapshot.inReadSet(version)) {
                return LoggedOperation::getRecord(loggedOperation->data());
            }
            // if we reach this code, we did not return the newest version
            isNewest = false;
            loggedOperation = LoggedOperation::getPrevious(loggedOperation->data());
        }
    }
    return res;
}

Table::Table(PageManager& pageManager, Schema const& schema)
    : mPageManager(pageManager),
      mSchema(schema),
      mRecord(schema),
      mLog(mPageManager),
      mInsertLog(mPageManager),
      mHashMap(new(allocator::malloc(sizeof(CuckooTable))) CuckooTable(mPageManager)) {
}

void GarbageCollector::run(const std::vector<Table*>& tables) {
}

void Table::insert(uint64_t key, const char* const data, const SnapshotDescriptor& descr,
                   bool* succeeded /*=nullptr*/) {
    // We just need to append to the log
    auto logEn = mInsertLog.tail();
    if (mHashMap.load()->get(key) != nullptr) {
        if (succeeded != nullptr)
            *succeeded = false;
        return;
    }
    // insertion works like follows:
    // - First we append the insertion to the log
    // - Then we check, whether this tuple has another entry in the log
    // - The insertion will still be in the log, but it will be ignored by the merger
    // By doing so, we can guarantee, that the key will be unique
    LoggedOperation op;
    op.key = key;
    op.operation = LogOperation::INSERT;
    op.tuple = data;
    auto nEntry = mInsertLog.append(uint32_t(op.serializedSize()));
    op.serialize(nEntry->data());
    nEntry->seal();
    if (succeeded != nullptr) {
        auto iter = mInsertLog.tail();
        while (iter != nEntry) {
            while (!iter->sealed()) {
            }
            if (key == *reinterpret_cast<const uint64_t*>(iter->data() + 4)) {
                *succeeded = false;
                return;
            }
            iter = iter->next();
        }
        *succeeded = true;
    }
    return;
}

bool Table::get(uint64_t key, const char*& data, const SnapshotDescriptor& desc, bool& isNewest) {
    auto& hMap = *mHashMap.load();
    auto addr = hMap.get(key);
    if (addr) {
        // found the value
        data = mRecord.getRecordData(desc, reinterpret_cast<const char*>(addr), isNewest);
        return true;
    }
    // The record is not in the in the hash map - therefore we need to look for it
    // in the insert log
    auto iterator = mInsertLog.tail();
    while (iterator->sealed() && iterator->size > 0) {
        if (LoggedOperation::getKey(iterator->data()) == key) {
            auto opData = iterator->data();
            isNewest = true;
            if (!desc.inReadSet(LoggedOperation::getVersion(opData))) {
                return false;
            }
            data = LoggedOperation::getRecord(opData);
            auto newest = LoggedOperation::getNewest(opData);
            const LogEntry* next = newest->load();
            while (next) {
                if (desc.inReadSet(LoggedOperation::getVersion(next->data()))) {
                    return true;
                }
                isNewest = false;
                next = LoggedOperation::getPrevious(next->data());
            }
            return true;
        }
        iterator = iterator->next();
    }
    return false;
}

bool Table::update(uint64_t key, const char* data, const SnapshotDescriptor& snapshot) {
    auto& hMap = *mHashMap.load();
    auto addr = reinterpret_cast<char*>(hMap.get(key));
    LoggedOperation loggedOperation;
    loggedOperation.operation = LogOperation::UPDATE;
    loggedOperation.key = key;
    loggedOperation.version = snapshot.version;
    if (addr) {
        // found it in the hash table
        std::atomic<LogEntry*>* newestPtr;
        bool isNewest;
        auto rec = mRecord.getRecordData(snapshot, addr, isNewest, &newestPtr);
        if (!isNewest) {
            // We got a conflict
            return false;
        }
        auto newestOp = newestPtr->load();
        if (newestOp != nullptr && newestOp != LoggedOperation::loggedOperationFromTuple(rec)) {
            // Someone did an update in the mean time
            return false;
        }
        loggedOperation.previous = newestPtr->load();
        auto logEntry = mLog.append(uint32_t(loggedOperation.serializedSize()));
        loggedOperation.serialize(logEntry->data());
        logEntry->seal();
        return newestPtr->compare_exchange_strong(newestOp, logEntry);
    }
    auto iterator = mInsertLog.tail();
    while (iterator->sealed() && iterator->size > 0) {
        if (LoggedOperation::getKey(iterator->data()) == key) {
            const char* opData = iterator->data();
            //isNewest = true;
            if (!snapshot.inReadSet(LoggedOperation::getVersion(opData))) {
                return false;
            }
            auto newest = LoggedOperation::getNewest(opData);
            auto newestPtr = newest->load();
            if (newestPtr) {
                if (!snapshot.inReadSet(LoggedOperation::getVersion(newestPtr->data()))) {
                    // The newest version is not in the read set
                    return false;
                }
            }
            // we found the newest record and it is writable
            loggedOperation.previous = newestPtr;
            auto logEntry = mLog.append(uint32_t(loggedOperation.serializedSize()));
            loggedOperation.serialize(logEntry->data());
            logEntry->seal();
            return newest->compare_exchange_strong(newestPtr, logEntry);
        }
        iterator = iterator->next();
    }
    return false;
}

bool Table::remove(uint64_t key, const SnapshotDescriptor& snapshot) {

    return false;
}
} // namespace tell
StoreImpl<Implementation::DELTA_MAIN_REWRITE>::StoreImpl(const StorageConfig& config)
    : pageManager(config.totalMemory), tableManager(config, gc) {
}

StoreImpl<Implementation::DELTA_MAIN_REWRITE>::StoreImpl(const StorageConfig& config, size_t totalMem)
    : pageManager(totalMem), tableManager(config, gc) {
}

} // namespace store
} // namespace dmrewrite