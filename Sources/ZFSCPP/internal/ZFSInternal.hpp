#pragma once

#include "ZFS.hpp"

#include <libzfs.h>

#include <memory>
#include <string>

namespace zfs {
	namespace detail {

		class Context {
			public:
				Context();
				~Context();

				Context(const Context&) = delete;
				Context& operator=(const Context&) = delete;

				libzfs_handle_t* handle() const noexcept;

			private:
				libzfs_handle_t* handle_ = nullptr;
		};

		[[noreturn]] void throw_libzfs_error(const Context& context,
				const char* operation);

	} // namespace detail

	class Pool::Impl {
		public:
			Impl(std::shared_ptr<detail::Context> context, zpool_handle_t* handle);
			~Impl();

			Impl(const Impl&) = delete;
			Impl& operator=(const Impl&) = delete;

			zpool_handle_t* handle() const noexcept;
			const std::shared_ptr<detail::Context>& context() const noexcept;

		private:
			std::shared_ptr<detail::Context> context_;
			zpool_handle_t* handle_ = nullptr;
	};

	class Dataset::Impl {
		public:
			Impl(std::shared_ptr<detail::Context> context, std::string name,
					zfs_type_t type);

			const std::string& name() const noexcept;
			const std::shared_ptr<detail::Context>& context() const noexcept;
			zfs_handle_t* open() const;

		private:
			std::shared_ptr<detail::Context> context_;
			std::string name_;
			zfs_type_t type_ = ZFS_TYPE_INVALID;
	};

} // namespace zfs
