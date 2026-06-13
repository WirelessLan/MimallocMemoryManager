#include "Settings.hpp"

#include <Windows.h>

namespace
{
	bool ParseUInt32(std::string_view value, std::uint32_t& out)
	{
		int base = 10;

		if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
			value.remove_prefix(2);
			base = 16;
		}

		std::uint32_t parsed{};

		const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed, base);

		if (ec != std::errc{} || ptr != value.data() + value.size()) {
			return false;
		}

		out = parsed;

		return true;
	}

	void ApplySetting(std::string_view key, std::string_view value)
	{
		if (key == "uMaxScrapHeapSize") {
			ParseUInt32(value, Settings::MaxScrapHeapSize);
		} else if (key == "uMaxScaleformPageSize") {
			ParseUInt32(value, Settings::MaxScaleformPageSize);
		} else if (key == "uMaxScaleformHeapSize") {
			ParseUInt32(value, Settings::MaxScaleformHeapSize);
		}
	}
}

namespace Settings
{
	std::uint32_t MaxScrapHeapSize = 0x4000000;
	std::uint32_t MaxScaleformPageSize = 0x10000;
	std::uint32_t MaxScaleformHeapSize = 0x8000000;

	void Load()
	{
		char buffer[1024]{};
		const auto path = fmt::format("Data\\F4SE\\Plugins\\{}.ini", Version::PROJECT);

		GetPrivateProfileSectionA("Settings", buffer, static_cast<DWORD>(sizeof(buffer)), path.c_str());

		for (const char* entry = buffer; *entry;) {
			const auto len = std::strlen(entry);
			const std::string_view line{ entry, len };

			const auto pos = line.find('=');
			if (pos != std::string_view::npos) {
				ApplySetting(line.substr(0, pos), line.substr(pos + 1));
			}

			entry += len + 1;
		}

		logger::info("uMaxScrapHeapSize: 0x{:X}", MaxScrapHeapSize);
		logger::info("uMaxScaleformPageSize: 0x{:X}", MaxScaleformPageSize);
		logger::info("uMaxScaleformHeapSize: 0x{:X}", MaxScaleformHeapSize);
	}
}
