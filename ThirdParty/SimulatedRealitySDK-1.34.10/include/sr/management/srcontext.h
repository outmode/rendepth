/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <string>
#include <map>

#include "sr/sense/core/sense.h"
#include "sr/world/worldobject.h"
#include "sr/network/core/networkinterface.h"
#include "sr/network/core/receiver.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

/**
 * \brief Enumeration of SR_contextMessage types
 *
 * \ingroup Core_Network
 */
enum SR_contextMessageType : uint64_t {
    Initialize,
};

/**
 * \brief C-compatible struct to signal the SR context
 *
 * Indicates that a certain function of the context needs to be invoked.
 *
 * \ingroup Core_Network
 */
typedef struct {
    SR_contextMessageType type;
} SR_contextMessage;

/**
 * \defgroup API 
 * \brief Application Programming Interface to be used by C++ SR applications
 */

/**
 * \defgroup Core
 * \brief Classes used to shape the foundation of the SR system
 *
 * Offers many base classes and templates to be implemented in more specific modules to allow the SRContext to keep track of them.
 */

/**
 * \defgroup Core_Network
 * \brief Classes that form the foundation of cross-application communication for the SR system
 */

/**
 * \brief Namespace containing all C++ Simulated Reality classes
 */
namespace SR {

//Forward declaration
class Configuration;

/**
 * \brief Maintains WorldObject and Sense objects during the application lifetime
 *
 * \ingroup DownstreamInterface Core API
 */
class DIMENCOSR_API SRContext : public Receiver {
private:
    uint64_t other = 0; // Remote Receiver*
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(push)
#pragma warning(disable: 4251)
    std::map<std::string, std::vector<WorldObject*>> worldObjects;
    std::map<std::string, std::vector<Sense*>> senses;
    std::unique_ptr<Configuration> configuration;
#pragma warning(pop)

    void calibrateAllSenses();
    void startAllSenses();
    void stopAllSenses();
    void deleteAllSenses();

public:
    enum NetworkMode {
        EdgeMode, //!< Identical to ServerMode
        ServerMode, //!< Send data to client
        ClientMode, //!< Receive data from server, keep connecting to server until available
        HybridMode, //!< Same as StandAlone mode
        StandaloneMode, //!< ClientMode if server is reachable, ServerMode behaviour otherwise
        NonBlockingClientMode, //!< Try to connect to server once, throw exception otherwise
    };

private:
    void construct(NetworkMode mode, std::string address, std::string port, bool lensPreference);

public:
    /**
     * \brief Affects which implementation of a certain interface will be constructed when the associated create function is called
     */
    const NetworkMode mode;

    /**
     * \brief NetworkInterface to application controlling sense implementations
     *
     * There is one application which controls the physical components, this NetworkInterface connects to it.
     * When a Sense is created and this is not nullptr, the functional implementation will be constructed on the remote application.
     *
     * TODO scope should be private (server is setting connectionToServer directly)
     */
    NetworkInterface* connectionToServer = nullptr;

    /**
     * \brief instance of Client 
     *
     * Connection to a more centralized application in the SR system.
     * This may connect to the application controlling the physical components or services shared between SR devices
     * connectionToServer may point to the same instance as client unless server is connected to a remote server
     *
     * TODO scope should be private (accessed to check mode in several locations to switch between implementations)
     */
    NetworkInterface* client = nullptr;

    /**
     * \brief Construct the SRContext
     *
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     */
    SRContext(NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief Construct the SRContext
     *
     * \param server address to connect to
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     */
    SRContext(std::string serverAddress, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief Construct the SRContext
     *
     * \param lens preference determines the initial lens state preference
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     */
    SRContext(bool lensPreference, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief Construct the SRContext
     *
     * \param lens preference determines the initial lens state preference
     * \param server address to connect to
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     */
    SRContext(bool lensPreference, std::string serverAddress, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief create new SRContext instance
     *
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     * \return SRContext* new instance
     */
    static SRContext* create(NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief create new SRContext instance
     *
     * \param server address to connect to
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     * \return SRContext* new instance
     */
    static SRContext* create(const char* serverAddress, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief create new SRContext instance
     *
     * \param lens preference determines the initial lens state preference
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     * \return SRContext* new instance
     */
    static SRContext* create(bool lensPreference, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief create new SRContext instance
     *
     * \param lens preference determines the initial lens state preference
     * \param server address to connect to
     * \param mode affects which implementation of a certain interface will be constructed when the associated create function is called
     * \throw a ServerNotAvailableException when the context could not establish a connection to the SR Service, the SR Service might not be running.
     * \return SRContext* new instance
     */
    static SRContext* create(bool lensPreference, const char* serverAddress, NetworkMode mode = NetworkMode::NonBlockingClientMode);

    /**
     * \brief delete SRContext instance
     *
     * \param context to be deleted
     */
    static void deleteSRContext(SRContext* context);

    /**
     * \brief set remote SRContext to communicate with
     *
     * \param srContext is used to identify remote SRContext
     */
    void setOther(uint64_t srContext);

    /**
     * \brief Clean up the SRContext and associated world
     */
    ~SRContext();

    /**
     * \brief Initialize all senses
     *
     * Call *after* all objects and senses have been added.
     */
    void initialize();

    /**
     * \brief Register a world object of the given interface with the context
     *
     * \param interfaceIdentifier group identifier used as part of a given interface's create function
     * \param worldObject to be registered as \p interfaceIdentifier
     */
    void addObject(std::string interfaceIdentifier, WorldObject* worldObject);

    /**
     * \brief Get all registered world objects of the given interface
     *
     * \param interfaceIdentifier group identifier used as part of a given interface's create function
     * \return @link WorldObject std::vector<WorldObject*>& @endlink which is a list of objects registered as \p interfaceIdentifier
     */
    const std::vector<WorldObject*>& getObjects(std::string interfaceIdentifier);

    /**
     * \brief Register a sense of the given interface with the context
     *
     * \param interfaceIdentifier group identifier used as part of a given interface's create function
     * \param sense to be registered as \p interfaceIdentifier
     */
    void addSense(std::string interfaceIdentifier, Sense* sense);

    /**
     * \brief Unregister a sense of the given interface from the context
     *
     * \param interfaceIdentifier group identifier used as part of a given interface's create function
     * \param sense to be unregistered as \p interfaceIdentifier
     */
    void removeSense(std::string interfaceIdentifier, Sense* sense);

    /**
     * \brief Get all registered senses of the given interface
     *
     * \param interfaceIdentifier group identifier used as part of a given interface's create function
     * \return @link Sense std::vector<Sense*>& @endlink which is a list of senses registered as \p interfaceIdentifier
     */
    const std::vector<Sense*>& getSenses(std::string interfaceIdentifier);

    /**
     * \brief Get configuration information
     *
     * \return const Configuration & object to represent configurations of the full system
     *
     * \deprecated Senses should handle their own configuration.
     */
    const Configuration& getConfiguration();

    /**
     * \brief Receives SR_packet sent over network to control remote instances of SRContext
     *
     * \param packet is a C-compatible message which requires no further serialization
     *
     * SRContext specifically receives packets to achieve the desired startup behaviour.
     * Inherited via Receiver.
     */
    virtual void receive(SR_packet& packet) override;

    /**
     * \brief Prints text representation of SR_packet containing eye position updates sent over network.
     *
     * \param packet is a C-compatible message which requires no further serialization
     *
     * Used for debugging purposes.
     * Inherited via Receiver.
     */
    virtual void print(SR_packet& packet) override;
};
 
}

#undef DIMENCOSR_API
