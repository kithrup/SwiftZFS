#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @file ZFS.hpp
 * @brief Public C++ interface for the SwiftZFS OpenZFS wrapper.
 *
 * The API models the major OpenZFS objects without exposing libzfs, nvlist,
 * or other OpenZFS implementation types.  ZFS is the top-level library
 * context; Pool represents an imported pool; ImportablePool represents a pool
 * found during import discovery; Dataset is the common base for Filesystem,
 * Volume, and Snapshot; VDev describes the storage topology; Property and
 * PoolFeature expose read-only metadata.
 */

/** Public API for the C++ OpenZFS wrapper. */
namespace zfs {

/**
 * Property name/value pairs supplied when creating or modifying ZFS objects.
 *
 * The key is the ZFS property name and the mapped value is the string value
 * passed to OpenZFS.  Snapshot creation currently accepts user properties
 * only.
 */
using PropertyValues = std::map<std::string, std::string>;

/**
 * Options controlling the format of a ZFS send stream.
 *
 * The flags correspond to stream features supported by libzfs_core.  The
 * default values produce the most conservative stream.
 */
struct SendOptions {
    bool embedded_data = false; ///< Permit embedded-data records in the stream.
    bool large_blocks = false;  ///< Permit blocks larger than 128 KiB.
    bool compressed = false;    ///< Preserve compressed blocks in the stream.
    bool raw = false;           ///< Produce a raw encrypted send stream.
};

/**
 * Exception raised by the C++ ZFS wrapper.
 *
 * Error wraps failures reported by libzfs as well as errors detected by the
 * wrapper itself.  The std::runtime_error message contains a human-readable
 * description; code() and system_error() provide machine-readable details.
 */
class Error : public std::runtime_error {
public:
    /** Wrapper-level error classifications. */
    enum class Code {
        unknown,              ///< Failure that cannot be classified more specifically.
        initialization_failed, ///< libzfs or wrapper initialization failed.
        invalid_argument,     ///< A caller supplied an invalid argument.
        permission_denied,    ///< The requested operation was not permitted.
        not_found,            ///< The requested object or property does not exist.
        already_exists,       ///< Creation failed because the object already exists.
        io_error,             ///< An underlying I/O operation failed.
        no_memory,            ///< Memory allocation failed.
        no_such_pool,         ///< The named pool does not exist.
        pool_unavailable,     ///< The pool exists but is unavailable for the operation.
        pool_busy,            ///< The pool is busy and cannot complete the operation.
        pool_faulted,         ///< The pool is faulted.
        invalid_pool,         ///< Pool configuration or metadata is invalid.
        unsupported,          ///< The requested operation or feature is unsupported.
        not_implemented       ///< The wrapper API exists but has not yet been implemented.
    };

    /**
     * Construct an error.
     *
     * @param code Wrapper-level error classification.
     * @param system_error Native/libzfs error number, or zero when none exists.
     * @param message Human-readable error description.
     * @throws std::bad_alloc if the base exception cannot copy the message.
     */
    Error(Code code, int system_error, const std::string& message);

    /**
     * Return the wrapper-level error classification.
     *
     * @return The error classification supplied when the exception was created.
     * @throws Nothing.
     */
    Code code() const noexcept;

