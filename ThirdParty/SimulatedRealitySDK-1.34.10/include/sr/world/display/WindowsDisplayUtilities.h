/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <Windows.h>
#include <SetupAPI.h>
#include <tuple>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <map>
#include <physicalmonitorEnumerationapi.h>

#include "sr/types.h"

#ifdef WIN32
#   ifdef COMPILING_DLL_SimulatedRealityDisplays
#     define DIMENCOSR_API __declspec(dllexport)
#   else
#     define DIMENCOSR_API __declspec(dllimport)
#   endif
#else
#   define DIMENCOSR_API
#endif

namespace SR {
    union EdidData {
        unsigned char buffer[128];
        struct {
            unsigned char pad[8];
            unsigned char manufacturerId[2];
            unsigned char productId[2];       //10-11
            unsigned char serialNumber[4];    //12-15
        };
    };

    struct Resolution {
        int height;
        int width;
    };

    /*!
     * \brief Holds all data required to set the DDC/CI settings of the display.
     */
    struct DDCCISettings {
        HANDLE physicalMonitorHandle; /*!< Physical monitor handle retrieved from the monitorHandle */
        unsigned char memoryAddress; /*!< The memory address to be used for setting the DDC/CI value */
        unsigned long disableFiltersValue; /*!< The value to set at the memory address to disable filters */
        unsigned long enableFiltersValue; /*!< The value to set at the memory address to enable filters */
    };

    struct MonitorData {
        HMONITOR monitorHandle;
        SR_recti rectangle;
        EdidData edid;
        Resolution displayResolution;
        Resolution nativeResolution;
        bool isDuplicated;
    };

    /**
     * \brief checks if the given productCode is supported
     *
     * \param productCode the productCode to check
     *
     * \return true if the productCode is supported, false otherwise.
     */
    DIMENCOSR_API bool isProductCodeSupported(std::string productCode);

    /**
     * \brief Gets preferred or recommended resolutions of the monitor
     *
     * \param adapterLuid adpater luid of the target monitor
     * \param targetId id of the monitor which resolution will be selected
     * \param PreferredMode resolution of the target monitor will be added here
     *
     * \return true if getting the configuration is successful otherwise false
     */
    DIMENCOSR_API bool getNativeResolution(const LUID adapterLuid, const UINT32 targetId, DISPLAYCONFIG_TARGET_PREFERRED_MODE& PreferredMode);

    /**
     * \brief Gets path of the given monitor handler
     *
     * \param hMonitor handler of the monitor
     * \param pPathInfo contains information about display configuration path
     *
     * \return HRESULT success if monitor information retrieval is successful otherwise fail
     */
    DIMENCOSR_API HRESULT GetPathInfo(HMONITOR hMonitor, _Out_ DISPLAYCONFIG_PATH_INFO* pPathInfo);

    /**
     * \brief Get display resolution of the given monitor
     *
     * \param[in] deviceName name of the monitor
     * \param[out] height monitor's height set in display resolution
     * \param[out] width monitor's width set in display resolution
     */
    DIMENCOSR_API bool getDisplayResolution(LPCSTR deviceName, int& height, int& width);

    /**
     * \brief Gets all active monitor handles and EDID information
     *
     * \param monitors vector containing MonitorData structs
     *
     * return true if found displays, otherwise false
     */
    DIMENCOSR_API bool getMonitorList(std::vector<MonitorData>& monitors);

    /**
     * \brief Gets edid data given a key from the registry and parses the data
     *
     * \param hDevRegKey Windows registry key. Assumed it is valid.
     * \param EdidData A valid EdidData struct
     *
     * TODO Parse the data
     * TODO clean code
     */
    DIMENCOSR_API bool getEdidData(const HKEY& hDevRegKey, EdidData& edid);

    /**
     * \brief logs the virtual rectangle, virtual position and the srDisplay boolean
     *
     * \param data A valid MonitorData structure
     * \param srDisplay wheter data is an SR display
     */
    DIMENCOSR_API void logSrMonitor(MonitorData data, bool srDisplay);

    /**
     * \brief logs all members for a given list of MonitorData
     *
     * \param monitors A valid vector of MonitorData objects
     */
    DIMENCOSR_API void printMonitorList(std::vector<MonitorData> monitors);

    /**
     * \brief Gets a list of SR displays from a list of displays.
     * Clears knownMonitors and appends SR monitors found in monitors.
     *
     * \param[in]  monitors A list of active monitors
     * \param[out] knownMonitors A list of monitors known to be SR monitors
     *
     * \return true if one or more SR displays are found, otherwise false
     */
    DIMENCOSR_API bool getKnownMonitors(const std::vector<MonitorData>& monitors, std::vector<MonitorData>& knownMonitors);

