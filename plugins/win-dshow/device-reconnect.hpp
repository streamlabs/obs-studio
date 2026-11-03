#pragma once

#include <windows.h>
#include <cfgmgr32.h>
#include <objbase.h>

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace DShowCapture {

// PnP callbacks only wake the owning DirectShow thread. No driver/OBS calls here.
class DeviceReconnectMonitor {
	HANDLE changed;
	HCMNOTIFICATION notification = nullptr;
	std::mutex mutex;
	std::wstring devicePath;
	bool removed = false;

	static DWORD CALLBACK OnChange(HCMNOTIFICATION, void *context, CM_NOTIFY_ACTION action,
				       CM_NOTIFY_EVENT_DATA *data, DWORD)
	{
		if (action != CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL &&
		    action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
			return ERROR_SUCCESS;
		auto &self = *static_cast<DeviceReconnectMonitor *>(context);
		std::lock_guard<std::mutex> lock(self.mutex);
		if (!self.devicePath.empty() &&
		    _wcsicmp(self.devicePath.c_str(), data->u.DeviceInterface.SymbolicLink) == 0) {
			self.removed |= action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL;
			SetEvent(self.changed);
		}
		return ERROR_SUCCESS;
	}

public:
	DeviceReconnectMonitor()
	{
		changed = CreateEventW(nullptr, false, false, nullptr);
		if (!changed)
			throw "Failed to create camera device-change event";
		CM_NOTIFY_FILTER filter = {};
		filter.cbSize = sizeof(filter);
		filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
		// DirectShow devices use several interface classes; match the full selected path in OnChange.
		filter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
		CM_Register_Notification(&filter, this, OnChange, &notification);
	}

	~DeviceReconnectMonitor()
	{
		// Waits for in-flight callbacks before their context and event are released.
		if (notification)
			CM_Unregister_Notification(notification);
		CloseHandle(changed);
	}

	DeviceReconnectMonitor(const DeviceReconnectMonitor &) = delete;
	DeviceReconnectMonitor &operator=(const DeviceReconnectMonitor &) = delete;

	HANDLE Event() const { return changed; }
	bool HasNotifications() const { return notification != nullptr; }

	void SetDevicePath(const std::wstring &path)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (devicePath != path) {
			devicePath = path;
			removed = false;
		}
	}

	bool TakeRemoval()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return std::exchange(removed, false);
	}

	// Query PnP presence without opening any capture filters, including other cameras.
	// Non-PnP/virtual devices and enumeration failures are unknown, never "unplugged".
	std::optional<bool> IsPresent()
	{
		std::wstring path;
		{
			std::lock_guard<std::mutex> lock(mutex);
			path = devicePath;
		}
		const auto begin = path.find_last_of(L'{');
		const auto end = path.find(L'}', begin == std::wstring::npos ? 0 : begin);
		if (path.compare(0, 4, L"\\\\?\\") != 0 || begin == std::wstring::npos || end == std::wstring::npos)
			return std::nullopt;
		GUID category;
		if (FAILED(CLSIDFromString(path.substr(begin, end - begin + 1).c_str(), &category)))
			return std::nullopt;
		for (int attempt = 0; attempt < 3; ++attempt) {
			ULONG size = 0;
			if (CM_Get_Device_Interface_List_SizeW(&size, &category, nullptr,
							       CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
				return std::nullopt;
			std::vector<wchar_t> interfaces(size + 1, L'\0');
			const auto result = CM_Get_Device_Interface_ListW(&category, nullptr, interfaces.data(), size,
									  CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
			if (result == CR_BUFFER_SMALL)
				continue;
			if (result != CR_SUCCESS)
				return std::nullopt;
			for (const wchar_t *item = interfaces.data(); *item; item += wcslen(item) + 1) {
				if (_wcsicmp(path.c_str(), item) == 0)
					return true;
			}
			return false;
		}
		return std::nullopt;
	}
};

} // namespace DShowCapture
