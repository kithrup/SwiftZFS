#include "internal/ZFSInternal.hpp"

#include <cerrno>
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

			libzfs_mnttab_cache(handle_, B_TRUE);
		}

		Context::~Context()
		{
			if (handle_ != nullptr) {
				libzfs_fini(handle_);
			}
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
