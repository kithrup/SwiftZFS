#ifndef SWIFTZFS_INTERNAL_HPP
#define SWIFTZFS_INTERNAL_HPP

#include <libzfs.h>

#include <cstdint>
#include <string>
#include <vector>

class SwiftZFSContext {
public:
    SwiftZFSContext();
    ~SwiftZFSContext();

    SwiftZFSContext(const SwiftZFSContext &) = delete;
    SwiftZFSContext &operator=(const SwiftZFSContext &) = delete;

    libzfs_handle_t *handle() const noexcept { return handle_; }

    void clearError() noexcept;
    void captureLibZFSError() noexcept;
    void setError(int32_t code, const char *description) noexcept;

    int32_t lastZFSError() const noexcept { return last_zfs_error_; }
    const char *lastErrorDescription() const noexcept {
        return last_error_description_.c_str();
    }

private:
    libzfs_handle_t *handle_ = nullptr;
    int32_t last_zfs_error_ = 0;
    std::string last_error_description_;
};

class SwiftZFSPoolList {
public:
    explicit SwiftZFSPoolList(SwiftZFSContext &context);

    size_t count() const noexcept { return names_.size(); }
    const char *nameAt(size_t index) const noexcept;

private:
    static int collectPool(zpool_handle_t *pool, void *argument);

    std::vector<std::string> names_;
};

#endif /* SWIFTZFS_INTERNAL_HPP */
