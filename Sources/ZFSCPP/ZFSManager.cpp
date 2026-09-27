#include "internal/ZFSInternal.hpp"

#include <libzutil.h>

#include <cerrno>
#include <charconv>
#include <exception>
#include <memory>
#include <utility>

namespace zfs {

	/*
	 * ZFS owns the root libzfs context.  Pool and Dataset implementations keep
	 * shared references to detail::Context so objects returned from a ZFS
	 * instance can safely outlive the instance that discovered them.
	 */
	class ZFS::Impl {
		public:
			Impl()
				: context_(std::make_shared<detail::Context>())
			{
			}

			const std::shared_ptr<detail::Context>& context() const noexcept
			{
				return context_;
			}

		private:
			std::shared_ptr<detail::Context> context_;
	};

	ZFS::ZFS()
		: impl_(std::make_unique<Impl>())
	{
	}

	ZFS::~ZFS() = default;
	ZFS::ZFS(ZFS&&) noexcept = default;
	ZFS& ZFS::operator=(ZFS&&) noexcept = default;

	std::string
		ZFS::userspace_version() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::userspace_version() is not implemented yet");
		}

	std::string
		ZFS::kernel_version() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::kernel_version() is not implemented yet");
		}

	std::vector<Pool>
		ZFS::pools() const
		{
			struct IteratorState {
				std::shared_ptr<detail::Context> context;
				std::vector<Pool> pools;
				std::exception_ptr exception;
			} state { impl_->context(), {}, nullptr };

			auto callback = [](zpool_handle_t* handle, void* arg) -> int {
				auto* state = static_cast<IteratorState*>(arg);

				try {
					Pool pool(std::unique_ptr<Pool::Impl>(
								new Pool::Impl(state->context, handle)));
					state->pools.push_back(std::move(pool));
				} catch (...) {
					/* zpool_iter() transfers this handle to the callback. */
					zpool_close(handle);
					state->exception = std::current_exception();
					return 1;
				}

				return 0;
			};

			const int error = zpool_iter(impl_->context()->handle(), callback, &state);

			if (state.exception != nullptr)
				std::rethrow_exception(state.exception);

			if (error != 0)
				detail::throw_libzfs_error(*impl_->context(), "zpool_iter()");

			return std::move(state.pools);
		}

	Pool
		ZFS::pool(const std::string& name) const
		{
			if (name.empty() || name.find('\0') != std::string::npos) {
				throw Error(Error::Code::invalid_argument, 0,
						"pool name must not be empty or contain a NUL byte");
			}

			const auto& context = impl_->context();
			using PoolHandle = std::unique_ptr<zpool_handle_t, decltype(&zpool_close)>;
			PoolHandle handle(zpool_open_canfail(context->handle(), name.c_str()),
					&zpool_close);
			if (handle == nullptr) {
				const int code = libzfs_errno(context->handle());
				if (code == EZFS_INVALIDNAME)
					throw Error(Error::Code::invalid_argument, code,
							"invalid pool name: " + name);
				if (code == EZFS_NOENT)
					throw Error(Error::Code::no_such_pool, code,
							"pool is not imported: " + name);
				detail::throw_libzfs_error(*context, name.c_str());
			}

			auto pool_impl = std::make_unique<Pool::Impl>(context, handle.get());
			handle.release();
			return Pool(std::move(pool_impl));
		}

	std::vector<ImportablePool>
		ZFS::search_pools() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::search_pools() is not implemented yet");
		}

	Pool
		ZFS::import_pool(const std::string& name)
		{
			bool already_imported = true;
			try {
				(void) pool(name);
			} catch (const Error& error) {
				if (error.code() != Error::Code::no_such_pool)
					throw;
				already_imported = false;
			}
			if (already_imported)
				throw Error(Error::Code::already_exists, 0,
						"pool is already imported: " + name);
			return import_pool_discovered(name, 0);
		}

	Pool
		ZFS::import_pool_guid(const std::string& guid_ascii)
		{
			const char* first = guid_ascii.data();
			const char* last = first + guid_ascii.size();
			int base = 10;
			if (guid_ascii.size() >= 2 && first[0] == '0' &&
					(first[1] == 'x' || first[1] == 'X')) {
				first += 2;
				base = 16;
			}
			guid_t guid = 0;
			const auto parsed = std::from_chars(first, last, guid, base);
			if (first == last || parsed.ec != std::errc{} || parsed.ptr != last ||
					guid == 0) {
				throw Error(Error::Code::invalid_argument, 0,
						"pool GUID must be nonzero ASCII decimal or 0x-prefixed hexadecimal");
			}
			return import_pool(guid);
		}

	Pool
		ZFS::import_pool(guid_t guid)
		{
			if (guid == 0)
				throw Error(Error::Code::invalid_argument, 0,
						"pool GUID must not be zero");
			for (const auto& existing : pools()) {
				if (existing.guid() == guid)
					throw Error(Error::Code::already_exists, 0,
							"pool is already imported: " + std::to_string(guid));
			}
			return import_pool_discovered(std::to_string(guid), guid);
		}

	Pool
		ZFS::import_pool(const ImportablePool& candidate)
		{
			return import_pool(candidate.guid());
		}

	Pool
		ZFS::import_pool_discovered(const std::string& target, guid_t guid)
		{
			const auto& context = impl_->context();
			libzfs_handle_t* zfs_handle = context->handle();
			importargs_t args {};
			args.poolname = guid == 0 ? target.c_str() : nullptr;
			args.guid = guid;
			libpc_handle_t search {};
			search.lpc_lib_handle = zfs_handle;
			search.lpc_ops = &libzfs_config_ops;
			nvlist_t* raw_config = nullptr;
			const int search_error = zpool_find_config(&search, target.c_str(),
					&raw_config, &args);
			using Config = std::unique_ptr<nvlist_t, decltype(&nvlist_free)>;
			Config config(raw_config, &nvlist_free);
			if (search_error != 0) {
				if (search.lpc_open_access_error || search_error == EACCES ||
						search_error == EPERM)
					throw Error(Error::Code::permission_denied, search_error,
							"cannot search devices for pool: " + target);
				if (search_error == ENOENT)
					throw Error(Error::Code::no_such_pool, search_error,
							"no importable pool matching: " + target);
				if (search_error == EINVAL && guid == 0)
					throw Error(Error::Code::pool_unavailable, search_error,
							"multiple importable pools named " + target +
							"; import by GUID instead");
				throw Error(search_error == ENOMEM ? Error::Code::no_memory
						: Error::Code::pool_unavailable, search_error,
						"cannot find importable pool: " + target);
			}
			if (config == nullptr)
				throw Error(Error::Code::invalid_pool, 0,
						"pool search returned no configuration: " + target);

			const char* pool_name = nullptr;
			std::uint64_t found_guid = 0;
			if (nvlist_lookup_string(config.get(), ZPOOL_CONFIG_POOL_NAME,
						&pool_name) != 0 || pool_name == nullptr ||
						nvlist_lookup_uint64(config.get(), ZPOOL_CONFIG_POOL_GUID,
							&found_guid) != 0 ||
						(guid != 0 ? found_guid != guid : target != pool_name)) {
				throw Error(Error::Code::invalid_pool, 0,
						"discovered pool does not match: " + target);
			}

			if (zpool_import_props(zfs_handle, config.get(), nullptr, nullptr,
							0) != 0) {
				const int code = libzfs_errno(zfs_handle);
				Error::Code category = Error::Code::unknown;
				switch (code) {
					case EZFS_NOREPLICAS:
					case EZFS_POOLUNAVAIL: category = Error::Code::pool_unavailable; break;
					case EZFS_ACTIVE_POOL:
					case EZFS_BUSY: category = Error::Code::pool_busy; break;
					case EZFS_INVALCONFIG: category = Error::Code::invalid_pool; break;
					case EZFS_BADDEV: category = Error::Code::invalid_pool; break;
					case EZFS_PERM: category = Error::Code::permission_denied; break;
					case EZFS_IO: category = Error::Code::io_error; break;
					case EZFS_NOMEM: category = Error::Code::no_memory; break;
					case EZFS_NOENT: category = Error::Code::no_such_pool; break;
					case EZFS_BADVERSION: category = Error::Code::unsupported; break;
					case EZFS_EXISTS: category = Error::Code::already_exists; break;
					default: break;
				}
				std::string message = "cannot import pool " + target;
				const char* description = libzfs_error_description(zfs_handle);
				if (description != nullptr && description[0] != '\0')
					message += ": " + std::string(description);
				throw Error(category, code, message);
			}
			return pool(pool_name);
		}

	Pool
		ZFS::create_pool(const std::string&)
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::create_pool() is not implemented yet");
		}

} // namespace zfs
