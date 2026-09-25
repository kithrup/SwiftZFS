#include "internal/ZFSInternal.hpp"

#include <exception>
#include <memory>
#include <utility>

namespace zfs {

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

	std::vector<ImportablePool>
		ZFS::search_pools() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::search_pools() is not implemented yet");
		}

	Pool
		ZFS::import_pool(const ImportablePool&)
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::import_pool() is not implemented yet");
		}

	Pool
		ZFS::create_pool(const std::string&)
		{
			throw Error(Error::Code::not_implemented, 0,
					"ZFS::create_pool() is not implemented yet");
		}

} // namespace zfs
