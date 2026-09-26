#pragma once

#include <stdexcept>
#include <string>

namespace zfs {

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

} // namespace zfs