    /**
     * Return the associated native/libzfs error number.
     *
     * @return The native error number, or zero when no native error applies.
     * @throws Nothing.
     */
    int system_error() const noexcept;

private:
    Code code_;
    int system_error_;
};

/**
 * Administrative state recorded for a ZFS storage pool.
 *
 * This describes how the pool is recorded/discovered, not its health.  Use
 * PoolStatus for health such as online or degraded.
 */
enum class PoolState {
    unknown,            ///< State could not be determined.
    active,             ///< Pool appears to be currently imported/active.
    exported,           ///< Pool was cleanly exported and is available for import.
    destroyed,          ///< Pool was destroyed but can still be discovered in metadata.
    potentially_active, ///< Pool appears active elsewhere or was not cleanly exported.
    unavailable         ///< Pool state cannot presently be used or determined reliably.
};

/** High-level operational health of a ZFS storage pool. */
enum class PoolStatus {
    unknown,     ///< Health could not be determined.
    online,      ///< Pool is operating normally.
    degraded,    ///< Pool is operating with reduced redundancy or another fault.
    faulted,     ///< Pool cannot operate because of unrecoverable faults.
    offline,     ///< Pool or required devices are administratively offline.
    unavailable, ///< Pool cannot currently be accessed.
    removed      ///< Pool or required devices have been removed.
};

/** Functional role of a vdev within a pool. */
enum class VDevRole {
    data,    ///< Normal data-storage vdev.
    log,     ///< Separate intent log (SLOG) vdev.
    spare,   ///< Hot spare vdev.
    cache,   ///< L2ARC cache vdev.
    special, ///< Special allocation-class vdev.
    dedup,   ///< Deduplication allocation-class vdev.
    unknown  ///< Role could not be determined.
};

/**
 * Read-only description of a vdev in a pool topology.
 *
 * VDev is a value type.  Composite vdevs such as mirrors and RAID-Z groups
 * contain child VDev objects.  Removed top-level ZFS "hole" placeholders are
 * intentionally omitted from the public topology.
 */
class VDev {
public:
    /**
     * Return the OpenZFS vdev type name (for example "disk", "mirror", or
     * "raidz").
     *
     * @return Reference to the type string owned by this VDev.
     * @throws Nothing.
     */
    const std::string& type() const noexcept;

    /**
     * Return the device path, if the vdev has one.
     *
     * @return Reference to the path string; empty for vdevs without a path.
     * @throws Nothing.
     */
    const std::string& path() const noexcept;

    /**
     * Return the vdev GUID.
     *
     * @return The vdev GUID, or zero when the OpenZFS configuration has none.
     * @throws Nothing.
     */
    std::uint64_t guid() const noexcept;

    /**
     * Return the vdev's functional role in the pool.
     *
     * @return The inherited or explicitly configured vdev role.
     * @throws Nothing.
     */
    VDevRole role() const noexcept;

    /**
     * Return this vdev's immediate children.
     *
     * @return Reference to the child-vdev collection owned by this object.
     * @throws Nothing.
     */
    const std::vector<VDev>& children() const noexcept;

    /**
     * Test whether this vdev has no child vdevs.
     *
     * @return true for a leaf vdev; false for a composite vdev.
     * @throws Nothing.
     */
    bool leaf() const noexcept;

    /**
     * Construct a vdev description.
     *
     * Primarily intended for wrapper implementation code and tests.
     *
     * @param type OpenZFS vdev type name.
     * @param path Device path, or an empty string if not applicable.
     * @param guid Vdev GUID.
     * @param role Functional role in the pool.
     * @param children Immediate child vdevs.
     * @throws std::bad_alloc if owned strings or child storage cannot be allocated.
     */
    VDev(std::string type, std::string path, std::uint64_t guid,
        VDevRole role, std::vector<VDev> children = {});

private:
    std::string type_;
    std::string path_;
    std::uint64_t guid_ = 0;
    VDevRole role_ = VDevRole::unknown;
    std::vector<VDev> children_;
};

/** Source from which a ZFS property value was obtained. */
enum class PropertySource {
    none,          ///< No source information is available/applicable.
    default_value, ///< Value comes from the property's built-in default.
    temporary,     ///< Value is a temporary runtime setting.
    local,         ///< Value was set directly on this object.
    inherited,     ///< Value was inherited from an ancestor dataset.
    received,      ///< Value was received, for example by replication.
    unknown        ///< Source could not be mapped to a known category.
};

/** Native representation used by a ZFS property. */
enum class PropertyType {
    number, ///< Numeric property represented internally as an integer.
    string, ///< String-valued property.
    index,  ///< Enumerated/index property with a printable symbolic value.
    unknown ///< Property representation could not be determined.
};

/**
 * Read-only value object describing a ZFS property.
 *
 * The same type is used for pool properties, filesystem properties, volume
 * properties, snapshot properties, and user-defined dataset properties.
 */
class Property {
public:
    /**
     * Return the property's canonical name.
     *
     * @return Reference to the name owned by this Property.
     * @throws Nothing.
     */
    const std::string& name() const noexcept;

    /**
     * Return the property's formatted value.
     *
     * @return Reference to the value owned by this Property.
     * @throws Nothing.
     */
    const std::string& value() const noexcept;

    /**
     * Return the source of the current value.
     *
     * @return The property's value source.
     * @throws Nothing.
     */
    PropertySource source() const noexcept;

