/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvAccessCPP is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#ifndef SERVERCONTEXT_H_
#define SERVERCONTEXT_H_

#include <pv/remote.h>
#include <pv/beaconServerStatusProvider.h>
#include <pv/caConstants.h>
#include <pv/pvVersion.h>
#include <pv/pvAccess.h>
#include <pv/blockingUDP.h>
#include <pv/blockingTCP.h>
#include <pv/beaconEmitter.h>
#include <pv/logger.h>

namespace epics {
namespace pvAccess {

/**
 * The class representing a CA Server context.
 */
class ServerContext
{
public:
    typedef std::tr1::shared_ptr<ServerContext> shared_pointer;
    typedef std::tr1::shared_ptr<const ServerContext> const_shared_pointer;
	
    /**
	 * Destructor
	 */
	virtual ~ServerContext() {};
	/**
	 * Get context implementation version.
	 * @return version of the context implementation.
	 */
	virtual const Version& getVersion() = 0;

	/**
	 * Set <code>ChannelAccess</code> implementation and initialize server.
	 * @param channelAccess implementation of channel access to be served.
	 */
	virtual void initialize(ChannelAccess::shared_pointer const & channelAccess) = 0;

	/**
	 * Run server (process events).
	 * @param	seconds	time in seconds the server will process events (method will block), if <code>0</code>
	 * 				the method would block until <code>destroy()</code> is called.
	 * @throws BaseException if server is already destroyed.
	 */
	virtual void run(epics::pvData::int32 seconds) = 0;

	/**
	 * Shutdown (stop executing run() method) of this context.
	 * After shutdown Context cannot be rerun again, destroy() has to be called to clear all used resources.
	 * @throws BaseException if the context has been destroyed.
	 */
	virtual void shutdown() = 0;

	/**
	 * Clear all resources attached to this context.
	 * @throws BaseException if the context has been destroyed.
	 */
	virtual void destroy() = 0;

	/**
	 * Prints detailed information about the context to the standard output stream.
	 */
	virtual void printInfo() = 0;

	/**
	 * Prints detailed information about the context to the specified output stream.
	 * @param str stream to which to print the info
	 */
	virtual void printInfo(std::ostream& str) = 0;

	/**
	 * Dispose (destroy) server context.
	 * This calls <code>destroy()</code> and silently handles all exceptions.
	 */
	virtual void dispose() = 0;

	// ************************************************************************** //
	// **************************** [ Plugins ] ********************************* //
	// ************************************************************************** //

