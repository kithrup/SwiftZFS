#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace zfs {

class Error : public std::runtime_error {
public:
    enum class Code {
        unknown,
        initialization_failed,
        invalid_argument,
        permission_denied,
        not_found,
        already_exists,
        io_error,
        no_memory,
        no_such_pool,
        pool_unavailable,
        pool_busy,
        pool_faulted,
        invalid_pool,
        unsupported,
        not_implemented
    };

    Error(Code code, int system_error, const std::string& message);

    Code code() const noexcept;
    int system_error() const noexcept;

private:
    Code code_;
    int system_error_;
};

enum class PoolState {
    unknown,
    active,
    exported,
    destroyed,
    potentially_active,
    unavailable
};

enum class PoolStatus {
    unknown,
    online,
    degraded,
    faulted,
    offline,
    unavailable,
    removed
};


enum class VDevRole {
    data,
    log,
    spare,
    cache,
    special,
    dedup,
    unknown
};

class VDev {
public:
    const std::string& type() const noexcept;
    const std::string& path() const noexcept;
    std::uint64_t guid() const noexcept;
    VDevRole role() const noexcept;
    const std::vector<VDev>& children() const noexcept;
    bool leaf() const noexcept;

    VDev(std::string type, std::string path, std::uint64_t guid,
        VDevRole role, std::vector<VDev> children = {});

private:
    std::string type_;
    std::string path_;
    std::uint64_t guid_ = 0;
    VDevRole role_ = VDevRole::unknown;
    std::vector<VDev> children_;
};

enum class PropertySource {
    none,
    default_value,
    temporary,
    local,
    inherited,
    received,
    unknown
};

enum class PropertyType {
    number,
    string,
    index,
    unknown
};

class Property {
public:
    const std::string& name() const noexcept;
    const std::string& value() const noexcept;
    PropertySource source() const noexcept;
    PropertyType type() const noexcept;
    bool readonly() const noexcept;

private:
    friend class Dataset;
    friend class Pool;

    Property(std::string name, std::string value,
        PropertySource source, PropertyType type, bool readonly);

    std::string name_;
    std::string value_;
    PropertySource source_ = PropertySource::unknown;
    PropertyType type_ = PropertyType::unknown;
    bool readonly_ = false;
};

enum class FeatureState {
    disabled,
    enabled,
    active,
    unsupported_inactive,
    unsupported_readonly,
    unknown
};

class PoolFeature {
public:
    const std::string& name() const noexcept;
    FeatureState state() const noexcept;
    bool supported() const noexcept;

private:
    friend class Pool;

    PoolFeature(std::string name, FeatureState state, bool supported);

    std::string name_;
    FeatureState state_ = FeatureState::unknown;
    bool supported_ = true;
};


class Snapshot;

class Dataset {
public:
    Dataset(Dataset&&) noexcept;
    Dataset& operator=(Dataset&&) noexcept;

    Dataset(const Dataset&) = delete;
    Dataset& operator=(const Dataset&) = delete;

    virtual ~Dataset();

    std::string name() const;
    std::uint64_t guid() const;
    std::vector<Property> properties() const;
    Property property(const std::string& name) const;

    virtual bool is_filesystem() const noexcept;
    virtual bool is_volume() const noexcept;
    virtual bool is_snapshot() const noexcept;
    virtual std::vector<Snapshot> snapshots() const;

protected:
    friend class Pool;

    class Impl;
    explicit Dataset(std::shared_ptr<Impl> impl) noexcept;

    std::shared_ptr<Impl> impl_;
};

class Filesystem final : public Dataset {
public:
    Filesystem(Filesystem&&) noexcept = default;
    Filesystem& operator=(Filesystem&&) noexcept = default;

    bool is_filesystem() const noexcept override;

    bool mounted() const;
    std::string mountpoint() const;

private:
    friend class Pool;

    explicit Filesystem(std::shared_ptr<Impl> impl) noexcept;
};

class Volume final : public Dataset {
public:
    Volume(Volume&&) noexcept = default;
    Volume& operator=(Volume&&) noexcept = default;

    bool is_volume() const noexcept override;

    std::uint64_t size() const;
    std::uint64_t block_size() const;

private:
    friend class Pool;

    explicit Volume(std::shared_ptr<Impl> impl) noexcept;
};

class Snapshot final : public Dataset {
public:
    Snapshot(Snapshot&&) noexcept = default;
    Snapshot& operator=(Snapshot&&) noexcept = default;

    bool is_snapshot() const noexcept override;
    std::vector<Snapshot> snapshots() const override;

private:
    friend class Dataset;

    explicit Snapshot(std::shared_ptr<Impl> impl) noexcept;
};

class DatasetCollection {
public:
    DatasetCollection() = default;
    DatasetCollection(DatasetCollection&&) noexcept = default;
    DatasetCollection& operator=(DatasetCollection&&) noexcept = default;

    DatasetCollection(const DatasetCollection&) = delete;
    DatasetCollection& operator=(const DatasetCollection&) = delete;

    const std::vector<Filesystem>& filesystems() const noexcept;
    const std::vector<Volume>& volumes() const noexcept;

private:
    friend class Pool;

    std::vector<Filesystem> filesystems_;
    std::vector<Volume> volumes_;
};

class Pool {
public:
    Pool(Pool&&) noexcept;
    Pool& operator=(Pool&&) noexcept;

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    ~Pool();

    std::string name() const;
    std::uint64_t guid() const;

    std::vector<Property> properties() const;
    std::vector<PoolFeature> features() const;
    std::vector<VDev> vdevs() const;
    DatasetCollection datasets() const;
    std::vector<Filesystem> filesystems() const;
    std::vector<Volume> volumes() const;

    PoolState state() const;
    PoolStatus status() const;
    void export_pool();
    void destroy();

private:
    friend class ZFS;

    class Impl;
    explicit Pool(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

class ImportablePool {
public:
    ImportablePool(const ImportablePool&) = default;
    ImportablePool& operator=(const ImportablePool&) = default;
    ImportablePool(ImportablePool&&) noexcept = default;
    ImportablePool& operator=(ImportablePool&&) noexcept = default;
    ~ImportablePool() = default;

    const std::string& name() const noexcept;
    std::uint64_t guid() const noexcept;
    PoolState state() const noexcept;
    const std::vector<VDev>& vdevs() const noexcept;

private:
    friend class ZFS;

    ImportablePool(std::string name, std::uint64_t guid, PoolState state,
        std::vector<VDev> vdevs = {});

    std::string name_;
    std::uint64_t guid_ = 0;
    PoolState state_ = PoolState::unknown;
    std::vector<VDev> vdevs_;
};

class ZFS {
public:
    ZFS();
    ~ZFS();

    ZFS(const ZFS&) = delete;
    ZFS& operator=(const ZFS&) = delete;

    ZFS(ZFS&&) noexcept;
    ZFS& operator=(ZFS&&) noexcept;

    std::string userspace_version() const;
    std::string kernel_version() const;

    std::vector<Pool> pools() const;
    std::vector<ImportablePool> search_pools() const;
    Pool import_pool(const ImportablePool& pool);
    Pool create_pool(const std::string& name);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

const char* to_string(PropertySource source) noexcept;
const char* to_string(PropertyType type) noexcept;
const char* to_string(FeatureState state) noexcept;
const char* to_string(VDevRole role) noexcept;

} // namespace zfs