    /**
     * Return the property's native value type.
     *
     * @return The property's value type.
     * @throws Nothing.
     */
    PropertyType type() const noexcept;

    /**
     * Test whether OpenZFS treats this property as read-only.
     *
     * @return true if the property cannot be set directly; false otherwise.
     * @throws Nothing.
     */
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

/** State reported for an OpenZFS pool feature. */
enum class FeatureState {
    disabled,             ///< Feature is supported but has not been enabled.
    enabled,              ///< Feature is enabled but has no active on-disk use.
    active,               ///< Feature is enabled and actively used by on-disk data.
    unsupported_inactive, ///< Unknown feature is present but not currently active.
    unsupported_readonly, ///< Unknown feature requires read-only pool access.
    unknown               ///< Feature state could not be interpreted.
};

/** Read-only description of a pool feature flag. */
class PoolFeature {
public:
    /**
     * Return the feature's short name, without the "feature@" prefix.
     *
     * @return Reference to the feature name owned by this object.
     * @throws Nothing.
     */
    const std::string& name() const noexcept;

    /**
     * Return the feature's current state.
     *
     * @return The feature state.
     * @throws Nothing.
     */
    FeatureState state() const noexcept;

    /**
     * Test whether this feature is recognized by the running OpenZFS library.
     *
     * @return true for supported features; false for unsupported feature entries.
     * @throws Nothing.
     */
    bool supported() const noexcept;

private:
    friend class Pool;

    PoolFeature(std::string name, FeatureState state, bool supported);

    std::string name_;
    FeatureState state_ = FeatureState::unknown;
    bool supported_ = true;
};

class Snapshot;

/**
 * Common base class for ZFS filesystems, volumes, and snapshots.
 *
 * Dataset objects retain the shared libzfs context but do not retain a fully
 * populated zfs_handle_t.  A handle is opened lazily when an operation needs
 * detailed dataset state.  This keeps bulk dataset enumeration inexpensive.
 */
class Dataset {
public:
    /**
     * Move-construct a dataset wrapper.
     * @param other Dataset whose wrapper state is transferred.
     * @throws Nothing.
     */
    Dataset(Dataset&& other) noexcept;

    /**
     * Move-assign a dataset wrapper.
     * @param other Dataset whose wrapper state is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    Dataset& operator=(Dataset&& other) noexcept;

    /** Dataset wrappers are intentionally non-copyable. */
    Dataset(const Dataset&) = delete;

    /** Dataset wrappers are intentionally non-copy-assignable. */
    Dataset& operator=(const Dataset&) = delete;

    /**
     * Destroy the wrapper and release its shared implementation state.
     * @throws Nothing.
     */
    virtual ~Dataset();

    /**
     * Return the full ZFS dataset name.
     *
     * @return The full dataset name, including any snapshot suffix.
     * @throws std::bad_alloc if copying the stored name fails.
     */
    std::string name() const;

    /**
     * Return the dataset GUID.
     *
     * @return The dataset's ZFS GUID property.
     * @throws zfs::Error if the dataset cannot be opened or queried.
     */
    std::uint64_t guid() const;

    /**
     * Return all visible properties for this dataset.
     *
     * Native ZFS properties applicable to the concrete dataset type and
     * user-defined dataset properties are both included.
     *
     * @return A collection of property value objects.
     * @throws zfs::Error if the dataset cannot be opened or a property cannot
     *         be queried.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<Property> properties() const;

    /**
     * Query a single dataset property by name.
     *
     * @param name Native or user-defined property name.
     * @return The requested property and its current value/source metadata.
     * @throws zfs::Error with Error::Code::not_found if the property does not
     *         exist for this dataset.
     * @throws zfs::Error if the dataset cannot be opened or queried.
     */
    Property property(const std::string& name) const;

    /**
     * Test whether this dataset is a filesystem.
     *
     * The base implementation returns false; Filesystem overrides it.
     *
     * @return true only for filesystem datasets.
     * @throws Nothing.
     */
    virtual bool is_filesystem() const noexcept;

    /**
     * Test whether this dataset is a volume (zvol).
     *
     * The base implementation returns false; Volume overrides it.
     *
     * @return true only for volume datasets.
     * @throws Nothing.
     */
    virtual bool is_volume() const noexcept;

