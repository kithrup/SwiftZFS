#pragma once

#include "Property.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
			const std::vector<Filesystem>& filesystems() const & noexcept;

			/**
			 * Move all filesystem datasets out of a temporary collection.
			 *
			 * This overload is intended for adapters such as the C ABI that need to
			 * transfer ownership without performing another dataset traversal.
			 *
			 * @return The filesystem collection by value.
			 * @throws Nothing.
			 */
			std::vector<Filesystem> filesystems() && noexcept;

			/**
			 * Return all volume datasets discovered by the traversal.
			 *
			 * @return Reference to the volume collection owned by this object.
			 * @throws Nothing.
			 */
			const std::vector<Volume>& volumes() const & noexcept;

			/**
			 * Move all volume datasets out of a temporary collection.
			 *
			 * @return The volume collection by value.
			 * @throws Nothing.
			 */
			std::vector<Volume> volumes() && noexcept;

		private:
			friend class Pool;

			std::vector<Filesystem> filesystems_;
			std::vector<Volume> volumes_;
	};

} // namespace zfs
