#include "SwiftZFS.h"
#include "SwiftZFSInternal.hpp"

#include <new>
#include <stdexcept>

struct swiftzfs_context {
    SwiftZFSContext impl;
};

struct swiftzfs_pool_list {
    explicit swiftzfs_pool_list(SwiftZFSContext &context) : impl(context) {}
    SwiftZFSPoolList impl;
};

SwiftZFSContext::SwiftZFSContext()
    : handle_(libzfs_init())
{
    if (handle_ == nullptr) {
        throw std::runtime_error("libzfs_init() failed");
    }
}

SwiftZFSContext::~SwiftZFSContext()
{
    if (handle_ != nullptr) {
        libzfs_fini(handle_);
    }
}

void
SwiftZFSContext::clearError() noexcept
{
    last_zfs_error_ = 0;
    last_error_description_.clear();
}

void
SwiftZFSContext::captureLibZFSError() noexcept
{
    if (handle_ == nullptr) {
        setError(0, "libzfs context is not initialized");
        return;
    }

    last_zfs_error_ = static_cast<int32_t>(libzfs_errno(handle_));
    const char *description = libzfs_error_description(handle_);
    last_error_description_ = description != nullptr ? description : "libzfs error";
}

void
SwiftZFSContext::setError(int32_t code, const char *description) noexcept
{
    last_zfs_error_ = code;
    last_error_description_ = description != nullptr ? description : "";
}

SwiftZFSPoolList::SwiftZFSPoolList(SwiftZFSContext &context)
{
    context.clearError();
    int error = zpool_iter(context.handle(), &SwiftZFSPoolList::collectPool, this);
    if (error != 0) {
        context.captureLibZFSError();
        throw std::runtime_error("zpool_iter() failed");
    }
}

int
SwiftZFSPoolList::collectPool(zpool_handle_t *pool, void *argument)
{
    auto *self = static_cast<SwiftZFSPoolList *>(argument);

    const char *name = zpool_get_name(pool);
    if (name != nullptr) {
        self->names_.emplace_back(name);
    }

    /* zpool_iter() transfers the temporary handle to the callback. */
    zpool_close(pool);
    return 0;
}

const char *
SwiftZFSPoolList::nameAt(size_t index) const noexcept
{
    if (index >= names_.size()) {
        return nullptr;
    }
    return names_[index].c_str();
}

extern "C" int32_t
swiftzfs_context_create(swiftzfs_context_t **result)
{
    if (result == nullptr) {
        return SWIFTZFS_INVALID_ARGUMENT;
    }

    *result = nullptr;

    try {
        *result = new swiftzfs_context;
        return SWIFTZFS_OK;
    } catch (const std::bad_alloc &) {
        return SWIFTZFS_NO_MEMORY;
    } catch (...) {
        return SWIFTZFS_ERROR;
    }
}

extern "C" void
swiftzfs_context_destroy(swiftzfs_context_t *context)
{
    delete context;
}

extern "C" int32_t
swiftzfs_context_last_zfs_error(const swiftzfs_context_t *context)
{
    return context != nullptr ? context->impl.lastZFSError() : 0;
}

extern "C" const char *
swiftzfs_context_last_error_description(const swiftzfs_context_t *context)
{
    if (context == nullptr) {
        return "invalid SwiftZFS context";
    }
    return context->impl.lastErrorDescription();
}

extern "C" int32_t
swiftzfs_pool_list_create(
    swiftzfs_context_t *context,
    swiftzfs_pool_list_t **result)
{
    if (context == nullptr || result == nullptr) {
        return SWIFTZFS_INVALID_ARGUMENT;
    }

    *result = nullptr;

    try {
        *result = new swiftzfs_pool_list(context->impl);
        return SWIFTZFS_OK;
    } catch (const std::bad_alloc &) {
        context->impl.setError(0, "out of memory");
        return SWIFTZFS_NO_MEMORY;
    } catch (...) {
        if (context->impl.lastErrorDescription()[0] == '\0') {
            context->impl.setError(0, "unable to enumerate ZFS pools");
        }
        return SWIFTZFS_ERROR;
    }
}

extern "C" void
swiftzfs_pool_list_destroy(swiftzfs_pool_list_t *list)
{
    delete list;
}

extern "C" size_t
swiftzfs_pool_list_count(const swiftzfs_pool_list_t *list)
{
    return list != nullptr ? list->impl.count() : 0;
}

extern "C" const char *
swiftzfs_pool_list_name_at(const swiftzfs_pool_list_t *list, size_t index)
{
    return list != nullptr ? list->impl.nameAt(index) : nullptr;
}
