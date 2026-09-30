/*!
 * Copyright (C) 2025 Leia, Inc.
 */

#pragma once
#include <memory>

namespace SR {

/**
 * \brief Template class to wrap data stream to a listener object
 *
 * \tparam StreamType should be a stream object with a stopListening() function such as a GenericStream implementation
 *
 * This should be a member of any listener implementation!
 *
 * \ingroup Core API
 */
template<typename StreamType>
class InputStream {
    /*!
     * Suppressing warning because if we don't want to export everything then solving the underlying problem requires modification of the API
     * Warning Description: 'type' : class 'type1' needs to have dll-interface to be used by clients of class 'type2'
     * Candidate for deprecation
     */
#pragma warning(suppress: 4251)
    std::shared_ptr<StreamType> stream = nullptr;

public:
    /**
     * \brief Cleans up the associated \p StreamType stream.
     */
    ~InputStream() {
        if (stream != nullptr) {
            stream->stopListening();
        }
    }

    /**
     * \brief Must be set to allow proper cleanup of internal stream
     *
     * \param input stream of StreamType to be cleaned up after destruction
     *
     * All implementations of SR listeners should call this function as part of their constructor.
     * Doing so ensures that the stream is stopped and cleaned up correctly.
     * Alternatively, users can call stopListening on the stream themselves.
     */
    void set(std::shared_ptr<StreamType> input) {
        if (stream != nullptr) {
            stream->stopListening();
        }
        stream = input;
    }
};

}
