#pragma once

#include <string>

namespace zfs {

	class Dataset;
	class Pool;

	/** Source from which a ZFS property value was obtained. */
	enum class PropertySource {
		none,          ///< No source information is available/applicable.
		default_value, ///< Value comes from the property's built-in default.
		temporary,     ///< Value is a temporary runtime setting.
		local,         ///< Value was set directly on this object.
		inherited,     ///< Value was inherited from an ancestor dataset.
		received,      ///< Value was received, for example by replication.
		unknown        ///< Source could not be mapped to a known category.
	};

	/** Native representation used by a ZFS property. */
	enum class PropertyType {
		number, ///< Numeric property represented internally as an integer.
		string, ///< String-valued property.
		index,  ///< Enumerated/index property with a printable symbolic value.
		unknown ///< Property representation could not be determined.
	};

	/**
	 * Read-only value object describing a ZFS property.
	 *
	 * The same type is used for pool properties, filesystem properties, volume
	 * properties, snapshot properties, and user-defined dataset properties.
	 */
	class Property {
		public:
			/**
			 * Return the property's canonical name.
			 *
			 * @return Reference to the name owned by this Property.
			 * @throws Nothing.
			 */
			const std::string& name() const noexcept;

			/**
			 * Return the property's formatted value.
			 *
			 * @return Reference to the value owned by this Property.
			 * @throws Nothing.
			 */
			const std::string& value() const noexcept;

			/**
			 * Return the source of the current value.
			 *
			 * @return The property's value source.
			 * @throws Nothing.
			 */
			PropertySource source() const noexcept;

			/**
			 * Return the property's native value type.
			 *
			 * @return The property's value type.
			 * @throws Nothing.
			 */
			PropertyType type() const noexcept;

			/**
			 * Test whether OpenZFS treats this property as read-only.
			 *
			 * @return true if the property cannot be set directly; false otherwise.
			 * @throws Nothing.
			 */
			bool readonly() const noexcept;

		private:
			friend class Dataset;
			friend class Pool;

			Property(std::string name, std::string value,
					PropertySource source, PropertyType type, bool readonly);

			std::string name_;
			std::string value_;
			PropertySource source_ = PropertySource::unknown;
			PropertyType type_ = PropertyType::unknown;
			bool readonly_ = false;
	};

	/**
	 * Convert a PropertySource to a stable printable name.
	 * @param source Source value to convert.
	 * @return Pointer to a static null-terminated string.
	 * @throws Nothing.
	 */
	const char* to_string(PropertySource source) noexcept;

	/**
	 * Convert a PropertyType to a stable printable name.
	 * @param type Property type to convert.
	 * @return Pointer to a static null-terminated string.
	 * @throws Nothing.
	 */
	const char* to_string(PropertyType type) noexcept;

} // namespace zfs