	/**
	 * Set beacon server status provider.
	 * @param beaconServerStatusProvider <code>BeaconServerStatusProvider</code> implementation to set.
	 */
	virtual void setBeaconServerStatusProvider(BeaconServerStatusProvider::shared_pointer const & beaconServerStatusProvider) = 0;

};


class ServerContextImpl :
    public ServerContext,
    public Context,
    public ResponseHandlerFactory,
    public std::tr1::enable_shared_from_this<ServerContextImpl>
{
public:
    typedef std::tr1::shared_ptr<ServerContextImpl> shared_pointer;
    typedef std::tr1::shared_ptr<const ServerContextImpl> const_shared_pointer;
protected:
	ServerContextImpl();
public:
    static ServerContextImpl::shared_pointer create();
    
	virtual ~ServerContextImpl();

	//**************** derived from ServerContext ****************//
	const Version& getVersion();
	void initialize(ChannelAccess::shared_pointer const & channelAccess);
	void run(epics::pvData::int32 seconds);
	void shutdown();
	void destroy();
	void printInfo();
	void printInfo(std::ostream& str);
	void dispose();
	void setBeaconServerStatusProvider(BeaconServerStatusProvider::shared_pointer const & beaconServerStatusProvider);
	//**************** derived from Context ****************//
	epics::pvData::Timer::shared_pointer getTimer();
	Channel::shared_pointer getChannel(pvAccessID id);
	Transport::shared_pointer getSearchTransport();
	Configuration::shared_pointer getConfiguration();
	TransportRegistry::shared_pointer getTransportRegistry();

    std::auto_ptr<ResponseHandler> createResponseHandler();
    void beaconAnomalyNotify();

    /**
     * Version.
     */
    static const Version VERSION;


    /**
     * Server state enum.
     */
    enum State {
    	/**
    	 * State value of non-initialized context.
    	 */
    	NOT_INITIALIZED,

    	/**
    	 * State value of initialized context.
    	 */
    	INITIALIZED,

    	/**
    	 * State value of running context.
    	 */
    	RUNNING,

    	/**
    	 * State value of shutdown (once running) context.
    	 */
    	SHUTDOWN,

    	/**
    	 * State value of destroyed context.
    	 */
    	DESTROYED
    };
    /**
     * Names of the enum <code>State</code>
     */
    static const char* StateNames[];

	/**
	 * Get initialization status.
	 * @return initialization status.
	 */
	bool isInitialized();

	/**
	 * Get destruction status.
	 * @return destruction status.
	 */
	bool isDestroyed();

	/**
	 * Get beacon address list.
	 * @return beacon address list.
	 */
	std::string getBeaconAddressList();

	/**
	 * Get beacon address list auto flag.
	 * @return beacon address list auto flag.
	 */
	bool isAutoBeaconAddressList();

	/**
	 * Get beacon period (in seconds).
	 * @return beacon period (in seconds).
	 */
	float getBeaconPeriod();

	/**
	 * Get receiver buffer (payload) size.
	 * @return max payload size.
	 */
	epics::pvData::int32 getReceiveBufferSize();

	/**
	 * Get server port.
	 * @return server port.
	 */
	epics::pvData::int32 getServerPort();

	/**
	 * Set server port number.
	 * @param port new server port number.
	 */
	void setServerPort(epics::pvData::int32 port);

	/**
	 * Get broadcast port.
	 * @return broadcast port.
	 */
	epics::pvData::int32 getBroadcastPort();

	/**
	 * Get ignore search address list.
	 * @return ignore search address list.
	 */
	std::string getIgnoreAddressList();

	/**
	 * Get registered beacon server status provider.
	 * @return registered beacon server status provider.
	 */
	BeaconServerStatusProvider::shared_pointer getBeaconServerStatusProvider();

	/**
	 * Get server newtwork (IP) address.
	 * @return server network (IP) address, <code>NULL</code> if not bounded.
	 */
	osiSockAddr* getServerInetAddress();

	/**
	 * Broadcast transport.
	 * @return broadcast transport.
	 */
	BlockingUDPTransport::shared_pointer getBroadcastTransport();

	/**
	 * Get channel access implementation.
	 * @return channel access implementation.
	 */
	ChannelAccess::shared_pointer getChannelAccess();

	/**
	 * Get channel provider name.
	 * @return channel provider name.
	 */
	std::string getChannelProviderName();

	/**
	 * Set channel provider name. 
	 * This method can only be called before initialize.
	 */
	void setChannelProviderName(std::string providerName);

	/**
	 * Get channel providers.
	 * @return channel providers.
	 */
	std::vector<ChannelProvider::shared_pointer> getChannelProviders();

private:
	/**
	 * Initialization status.
	 */
	State _state;

	/**
	 * A space-separated list of broadcast address which to send beacons.
	 * Each address must be of the form: ip.number:port or host.name:port
	 */
	std::string _beaconAddressList;

	/**
	 * A space-separated list of address from which to ignore name resolution requests.
	 * Each address must be of the form: ip.number:port or host.name:port
	 */
	std::string _ignoreAddressList;

	/**
	 * Define whether or not the network interfaces should be discovered at runtime.
	 */
	bool _autoBeaconAddressList;

	/**
	 * Period in second between two beacon signals.
	 */
	float _beaconPeriod;

	/**
	 * Broadcast port number to listen to.
	 */
	epics::pvData::int32 _broadcastPort;

	/**
	 * Port number for the server to listen to.
	 */
	epics::pvData::int32 _serverPort;

	/**
	 * Length in bytes of the maximum buffer (payload) size that may pass through CA.
	 */
	epics::pvData::int32 _receiveBufferSize;

	/**
	 * Timer.
	 */
	epics::pvData::Timer::shared_pointer _timer;

	/**
	 * Broadcast transport needed for channel searches.
	 */
	BlockingUDPTransport::shared_pointer _broadcastTransport;

	/**
	 * Beacon emitter.
	 */
	BeaconEmitter::shared_pointer _beaconEmitter;

	/**
	 * CAS acceptor (accepts CA virtual circuit).
	 */
	BlockingTCPAcceptor::shared_pointer _acceptor;

	/**
	 * CA transport (virtual circuit) registry.
	 * This registry contains all active transports - connections to CA servers.
	 */
	TransportRegistry::shared_pointer _transportRegistry;

	/**
	 * Channel access.
	 */
	ChannelAccess::shared_pointer _channelAccess;

	/**
	 * Channel provider name.
	 */
	std::string _channelProviderNames;

	/**
	 * Channel provider.
	 */
	std::vector<ChannelProvider::shared_pointer> _channelProviders;

	/**
	 * Run mutex.
	 */
	epics::pvData::Mutex _mutex;

	/**
	 * Run event.
	 */
	epics::pvData::Event _runEvent;

	/**
	 * Beacon server status provider interface (optional).
	 */
	BeaconServerStatusProvider::shared_pointer _beaconServerStatusProvider;

	/**
	 * Initialize logger.
	 */
	void initializeLogger();

	/**
	 * Load configuration.
	 */
	void loadConfiguration();

	/**
	 * Internal initialization.
	 */
	void internalInitialize();

	/**
	 * Initialize broadcast DP transport (broadcast socket and repeater connection).
	 */
	void initializeBroadcastTransport();

	/**
	 * Internal destroy.
	 */
	void internalDestroy();

	/**
	 * Destroy all transports.
	 */
	void destroyAllTransports();
};



}
}


#endif /* SERVERCONTEXT_H_ */