    /**
     * Test whether this dataset is a snapshot.
     *
     * The base implementation returns false; Snapshot overrides it.
     *
     * @return true only for snapshot datasets.
     * @throws Nothing.
     */
    virtual bool is_snapshot() const noexcept;

    /**
     * List snapshots owned by this dataset.
     *
     * Filesystems and volumes use this implementation.  Snapshot overrides it
     * and returns an empty collection because snapshots cannot contain
     * snapshots.
     *
     * @return Snapshot objects belonging directly to this dataset.
     * @throws zfs::Error if the dataset cannot be opened or snapshot iteration
     *         fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    virtual std::vector<Snapshot> snapshots() const;

    /**
     * Create a snapshot of this dataset.
     *
     * Snapshot names are relative names such as "before-upgrade"; the wrapper
     * constructs the full OpenZFS snapshot name from this dataset's name.
     * Snapshot creation properties are limited to user-defined properties.
     *
     * @param snapshot_name Snapshot component name, without the dataset name or
     *        the '@' separator.
     * @param properties Optional user properties to set atomically on the new
     *        snapshot.
     * @return A Snapshot representing the newly created snapshot.
     * @throws zfs::Error with Error::Code::invalid_argument if snapshot_name is
     *         empty, this object is itself a snapshot, or a property name is not
     *         a valid ZFS user property.
     * @throws zfs::Error if OpenZFS rejects or fails snapshot creation.
     * @throws std::bad_alloc if temporary or result storage cannot be allocated.
     */
    virtual Snapshot create_snapshot(
        const std::string& snapshot_name,
        const PropertyValues& properties = {}) const;

protected:
    friend class Pool;

    class Impl;
    explicit Dataset(std::shared_ptr<Impl> impl) noexcept;

    std::shared_ptr<Impl> impl_;
};

/**
 * A mountable ZFS filesystem dataset.
 *
 * Filesystem inherits all common dataset queries, including properties and
 * snapshot enumeration, and adds filesystem-specific mount state.
 */
class Filesystem final : public Dataset {
public:
    /**
     * Move-construct a filesystem wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @throws Nothing.
     */
    Filesystem(Filesystem&& other) noexcept = default;

    /**
     * Move-assign a filesystem wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    Filesystem& operator=(Filesystem&& other) noexcept = default;

    /**
     * Identify this object as a filesystem dataset.
     * @return true.
     * @throws Nothing.
     */
    bool is_filesystem() const noexcept override;

    /**
     * Test whether the filesystem is currently mounted.
     *
     * @return true if mounted; false otherwise.
     * @throws zfs::Error if the dataset cannot be opened.
     */
    bool mounted() const;

    /**
     * Return the filesystem's effective mountpoint property.
     *
     * @return The formatted mountpoint property value.
     * @throws zfs::Error if the filesystem cannot be opened or queried.
     */
    std::string mountpoint() const;

private:
    friend class Pool;

    explicit Filesystem(std::shared_ptr<Impl> impl) noexcept;
};

/**
 * A ZFS volume (zvol) dataset.
 *
 * A volume is a block device backed by ZFS.  It shares common Dataset
 * properties and snapshots with filesystems but is not mountable as a ZFS
 * filesystem.
 */
class Volume final : public Dataset {
public:
    /**
     * Move-construct a volume wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @throws Nothing.
     */
    Volume(Volume&& other) noexcept = default;

    /**
     * Move-assign a volume wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    Volume& operator=(Volume&& other) noexcept = default;

    /**
     * Identify this object as a volume dataset.
     * @return true.
     * @throws Nothing.
     */
    bool is_volume() const noexcept override;

    /**
     * Return the logical volume size.
     *
     * @return ZFS_PROP_VOLSIZE in bytes.
     * @throws zfs::Error if the volume cannot be opened or queried.
     */
    std::uint64_t size() const;

    /**
     * Return the volume block size.
     *
     * @return ZFS_PROP_VOLBLOCKSIZE in bytes.
     * @throws zfs::Error if the volume cannot be opened or queried.
     */
    std::uint64_t block_size() const;

private:
    friend class Pool;

