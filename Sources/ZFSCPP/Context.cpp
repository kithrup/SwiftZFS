#include "internal/ZFSInternal.hpp"

#include <libzfs_core.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace zfs {
	namespace detail {

		Context::Context()
			: handle_(libzfs_init())
		{
			if (handle_ == nullptr) {
				throw Error(Error::Code::initialization_failed, errno,
						"libzfs_init() failed");
			}

			/*
			 * Match the zfs(8) command-line tools: keep the libzfs mount-table
			 * cache enabled for the lifetime of the context.  Re-reading the FreeBSD
			 * mount table for each dataset handle is prohibitively expensive on
			 * systems with many datasets.
			 */
			libzfs_mnttab_cache(handle_, B_TRUE);

			const int core_error = libzfs_core_init();
			if (core_error != 0) {
				libzfs_fini(handle_);
				handle_ = nullptr;
				throw Error(Error::Code::initialization_failed, core_error,
						std::string("libzfs_core_init() failed: ") +
						std::strerror(core_error));
			}
			core_initialized_ = true;
		}

		Context::~Context()
		{
			if (core_initialized_)
				libzfs_core_fini();
			if (handle_ != nullptr)
				libzfs_fini(handle_);
		}

		libzfs_handle_t*
			Context::handle() const noexcept
			{
				return handle_;
			}

		[[noreturn]] void
			throw_libzfs_error(const Context& context, const char* operation)
			{
				libzfs_handle_t* handle = context.handle();
				const int code = libzfs_errno(handle);
				const char* description = libzfs_error_description(handle);

				std::string message(operation != nullptr ? operation : "libzfs operation");
				if (description != nullptr && description[0] != '\0') {
					message += ": ";
					message += description;
				}

				throw Error(Error::Code::unknown, code, message);
			}

	} // namespace detail
} // namespace zfs
