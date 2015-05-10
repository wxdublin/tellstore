#pragma once

#include "ServerConfig.hpp"

#include <tellstore.hpp>

#include <crossbow/infinio/InfinibandService.hpp>
#include <crossbow/infinio/InfinibandSocket.hpp>

#include <tbb/queuing_rw_mutex.h>
#include <tbb/concurrent_unordered_set.h>

#include <boost/system/error_code.hpp>

namespace crossbow {
namespace infinio {
class EventDispatcher;
} // namespace infinio
} // namespace crossbow

namespace tell {
namespace store {

class ClientConnection;

class ConnectionManager : private crossbow::infinio::InfinibandSocketHandler {
public:
    ConnectionManager(Storage& storage, crossbow::infinio::EventDispatcher& dispatcher, const ServerConfig& config)
            : mConfig(config),
              mStorage(storage),
              mService(dispatcher),
              mAcceptor(mService) {
    }

    void init(boost::system::error_code& ec);

    void shutdown();


private:
    friend class ClientConnection;

    virtual bool onConnection(crossbow::infinio::InfinibandSocket socket) final override;

    void removeConnection(ClientConnection* con);

    ServerConfig mConfig;

    Storage& mStorage;

    crossbow::infinio::InfinibandService mService;
    crossbow::infinio::InfinibandAcceptor mAcceptor;

    tbb::queuing_rw_mutex mConnectionsMutex;
    tbb::concurrent_unordered_set<ClientConnection*> mConnections;
};

} // namespace store
} // namespace tell