    explicit Volume(std::shared_ptr<Impl> impl) noexcept;
};

/**
 * A read-only ZFS snapshot dataset.
 *
 * Snapshot inherits the common Dataset query interface.  It is immutable as a
 * dataset and cannot itself own snapshots, so snapshots() always returns an
 * empty collection.  Snapshot-specific mutating operations such as clone or
 * destroy may be added separately without making Snapshot writable.
 */
class Snapshot final : public Dataset {
public:
    /**
     * Move-construct a snapshot wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @throws Nothing.
     */
    Snapshot(Snapshot&& other) noexcept = default;

    /**
     * Move-assign a snapshot wrapper.
     * @param other Wrapper whose implementation state is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    Snapshot& operator=(Snapshot&& other) noexcept = default;

    /**
     * Identify this object as a snapshot dataset.
     * @return true.
     * @throws Nothing.
     */
    bool is_snapshot() const noexcept override;

    /**
     * Return snapshots contained by this snapshot.
     *
     * Snapshots cannot themselves contain snapshots.
     *
     * @return An empty vector.
     * @throws Nothing except allocation failure permitted by std::vector.
     */
    std::vector<Snapshot> snapshots() const override;

    /**
     * Reject attempts to create a snapshot of a snapshot.
     *
     * @param snapshot_name Requested child snapshot name.
     * @param properties Requested child snapshot properties.
     * @return This method never returns.
     * @throws zfs::Error with Error::Code::invalid_argument always, because a
     *         snapshot cannot contain another snapshot.
     */
    Snapshot create_snapshot(
        const std::string& snapshot_name,
        const PropertyValues& properties = {}) const override;

    /**
     * Write a full ZFS send stream for this snapshot.
     *
     * This call is synchronous and does not return until libzfs_core has
     * finished writing the stream or an error occurs.  The caller retains
     * ownership of the file descriptor; this method never closes it.
     *
     * @param fd Open file descriptor to which the stream is written.
     * @param options Stream-format options.
     * @return Nothing.
     * @throws zfs::Error with Error::Code::invalid_argument if fd is negative.
     * @throws zfs::Error if OpenZFS cannot generate or write the send stream.
     */
    void send(int fd, const SendOptions& options = {}) const;

    /**
     * Write an incremental ZFS send stream ending at this snapshot.
     *
     * The starting snapshot must be a valid incremental ancestor of this
     * snapshot as required by OpenZFS.  Validation of the relationship is
     * performed by libzfs_core.  The caller retains ownership of fd.
     *
     * @param fd Open file descriptor to which the stream is written.
     * @param from Earlier snapshot used as the incremental starting point.
     * @param options Stream-format options.
     * @return Nothing.
     * @throws zfs::Error with Error::Code::invalid_argument if fd is negative.
     * @throws zfs::Error if the snapshots do not form a valid incremental
     *         relationship or OpenZFS cannot generate or write the stream.
     */
    void send(int fd, const Snapshot& from,
        const SendOptions& options = {}) const;

private:
    friend class Dataset;

    explicit Snapshot(std::shared_ptr<Impl> impl) noexcept;
};

/**
 * Result of enumerating filesystem and volume datasets in a pool.
 *
 * A single OpenZFS traversal populates both collections to avoid performing
 * separate expensive tree walks for filesystems and volumes.
 */
class DatasetCollection {
public:
    /** Construct an empty dataset collection. @throws Nothing. */
    DatasetCollection() = default;

    /**
     * Move-construct a dataset collection.
     * @param other Collection whose contents are transferred.
     * @throws Nothing.
     */
    DatasetCollection(DatasetCollection&& other) noexcept = default;

    /**
     * Move-assign a dataset collection.
     * @param other Collection whose contents are transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    DatasetCollection& operator=(DatasetCollection&& other) noexcept = default;

    /** Collections are non-copyable because contained Dataset wrappers are move-only. */
    DatasetCollection(const DatasetCollection&) = delete;

    /** Collections are non-copy-assignable because contained Dataset wrappers are move-only. */
    DatasetCollection& operator=(const DatasetCollection&) = delete;

    /**
     * Return all filesystem datasets discovered by the traversal.
     *
     * @return Reference to the filesystem collection owned by this object.
     * @throws Nothing.
     */
    const std::vector<Filesystem>& filesystems() const noexcept;

