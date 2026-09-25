#include "internal/ZFSInternal.hpp"

#include <zfs_prop.h>

#include <cstring>
#include <utility>

namespace zfs {

	Property::Property(std::string name, std::string value,
			PropertySource source, PropertyType type, bool readonly)
		: name_(std::move(name)), value_(std::move(value)), source_(source),
		type_(type), readonly_(readonly)
	{
	}

	const std::string&
		Property::name() const noexcept
		{
			return name_;
		}

	const std::string&
		Property::value() const noexcept
		{
			return value_;
		}

	PropertySource
		Property::source() const noexcept
		{
			return source_;
		}

	PropertyType
		Property::type() const noexcept
		{
			return type_;
		}

	bool
		Property::readonly() const noexcept
		{
			return readonly_;
		}

	PoolFeature::PoolFeature(std::string name, FeatureState state, bool supported)
		: name_(std::move(name)), state_(state), supported_(supported)
	{
	}

	const std::string&
		PoolFeature::name() const noexcept
		{
			return name_;
		}

	FeatureState
		PoolFeature::state() const noexcept
		{
			return state_;
		}

	bool
		PoolFeature::supported() const noexcept
		{
			return supported_;
		}

	const char*
		to_string(PropertySource source) noexcept
		{
			switch (source) {
				case PropertySource::none: return "none";
				case PropertySource::default_value: return "default";
				case PropertySource::temporary: return "temporary";
				case PropertySource::local: return "local";
				case PropertySource::inherited: return "inherited";
				case PropertySource::received: return "received";
				case PropertySource::unknown: return "unknown";
			}
			return "unknown";
		}

	const char*
		to_string(PropertyType type) noexcept
		{
			switch (type) {
				case PropertyType::number: return "number";
				case PropertyType::string: return "string";
				case PropertyType::index: return "index";
				case PropertyType::unknown: return "unknown";
			}
			return "unknown";
		}

	const char*
		to_string(FeatureState state) noexcept
		{
			switch (state) {
				case FeatureState::disabled: return "disabled";
				case FeatureState::enabled: return "enabled";
				case FeatureState::active: return "active";
				case FeatureState::unsupported_inactive: return "inactive";
				case FeatureState::unsupported_readonly: return "readonly";
				case FeatureState::unknown: return "unknown";
			}
			return "unknown";
		}

	namespace {

		PropertySource
			property_source(zprop_source_t source) noexcept
			{
				switch (source) {
					case ZPROP_SRC_NONE: return PropertySource::none;
					case ZPROP_SRC_DEFAULT: return PropertySource::default_value;
					case ZPROP_SRC_TEMPORARY: return PropertySource::temporary;
					case ZPROP_SRC_LOCAL: return PropertySource::local;
					case ZPROP_SRC_INHERITED: return PropertySource::inherited;
					case ZPROP_SRC_RECEIVED: return PropertySource::received;
					default: return PropertySource::unknown;
				}
			}

		PropertyType
			property_type(zpool_prop_t prop) noexcept
			{
				switch (zpool_prop_get_type(prop)) {
					case PROP_TYPE_NUMBER: return PropertyType::number;
					case PROP_TYPE_STRING: return PropertyType::string;
					case PROP_TYPE_INDEX: return PropertyType::index;
					default: return PropertyType::unknown;
				}
			}

		FeatureState
			feature_state(const char* value, bool supported) noexcept
			{
				if (value == nullptr)
					return FeatureState::unknown;
				if (std::strcmp(value, ZFS_FEATURE_DISABLED) == 0)
					return FeatureState::disabled;
				if (std::strcmp(value, ZFS_FEATURE_ENABLED) == 0)
					return FeatureState::enabled;
				if (std::strcmp(value, ZFS_FEATURE_ACTIVE) == 0)
					return FeatureState::active;
				if (!supported && std::strcmp(value, ZFS_UNSUPPORTED_INACTIVE) == 0)
					return FeatureState::unsupported_inactive;
				if (!supported && std::strcmp(value, ZFS_UNSUPPORTED_READONLY) == 0)
					return FeatureState::unsupported_readonly;
				return FeatureState::unknown;
			}

		class PropertyList {
			public:
				explicit PropertyList(libzfs_handle_t* handle)
				{
					char all[] = "all";
					if (zprop_get_list(handle, all, &head_, ZFS_TYPE_POOL) != 0) {
						throw Error(Error::Code::unknown, libzfs_errno(handle),
								"zprop_get_list(all) failed");
					}
				}

				~PropertyList()
				{
					if (head_ != nullptr)
						zprop_free_list(head_);
				}

				PropertyList(const PropertyList&) = delete;
				PropertyList& operator=(const PropertyList&) = delete;

				void expand(zpool_handle_t* handle)
				{
					if (zpool_expand_proplist(handle, &head_, ZFS_TYPE_POOL, B_FALSE) != 0) {
						throw Error(Error::Code::unknown,
								libzfs_errno(zpool_get_handle(handle)),
								"zpool_expand_proplist() failed");
					}
				}

				zprop_list_t* head() const noexcept { return head_; }

			private:
				zprop_list_t* head_ = nullptr;
		};

	} // namespace

	std::vector<Property>
		Pool::properties() const
		{
			PropertyList list(impl_->context()->handle());
			list.expand(impl_->handle());

			std::vector<Property> result;
			char value[ZPOOL_MAXPROPLEN];

			for (zprop_list_t* item = list.head(); item != nullptr; item = item->pl_next) {
				if (item->pl_user_prop != nullptr || item->pl_prop == ZPOOL_PROP_INVAL)
					continue;

				const auto prop = static_cast<zpool_prop_t>(item->pl_prop);
				const char* prop_name = zpool_prop_to_name(prop);
				if (prop_name == nullptr)
					continue;

				zprop_source_t source = ZPROP_SRC_NONE;
				if (zpool_get_prop(impl_->handle(), prop, value, sizeof(value),
							&source, B_FALSE) != 0) {
					detail::throw_libzfs_error(*impl_->context(), prop_name);
				}

				Property property(prop_name, value, property_source(source),
						property_type(prop), zpool_prop_readonly(prop) != B_FALSE);
				result.push_back(std::move(property));
			}

			return result;
		}

	std::vector<PoolFeature>
		Pool::features() const
		{
			PropertyList list(impl_->context()->handle());
			list.expand(impl_->handle());

			std::vector<PoolFeature> result;
			char value[ZPOOL_MAXPROPLEN];

			for (zprop_list_t* item = list.head(); item != nullptr; item = item->pl_next) {
				if (item->pl_user_prop == nullptr)
					continue;

				const char* prop_name = item->pl_user_prop;
				const bool supported = zpool_prop_feature(prop_name) != B_FALSE;
				const bool unsupported = zpool_prop_unsupported(prop_name) != B_FALSE;
				if (!supported && !unsupported)
					continue;

				if (zpool_prop_get_feature(impl_->handle(), prop_name, value,
							sizeof(value)) != 0) {
					detail::throw_libzfs_error(*impl_->context(), prop_name);
				}

				const char* at = std::strchr(prop_name, '@');
				const char* short_name = at != nullptr ? at + 1 : prop_name;
				PoolFeature feature(short_name, feature_state(value, supported),
						supported);
				result.push_back(std::move(feature));
			}

			return result;
		}

} // namespace zfs