    /**
     * \brief gets a list of SR displays that support DDC/CI.
     *  Clears DDCCISupportedMonitors and appends the DDC/CI settings of SR displays known to support DDC/CI.
     *
     * \param[in]  monitors A list of active monitors
     * \param[out] DDCCISupportedMonitors A list of SR displays known to support DDC/CI with their desired settings
     *
     * \return true if one or more SR displays are found that support DDC/CI, otherwise false
     */
    DIMENCOSR_API bool getDDCCISupportedMonitors(const std::vector<MonitorData>& monitors, std::vector<DDCCISettings>& DDCCISupportedMonitors);

    /**
     * \brief returns true if attach/detach notifications should be disabled for the currently attached monitors.
     * This function assumes only one SR device is attached at a time, it uses the list disableAttachDetachNotificationsByProductCode which should only contain laptop product codes.
     * This function only verifies an SR device by EDID, so it is assumed any SR device will have an EDID that it does not share with non-SR devices.
     * Because it can only verify an SR device by EDID it will only disable notifications related to FPC attach/detach and not monitor attach/detach.
     * Which are events that should not happen for a laptop.
     *
     * \return true if attach/detach notifications should be disabled
     */
    DIMENCOSR_API bool disableAttachDetachNotifications();

    /*!
     * \brief Check expected EDID productId for a given product code
     *
     * \param[in] productCode identifying an SR product, such as AJ, D1 or AN
     * \param[in] monitorData struct containing EDID data to check
     * \return true if the productCode matches the EDID in monitorData or false otherwise
     *
     * Will also return false if the product code is not known
     */
    DIMENCOSR_API bool matchProductId(const std::string productCode, const MonitorData& monitorData);

    /**
     * \brief add preferred resolution of the given monitors
     *
     * \param externalMonitors list of connected monitors which preferred resolution would be added
     */
    DIMENCOSR_API void addPreferredResolution(std::vector<MonitorData>& externalMonitors);

    /**
     * \brief add clone or duplicate status of connected monitors
     *
     * \param externalMonitors list of connected monitors which clone status will be added
     */
    DIMENCOSR_API void addCloneStatus(std::vector<MonitorData>& externalMonitors);

    /**
     * \brief Compare two given monitors and see if they are the equal based on the EDID data in MonitorData.
     *
     * \param monitorA A valid MonitorData object
     * \param monitorB A valid MonitorData object
     *
     * \return True if both given monitors are equal based on the EDID data.
     */
    DIMENCOSR_API bool monitorEquals(const SR::MonitorData& monitorA, const SR::MonitorData& monitorB);

    /**
     * \brief Compare two given monitors and see if they are the equal based on the resolution and duplication values.
     * Specifically, the rectangle, isDuplicated, and displayResolution members are compared.
     *
     * \param monitorA A valid MonitorData object
     * \param monitorB A valid MonitorData object
     *
     * \return True if both given monitors are equal based on resolution and duplication values.
     */
    DIMENCOSR_API bool monitorConfigurationEquals(const SR::MonitorData& monitorA, const SR::MonitorData& monitorB);

    /**
     * \brief Compare the displayResolution and the nativeResolution of the given monitor
     *
     * \param monitor A valid MonitorData object
     *
     * \return True if both the displayResolution and the nativeResolution of the given monitorData are equal.
     */
    DIMENCOSR_API bool hasNonNativeResolution(const SR::MonitorData& monitor);

    /*!
     * \brief Extract RECT rectangles from a list of MonitorData monitors
     */
    DIMENCOSR_API std::vector<RECT> getMonitorRectangles(const std::vector<MonitorData>& monitors);

    /*!
     * \brief Updates monitorRectangles with the rectangles of the monitors currently returned by getMonitorList() and blocks until this list has changed (as a set)
     *
     * \param monitorRectangles is a reference to a vector of RECTs that will be updated, this function blocks until the contents of this vector has changed (as a set).
     * \param timeBetweenChecks is an integer expressing the number of milliseconds to wait between attempts to get the monitor list
     * \param active is a reference to a boolean that reflects whether the function should keep waiting, if it is ever set to false the function will stop blocking
     */
    DIMENCOSR_API void waitForChangeMonitorRectangles(std::vector<RECT>& monitorRectangles, unsigned int timeBetweenChecks, bool& active);

    /*!
     * \brief Return the refresh rate in hertz of the monitor that is represented by the given monitorHandle
     * If the monitor handle is invalid or the refresh rate could otherwise not be retrieved a default value of 60 will be returned.
     *
     * \param monitorHandle is a HMONITOR monitor handle
     *
     * \return The refresh rate in hertz of the monitorHandle provided.
     */
    DIMENCOSR_API double getMonitorRefreshRate(HMONITOR monitorHandle);
}

#undef DIMENCOSR_API
