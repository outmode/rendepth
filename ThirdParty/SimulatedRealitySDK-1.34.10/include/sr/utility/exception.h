/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <vector>
#include <string>
#include <exception>

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityCore
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {


/*!
 * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
 * Warning Description: non - DLL-interface class 'class_1' used as base for DLL-interface class 'class_2'
 * Candidate for deprecation
 */
#pragma warning(push)
#pragma warning(disable: 4275)

/*! \brief Class of exception which indicates a general SR logic error
 *  \ingroup API
 */
class DIMENCOSR_API Exception : public std::exception {

public:
    /*! \brief Constructor (C strings).
     *  \param message C-style string error message.
     */
    explicit Exception(const char* message) :
        errorMessage(message)
    {
    }

    /*! \brief Constructor (C++ STL strings).
     *  \param message The error message.
     */
    explicit Exception(const std::string& message) :
        errorMessage(message)
    {}

    /*! \brief Destructor.
     * Virtual to allow for subclassing.
     */
    virtual ~Exception() throw () {}

    /*! \brief Returns a pointer to the (constant) error description.
     *  \return A pointer to a const char* to represent the error description.
     */
    virtual const char* what() const throw () {
        return errorMessage.c_str();
    }

protected:
    /*! \brief Error message.
     */
     /*!
      * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
      * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
      * Candidate for deprecation
      */
#pragma warning(suppress: 4251)
    std::string errorMessage;
};
#pragma warning(pop)


//! \brief Class of exception which indicates that a hardware device was not available for use in the SR system
//!
class DIMENCOSR_API DeviceNotAvailableException : public Exception {

public: 
    /*! \brief Constructs an instance of DeviceNotAvailableException for a specific device identifier
     *  \param deviceIdentifier contains a string to represent a device which doesn't seem to be available to the system.
     */
    DeviceNotAvailableException(std::string deviceIdentifier);

    //! \brief Constructs an instance of DeviceNotAvailableException
    //!
    DeviceNotAvailableException();
};

//! \brief Class of Exception which indicates that a SR Service was not available to connect with
//!
class DIMENCOSR_API ServerNotAvailableException : public Exception {

public:
    //! \brief Constructs an instance of ServerNotAvailableException
    //!
    ServerNotAvailableException();
};
}

#undef DIMENCOSR_API
