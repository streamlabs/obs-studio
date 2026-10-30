#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace DShowCapture {

inline const char *FailureReason(uint32_t code)
{
	// ERROR_CAMERA_INSUFFICIENT_BANDWIDTH. Other resource errors do not prove USB exhaustion.
	return code == 0x800718CFu ? "insufficient_bandwidth" : "start_failed";
}

inline const char *EnumerationFailureReason(std::optional<bool> present)
{
	// Activated enumeration also omits devices whose filter/capabilities cannot be opened.
	// Only a separate, successful presence query can establish that hardware is missing.
	return present.has_value() && !*present ? "device_not_found" : "start_failed";
}

// Protected by DShowInput::mutex. Generations invalidate results of superseded actions.
struct CaptureStatus {
	static constexpr uint32_t MaxReconnectAttempts = 5;

	uint64_t generation = 0;
	std::string state = "inactive";
	std::string reason;
	std::string deviceName;
	uint32_t errorCode = 0;

	bool CanReconnect() const
	{
		return state == "error" && reconnectAttemptsRemaining &&
		       (reason == "device_not_found" || reason == "start_failed");
	}
	bool CanCheckDevice() const { return state == "running" || CanReconnect(); }

	bool DeviceRemoved(uint64_t request)
	{
		if (request != generation || (state != "running" && state != "error"))
			return false;
		++generation;
		state = "error";
		reason = "device_not_found";
		errorCode = 0;
		reconnectAttemptsRemaining = MaxReconnectAttempts;
		return true;
	}

	// Keep the retained warning during an automatic retry; only success clears it.
	uint64_t Reconnect(uint64_t request)
	{
		if (request != generation || !CanReconnect())
			return 0;
		--reconnectAttemptsRemaining;
		return ++generation;
	}

	uint64_t Request(bool activate)
	{
		++generation;
		state = activate ? "starting" : "inactive";
		reason.clear();
		deviceName.clear();
		errorCode = 0;
		reconnectAttemptsRemaining = 0;
		return generation;
	}

	void Complete(uint64_t request, bool success, const std::string &device, const std::string &failure,
		      uint32_t code)
	{
		if (request != generation)
			return;
		if (!success && failure == "device_not_found")
			reconnectAttemptsRemaining = MaxReconnectAttempts;
		else if (success || failure != "start_failed")
			reconnectAttemptsRemaining = 0;
		// An empty failure means no device was selected, not a capture error.
		state = success ? "running" : failure.empty() ? "inactive" : "error";
		deviceName = device;
		reason = success ? "" : failure;
		errorCode = success ? 0 : code;
	}

private:
	// Only confirmed absence/removal arms retries. Transient startup failures during
	// that recovery consume the budget; ordinary startup and bandwidth errors do not retry.
	uint32_t reconnectAttemptsRemaining = 0;
};

} // namespace DShowCapture
