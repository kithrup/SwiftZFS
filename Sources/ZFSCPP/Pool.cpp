#include "internal/ZFSInternal.hpp"

#include <utility>

namespace zfs {

	Pool::Impl::Impl(std::shared_ptr<detail::Context> context, zpool_handle_t* handle)
		: context_(std::move(context)), handle_(handle)
	{
		if (context_ == nullptr || handle_ == nullptr) {
			throw Error(Error::Code::invalid_argument, 0,
					"invalid pool implementation");
		}
	}

	Pool::Impl::~Impl()
	{
		if (handle_ != nullptr) {
			zpool_close(handle_);
		}
	}

	zpool_handle_t*
		Pool::Impl::handle() const noexcept
		{
			return handle_;
		}

	const std::shared_ptr<detail::Context>&
		Pool::Impl::context() const noexcept
		{
			return context_;
		}

	Pool::Pool(std::unique_ptr<Impl> impl) noexcept
		: impl_(std::move(impl))
		{
		}

	Pool::Pool(Pool&&) noexcept = default;
	Pool& Pool::operator=(Pool&&) noexcept = default;
	Pool::~Pool() = default;

	std::string
		Pool::name() const
		{
			const char* value = zpool_get_name(impl_->handle());
			return value != nullptr ? std::string(value) : std::string();
		}

	std::uint64_t
		Pool::guid() const
		{
			return static_cast<std::uint64_t>(
					zpool_get_prop_int(impl_->handle(), ZPOOL_PROP_GUID, nullptr));
		}

	PoolState
		Pool::state() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"Pool::state() is not implemented yet");
		}

	PoolStatus
		Pool::status() const
		{
			throw Error(Error::Code::not_implemented, 0,
					"Pool::status() is not implemented yet");
		}

	void
		Pool::export_pool()
		{
			throw Error(Error::Code::not_implemented, 0,
					"Pool::export_pool() is not implemented yet");
		}

	void
		Pool::destroy()
		{
			throw Error(Error::Code::not_implemented, 0,
					"Pool::destroy() is not implemented yet");
		}

	ImportablePool::ImportablePool(std::string name, std::uint64_t guid,
			PoolState state, std::vector<VDev> vdevs)
		: name_(std::move(name)), guid_(guid), state_(state),
		vdevs_(std::move(vdevs))
	{
	}

	const std::string&
		ImportablePool::name() const noexcept
		{
			return name_;
		}

	std::uint64_t
		ImportablePool::guid() const noexcept
		{
			return guid_;
		}

	PoolState
		ImportablePool::state() const noexcept
		{
			return state_;
		}

	const std::vector<VDev>&
		ImportablePool::vdevs() const noexcept
		{
			return vdevs_;
		}

} // namespace zfs
