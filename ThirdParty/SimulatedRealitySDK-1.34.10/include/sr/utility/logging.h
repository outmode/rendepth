/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <string>
#include <ostream>

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
 * \brief Verbosity level enumeration
 */
enum Verbosity : int {
    HighVerbosity = 0,
    MediumVerbosity = 1,
    LowVerbosity = 2,
    NoLogging = 3
};

/*!
 * \brief Class with static logging functionality
 *
 * Third-party dependencies are not exposed when used in header files
 *
 * \ingroup Core API
 */
class DIMENCOSR_API Log {
public:
    /*!
     * \brief Initialize underlying logging implementation (GLog), logs both to file and console
     *
     * Verbosity level can be set to LowVerbosity, MediumVerbosity, HighVerbosity or NoLogging.
     * Default is set to LowVerbosity, for which only errors will be logged.
     * MediumVerbosity - errors and warnings are logged.
     * HighVerbosity - errors, warnings and info messages are logged.
     * NoLogging - all logging messages are suppressed.
     * When called a second time it will enable console logging
     *
     * The underlying logger should only be initialized once.
     * This function will check whether it has already been initialized but the check is not thread-safe.
     * If you want to override the default SR logging behavior, call this function before constructing the first SRContext.
     * Call this function from the same thread in which your SRContext object is constructed.
     *
     * \param - verbosity defines verbosity level to be set
     */
    static void initialize(Verbosity verbosity, std::string logPath, std::string logFilePrefix);

    /*!
     * \brief Initialize underlying logging implementation (GLog) with file logging only
     *
     * Verbosity level can be set to LowVerbosity, MediumVerbosity, HighVerbosity or NoLogging.
     * Default is set to LowVerbosity, for which only errors will be logged.
     * MediumVerbosity - errors and warnings are logged.
     * HighVerbosity - errors, warnings and info messages are logged.
     * NoLogging - all logging messages are suppressed.
     * When called a second time it will disable console logging
     *
     * The underlying logger should only be initialized once.
     * This function will check whether it has already been initialized but the check is not thread-safe.
     * This initialize function is invoked by the SRContext constructor by default.
     * If you want to override the default SR logging behavior, call this function before constructing the first SRContext.
     * Call this function from the same thread in which your SRContext object is constructed.
     *
     * \param - verbosity defines verbosity level to be set
     */
    static void initializeToFile(Verbosity verbosity, std::string logPath, std::string logFilePrefix);

    /*!
     * \brief Logs an informative message if the compiled in debug mode
     *
     * \param Message to be displayed in debug mode
     */
    static void debugInfo(std::string message);

    /*!
     * \brief Logs an informative message
     *
     * \param Non-essential message to be displayed as info
     */
    static void info(std::string message);

    /*!
     * \brief Logs an warning message
     *
     * \param Message to be displayed as warning
     */
    static void warning(std::string message);

    /*!
     * \brief Logs an error message
     *
     * Ensures that the buffer is flushed and that message will be output.
     *
     * \param Message to be displayed as error
     */
    static void error(std::string message);

    /*!
     * \brief Flushes the log
     *
     * Ensures that the buffer is flushed and that all previous messages will be output.
     */
    static void flush();

    /*!
     * Call Google Crash handler (call back). This Should be call
     * for each thread independently
     */
    static void initializeCrashCallBack();
};

}

#undef DIMENCOSR_API
