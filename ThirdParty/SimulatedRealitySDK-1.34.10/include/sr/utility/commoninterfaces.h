/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once

#include <typeindex>
namespace SR {
    /*!
     * \brief Interface for objects that can be destroyed
     *
     * \ingroup API
     */
    class IDestroyable
    {
    public:
        /*!
         * \brief Deletes this object. All future references will be invalid.
        */
        virtual void destroy() = 0;
    };

    /*!
     * \brief Interface for querying interfaces with a specific type
     *
     * \ingroup API
     */
    class IQueryInterface
    {
    protected:
        virtual void* queryInterface(std::type_index type) = 0;

    public:
        /*!
         * \brief Query the interface from the running Runtime. Returns nullptr when this interface is not available.
         */
        template<typename T>
        inline T* query() { return static_cast<T*>(queryInterface(typeid(T))); }
    };
}