    /**
     * Return all volume datasets discovered by the traversal.
     *
     * @return Reference to the volume collection owned by this object.
     * @throws Nothing.
     */
    const std::vector<Volume>& volumes() const noexcept;

private:
    friend class Pool;

    std::vector<Filesystem> filesystems_;
    std::vector<Volume> volumes_;
};

/**
 * Wrapper for an imported/live ZFS storage pool.
 *
 * Pool owns a live zpool handle and exposes pool metadata, feature flags, vdev
 * topology, and the filesystems/volumes contained in the pool.  Pool objects
 * are move-only because the underlying zpool handle has unique ownership.
 */
class Pool {
public:
    /**
     * Move-construct a pool wrapper.
     * @param other Pool whose zpool-handle ownership is transferred.
     * @throws Nothing.
     */
    Pool(Pool&& other) noexcept;

    /**
     * Move-assign a pool wrapper.
     * @param other Pool whose zpool-handle ownership is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    Pool& operator=(Pool&& other) noexcept;

    /** Pool wrappers are non-copyable because they uniquely own a zpool handle. */
    Pool(const Pool&) = delete;

    /** Pool wrappers are non-copy-assignable because they uniquely own a zpool handle. */
    Pool& operator=(const Pool&) = delete;

    /**
     * Close the underlying zpool handle, if any.
     * @throws Nothing.
     */
    ~Pool();

    /**
     * Return the pool name.
     *
     * @return The imported pool's name.
     * @throws std::bad_alloc if copying the name fails.
     */
    std::string name() const;

    /**
     * Return the pool GUID.
     *
     * @return The pool's GUID property.
     * @throws Nothing from the wrapper; libzfs is queried directly.
     */
    std::uint64_t guid() const;

    /**
     * Return all native pool properties.
     *
     * Feature properties are returned separately by features().
     *
     * @return Native pool properties and their current values.
     * @throws zfs::Error if property-list construction or querying fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<Property> properties() const;

    /**
     * Return feature@ and unsupported@ entries associated with the pool.
     *
     * @return Pool feature descriptions.
     * @throws zfs::Error if feature enumeration or querying fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<PoolFeature> features() const;

    /**
     * Return the pool's public vdev topology.
     *
     * OpenZFS "hole" placeholders are omitted; missing vdevs remain visible.
     *
     * @return Top-level data, log, special, dedup, spare, and cache vdevs.
     * @throws zfs::Error if the pool configuration cannot be retrieved or is
     *         malformed.
     * @throws std::bad_alloc if topology storage cannot be allocated.
     */
    std::vector<VDev> vdevs() const;

    /**
     * Enumerate filesystems and volumes in one dataset-tree traversal.
     *
     * @return A collection containing both dataset categories.
     * @throws zfs::Error if dataset enumeration fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    DatasetCollection datasets() const;

    /**
     * Convenience wrapper returning only filesystem datasets.
     *
     * @return Filesystems discovered in the pool.
     * @throws zfs::Error if dataset enumeration fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<Filesystem> filesystems() const;

    /**
     * Convenience wrapper returning only volume datasets.
     *
     * @return Volumes discovered in the pool.
     * @throws zfs::Error if dataset enumeration fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<Volume> volumes() const;

    /**
     * Return the pool state.
     *
     * @return Pool state when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    PoolState state() const;

    /**
     * Return the pool health/status.
     *
     * @return Pool status when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    PoolStatus status() const;

    /**
     * Export this imported pool from the system.
     *
     * @return Nothing.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    void export_pool();

    /**
     * Destroy this imported pool.
     *
     * @return Nothing.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    void destroy();

private:
    friend class ZFS;

    class Impl;
    explicit Pool(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

/**
 * Description of a pool discovered on storage but not currently imported.
 *
 * ImportablePool is a copyable value object because discovery data is detached
 * from live zpool handles.
 */
class ImportablePool {
public:
    /** Copy-construct a detached import candidate. @param other Source candidate. */
    ImportablePool(const ImportablePool& other) = default;

    /** Copy-assign a detached import candidate. @param other Source candidate. @return *this. */
    ImportablePool& operator=(const ImportablePool& other) = default;

    /** Move-construct an import candidate. @param other Source candidate. @throws Nothing. */
    ImportablePool(ImportablePool&& other) noexcept = default;

    /** Move-assign an import candidate. @param other Source candidate. @return *this. @throws Nothing. */
    ImportablePool& operator=(ImportablePool&& other) noexcept = default;

