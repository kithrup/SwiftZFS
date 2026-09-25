#include "ZFS.hpp"

namespace zfs {

	Error::Error(Code code, int system_error, const std::string& message)
		: std::runtime_error(message), code_(code), system_error_(system_error)
	{
	}

	Error::Code
		Error::code() const noexcept
		{
			return code_;
		}

	int
		Error::system_error() const noexcept
		{
			return system_error_;
		}

} // namespace zfs
