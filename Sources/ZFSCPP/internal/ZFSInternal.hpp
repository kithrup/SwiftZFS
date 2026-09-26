#pragma once

#include "ZFS.hpp"

#include <libzfs.h>

#include <memory>
#include <string>
#include <vector>

namespace zfs {
	namespace detail {

		/**
		 * Shared owner of the libzfs and libzfs_core state used by wrapper objects.
		 *
		 * Pool and Dataset implementation objects retain a shared Context so the
		 * underlying libzfs_handle_t and reference-counted libzfs_core state remain
		 * valid even after the public ZFS object that originally created them has
		 * been destroyed.
		 */
		class Context {
			public:
				/**
				 * Initialize libzfs and libzfs_core and enable the mount-table cache.
				 *
				 * @throws zfs::Error with Error::Code::initialization_failed if
				 *         libzfs_init() or libzfs_core_init() fails.
				 */
				Context();

				/**
				 * Finalize the owned libzfs and libzfs_core state.
				 * @throws Nothing.
				 */
				~Context();

				/** Context objects uniquely own their libzfs_handle_t. */
				Context(const Context&) = delete;

				/** Context objects are not copy-assignable. */
				Context& operator=(const Context&) = delete;

				/**
				 * Return the owned raw libzfs handle.
				 *
				 * The returned pointer is borrowed and remains valid only while this
				 * Context object remains alive.
				 *
				 * @return Borrowed libzfs_handle_t pointer.
				 * @throws Nothing.
				 */
				libzfs_handle_t* handle() const noexcept;

			private:
				libzfs_handle_t* handle_ = nullptr;
				bool core_initialized_ = false;
		};

		/**
		 * Convert the current libzfs error state into a zfs::Error and throw it.
		 *
		 * @param context Context whose libzfs error state is queried.
		 * @param operation Human-readable description of the failed operation;
		 *        may be nullptr.
		 * @throws zfs::Error Always.
		 */
		[[noreturn]] void throw_libzfs_error(const Context& context,
				const char* operation);

	} // namespace detail

	/**
	 * Private implementation backing a public Pool object.
	 *
	 * Impl uniquely owns the zpool_handle_t received from zpool_iter() or a
	 * future pool operation and retains the shared libzfs Context required by
	 * that handle.
	 */
	class Pool::Impl {
		public:
			/**
			 * Adopt a live zpool handle.
			 *
			 * @param context Shared libzfs context on which the handle depends.
			 * @param handle zpool handle whose ownership is transferred to Impl.
			 * @throws zfs::Error with Error::Code::invalid_argument if either
			 *         argument is invalid.
			 */
			Impl(std::shared_ptr<detail::Context> context, zpool_handle_t* handle);

			/**
			 * Close the owned zpool handle.
			 * @throws Nothing.
			 */
			~Impl();

			/** Pool implementations uniquely own their zpool handle. */
			Impl(const Impl&) = delete;

			/** Pool implementations are not copy-assignable. */
			Impl& operator=(const Impl&) = delete;

			/**
			 * Return the owned raw zpool handle.
			 * @return Borrowed zpool_handle_t pointer.
			 * @throws Nothing.
			 */
			zpool_handle_t* handle() const noexcept;

			/**
			 * Return the shared libzfs context retained by this pool.
			 * @return Reference to the shared Context pointer.
			 * @throws Nothing.
			 */
			const std::shared_ptr<detail::Context>& context() const noexcept;

		private:
			std::shared_ptr<detail::Context> context_;
			zpool_handle_t* handle_ = nullptr;
	};

	/**
	 * Lightweight implementation shared by Dataset-derived public objects.
	 *
	 * Dataset enumeration stores only the dataset name and OpenZFS type rather
	 * than retaining a fully populated zfs_handle_t.  open() materializes a
	 * short-lived handle only when an operation needs detailed dataset state.
	 * This is important for keeping large dataset enumerations inexpensive.
	 */
	class Dataset::Impl {
		public:
			/**
			 * Construct a lightweight dataset identity.
			 *
			 * @param context Shared libzfs context used for later opens.
			 * @param name Full dataset name, including @snapshot suffix when present.
			 * @param type OpenZFS dataset type recorded during enumeration.
			 * @throws std::bad_alloc if the name or shared state cannot be stored.
			 */
			Impl(std::shared_ptr<detail::Context> context, std::string name,
					zfs_type_t type);

			/**
			 * Return the stored full dataset name.
			 * @return Reference to the name owned by this Impl.
			 * @throws Nothing.
			 */
			const std::string& name() const noexcept;

			/**
			 * Return the shared libzfs context used by this dataset.
			 * @return Reference to the shared Context pointer.
			 * @throws Nothing.
			 */
			const std::shared_ptr<detail::Context>& context() const noexcept;

			/** Return the OpenZFS dataset type recorded during enumeration. */
			zfs_type_t type() const noexcept;

			/** Add one immediate child discovered during bulk enumeration. */
			void add_child(const std::shared_ptr<Impl>& child);

			/** Return the immediate children discovered during bulk enumeration. */
			const std::vector<std::shared_ptr<Impl>>& children() const noexcept;

			/**
			 * Open a full libzfs dataset handle for the stored name and type.
			 *
			 * The caller owns the returned handle and must close it with zfs_close().
			 *
			 * @return Newly opened zfs_handle_t.
			 * @throws zfs::Error if the dataset cannot be opened.
			 */
			zfs_handle_t* open() const;

		private:
			std::shared_ptr<detail::Context> context_;
			std::string name_;
			zfs_type_t type_ = ZFS_TYPE_INVALID;
			std::vector<std::shared_ptr<Impl>> children_;
	};

} // namespace zfs