    /** Destroy the detached discovery data. @throws Nothing. */
    ~ImportablePool() = default;

    /**
     * Return the name recorded in the discovered pool configuration.
     * @return Reference to the pool name owned by this object.
     * @throws Nothing.
     */
    const std::string& name() const noexcept;

    /**
     * Return the GUID recorded in the discovered pool configuration.
     * @return The discovered pool GUID.
     * @throws Nothing.
     */
    std::uint64_t guid() const noexcept;

    /**
     * Return the administrative state found during discovery.
     * @return The discovered PoolState.
     * @throws Nothing.
     */
    PoolState state() const noexcept;

    /**
     * Return the vdev topology recovered during import discovery.
     * @return Reference to the topology owned by this object.
     * @throws Nothing.
     */
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

/**
 * Top-level OpenZFS library context.
 *
 * ZFS owns the libzfs context used for pool discovery and management.  Objects
 * returned by this class retain shared internal context as necessary so their
 * lifetime is not tied to the lifetime of the originating ZFS object.
 */
class ZFS {
public:
    /**
     * Initialize libzfs.
     *
     * @throws zfs::Error with Error::Code::initialization_failed if libzfs
     *         initialization fails.
     */
    ZFS();

    /** Finalize the owned libzfs context after dependent shared state is gone. */
    ~ZFS();

    /** ZFS contexts are non-copyable. */
    ZFS(const ZFS&) = delete;

    /** ZFS contexts are non-copy-assignable. */
    ZFS& operator=(const ZFS&) = delete;

    /**
     * Move-construct a ZFS context wrapper.
     * @param other Context wrapper whose implementation is transferred.
     * @throws Nothing.
     */
    ZFS(ZFS&& other) noexcept;

    /**
     * Move-assign a ZFS context wrapper.
     * @param other Context wrapper whose implementation is transferred.
     * @return Reference to this object.
     * @throws Nothing.
     */
    ZFS& operator=(ZFS&& other) noexcept;

    /**
     * Return the OpenZFS userspace version.
     *
     * @return Userspace OpenZFS version string when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    std::string userspace_version() const;

    /**
     * Return the running kernel OpenZFS version.
     *
     * @return Kernel OpenZFS version string when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    std::string kernel_version() const;

    /**
     * Enumerate currently imported pools.
     *
     * @return Live Pool wrappers for every imported pool visible to libzfs.
     * @throws zfs::Error if libzfs pool iteration fails.
     * @throws std::bad_alloc if result storage cannot be allocated.
     */
    std::vector<Pool> pools() const;

    /**
     * Search attached storage for pools that can potentially be imported.
     *
     * @return Discovered import candidates when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    std::vector<ImportablePool> search_pools() const;

    /**
     * Import a previously discovered pool.
     *
     * @param pool Import candidate returned by search_pools().
     * @return A live Pool wrapper for the imported pool when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    Pool import_pool(const ImportablePool& pool);

    /**
     * Create a new pool.
     *
     * The vdev/configuration API is still to be designed; this placeholder
     * currently accepts only a pool name.
     *
     * @param name Name for the new pool.
     * @return A live Pool wrapper for the new pool when implemented.
     * @throws zfs::Error with Error::Code::not_implemented in the current
     *         implementation.
     */
    Pool create_pool(const std::string& name);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Convert a PropertySource to a stable printable name.
 * @param source Source value to convert.
 * @return Pointer to a static null-terminated string.
 * @throws Nothing.
 */
const char* to_string(PropertySource source) noexcept;

/**
 * Convert a PropertyType to a stable printable name.
 * @param type Property type to convert.
 * @return Pointer to a static null-terminated string.
 * @throws Nothing.
 */
const char* to_string(PropertyType type) noexcept;

/**
 * Convert a FeatureState to a stable printable name.
 * @param state Feature state to convert.
 * @return Pointer to a static null-terminated string.
 * @throws Nothing.
 */
const char* to_string(FeatureState state) noexcept;

/**
 * Convert a VDevRole to a stable printable name.
 * @param role Vdev role to convert.
 * @return Pointer to a static null-terminated string.
 * @throws Nothing.
 */
const char* to_string(VDevRole role) noexcept;

} // namespace zfs
