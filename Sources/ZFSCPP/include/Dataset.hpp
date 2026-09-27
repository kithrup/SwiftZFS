#pragma once

#include "Property.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <vector>
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
	class DatasetCollection;
	class SendStream;

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
			 * Look up one snapshot owned directly by this dataset.
			 *
			 * @param snapshot_name Snapshot component name, without the dataset name
			 *        or the '@' separator.
			 * @return The requested snapshot.
			 * @throws zfs::Error with Error::Code::invalid_argument if the name is
			 *         invalid or this dataset is itself a snapshot.
			 * @throws zfs::Error with Error::Code::not_found if the snapshot does not
			 *         exist.
			 * @throws zfs::Error if OpenZFS cannot open the snapshot.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			Snapshot snapshot(const std::string& snapshot_name) const;

			/**
			 * Return datasets immediately below this dataset.
			 *
			 * This operation is deliberately non-recursive.  Filesystem and volume
			 * children are returned separately in one DatasetCollection.  Snapshots
			 * have no children and return an empty collection.
			 *
			 * @return Immediate filesystem and volume children.
			 * @throws zfs::Error if the dataset cannot be opened or child iteration
			 *         fails.
			 * @throws std::bad_alloc if result storage cannot be allocated.
			 */
			DatasetCollection children() const;

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
			friend class SendStream;

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
			friend class Dataset;

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
			friend class Dataset;

			explicit Volume(std::shared_ptr<Impl> impl) noexcept;
	};

	/**
	 * Pull-based view of a ZFS send stream.
	 *
	 * A SendStream starts a background producer that asks libzfs_core to write
	 * the stream into an internal pipe.  Consumers may either iterate over the
	 * stream in fixed-size byte chunks or call read() directly.  The object is
	 * move-only and supports a single consumer.
	 *
	 * If iteration is abandoned before end-of-stream, final destruction drains
	 * the remaining producer output before joining the producer thread.  This
	 * avoids terminating the process with SIGPIPE, but means destruction can
	 * block until OpenZFS finishes producing the stream.
	 */
	class SendStream {
		public:
			using Chunk = std::vector<std::byte>;

			/** Input iterator yielding successive send-stream chunks. */
			class Iterator {
				public:
					using iterator_category = std::input_iterator_tag;
					using value_type = Chunk;
					using difference_type = std::ptrdiff_t;
					using pointer = const Chunk*;
					using reference = const Chunk&;

					/** Construct an end iterator. @throws Nothing. */
					Iterator() noexcept = default;

					/** Return the current chunk. @return Current chunk. */
					reference operator*() const noexcept;

					/** Return a pointer to the current chunk. @return Current chunk pointer. */
					pointer operator->() const noexcept;

					/**
					 * Advance to the next chunk.
					 * @return Reference to this iterator.
					 * @throws zfs::Error if stream production or reading fails.
					 */
					Iterator& operator++();

					/** Post-increment. @throws zfs::Error if advancing fails. */
					Iterator operator++(int);

					friend bool operator==(const Iterator& lhs,
							const Iterator& rhs) noexcept;
					friend bool operator!=(const Iterator& lhs,
							const Iterator& rhs) noexcept;

				private:
					friend class SendStream;
					class State;
					explicit Iterator(std::shared_ptr<State> state);
					void read_next();

					std::shared_ptr<State> state_;
					Chunk chunk_;
					bool done_ = true;
			};

			SendStream(SendStream&& other) noexcept = default;
			SendStream& operator=(SendStream&& other) noexcept = default;
			SendStream(const SendStream&) = delete;
			SendStream& operator=(const SendStream&) = delete;
			~SendStream() = default;

			/**
			 * Read raw stream bytes into a caller-provided buffer.
			 *
			 * @param buffer Destination buffer.  May be nullptr only when capacity is 0.
			 * @param capacity Maximum number of bytes to read.
			 * @return Number of bytes read, or 0 at end-of-stream.
			 * @throws zfs::Error if stream production or reading fails.
			 */
			std::size_t read(void* buffer, std::size_t capacity);

			/** @return Iterator positioned at the first stream chunk. */
			Iterator begin();

			/** @return End iterator. @throws Nothing. */
			Iterator end() noexcept;

		private:
			friend class Snapshot;
			using State = Iterator::State;
			SendStream(std::string target, std::string from,
					const SendOptions& options,
					std::shared_ptr<void> target_lifetime,
					std::shared_ptr<void> from_lifetime = {});

			std::shared_ptr<State> state_;
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
			 * Create an iterable full ZFS send stream for this snapshot.
			 *
			 * @param options Stream-format options.
			 * @return Move-only stream whose iterator yields byte chunks.
			 * @throws zfs::Error if the stream transport cannot be created.
			 */
			SendStream send(const SendOptions& options = {}) const;

			/**
			 * Create an iterable incremental send stream ending at this snapshot.
			 *
			 * @param from Earlier snapshot used as the incremental starting point.
			 * @param options Stream-format options.
			 * @return Move-only stream whose iterator yields byte chunks.
			 * @throws zfs::Error if the stream transport cannot be created.
			 */
			SendStream send(const Snapshot& from,
					const SendOptions& options = {}) const;

			/**
			 * Write a full send stream directly to a caller-owned file descriptor.
			 * @param fd Output file descriptor, which is never closed by this method.
			 * @param options Stream-format options.
			 * @throws zfs::Error if fd is invalid or OpenZFS send fails.
			 */
			void send_to(int fd, const SendOptions& options = {}) const;

			/**
			 * Write an incremental send stream directly to a file descriptor.
			 * @param fd Output file descriptor, which is never closed by this method.
			 * @param from Earlier snapshot used as the incremental starting point.
			 * @param options Stream-format options.
			 * @throws zfs::Error if fd is invalid or OpenZFS send fails.
			 */
			void send_to(int fd, const Snapshot& from,
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
			friend class Dataset;

			std::vector<Filesystem> filesystems_;
			std::vector<Volume> volumes_;
	};

} // namespace zfs
