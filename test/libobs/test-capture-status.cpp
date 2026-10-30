#include <catch2/catch_test_macros.hpp>
#include "../../plugins/win-dshow/capture-status.hpp"

using namespace DShowCapture;

TEST_CASE("Camera failures identify only confirmed bandwidth exhaustion", "[capture-status]")
{
	CHECK(std::string(FailureReason(0x800718CFu)) == "insufficient_bandwidth");
	CHECK(std::string(FailureReason(0x800705AAu)) == "start_failed");
	CHECK(std::string(FailureReason(0x8007001Fu)) == "start_failed");
	CHECK(std::string(FailureReason(0)) == "start_failed");
}

TEST_CASE("Camera status retains failures and rejects superseded completions", "[capture-status]")
{
	CaptureStatus status;
	auto first = status.Request(true);
	status.Complete(first, false, "Camera A", FailureReason(0x800718CFu), 0x800718CFu);
	CHECK(status.state == "error");
	CHECK(status.errorCode == 0x800718CFu);
	CHECK(status.deviceName == "Camera A");

	auto second = status.Request(true);
	CHECK(status.state == "starting");
	CHECK(status.errorCode == 0);
	status.Complete(first, false, "Camera A", "start_failed", 1);
	CHECK(status.state == "starting");
	status.Complete(second, true, "Camera B", "ignored", 2);
	CHECK(status.state == "running");
	CHECK(status.reason.empty());
	CHECK(status.errorCode == 0);
	CHECK(status.deviceName == "Camera B");

	auto third = status.Request(true);
	status.Request(false);
	status.Complete(third, false, "Camera B", "start_failed", 3);
	CHECK(status.state == "inactive");
	CHECK(status.reason.empty());
	CHECK(status.deviceName.empty());

	status.Complete(status.Request(true), false, "", "", 0);
	CHECK(status.state == "inactive");
}

TEST_CASE("Camera reconnect retains the warning until recovery and respects newer requests", "[capture-status]")
{
	CaptureStatus status;
	CHECK_FALSE(status.CanCheckDevice());
	CHECK_FALSE(status.DeviceRemoved(status.generation));
	CHECK(status.Reconnect(status.generation) == 0);
	status.Complete(status.Request(true), false, "Camera A", "device_not_found", 0);
	REQUIRE(status.CanReconnect());
	const auto missing = status.generation;
	const auto retry = status.Reconnect(missing);
	REQUIRE(retry > missing);
	CHECK(status.state == "error");
	CHECK(status.reason == "device_not_found");
	CHECK(status.deviceName == "Camera A");
	CHECK(status.Reconnect(missing) == 0);
	status.Complete(retry, true, "Camera A", "", 0);
	CHECK(status.state == "running");
	CHECK(status.reason.empty());
	CHECK_FALSE(status.CanReconnect());
	REQUIRE(status.DeviceRemoved(retry));
	CHECK(status.CanReconnect());
	CHECK(status.deviceName == "Camera A");
	CHECK(status.errorCode == 0);

	const auto oldRetry = status.Reconnect(status.generation);
	status.Request(false);
	status.Complete(oldRetry, true, "Camera A", "", 0);
	CHECK(status.state == "inactive");
	CHECK_FALSE(status.DeviceRemoved(oldRetry));
	CHECK(status.Reconnect(oldRetry) == 0);

	const auto newerDevice = status.Request(true);
	CHECK_FALSE(status.DeviceRemoved(oldRetry));
	status.Complete(newerDevice, false, "Camera B", "insufficient_bandwidth", 0x800718CFu);
	CHECK_FALSE(status.CanReconnect());
	CHECK(status.Reconnect(newerDevice) == 0);
}

TEST_CASE("Camera reconnect survives transient startup failures until the driver is ready", "[capture-status]")
{
	CaptureStatus status;
	status.Complete(status.Request(true), false, "Camera A", "device_not_found", 0);
	const auto first = status.Reconnect(status.generation);
	REQUIRE(first != 0);
	status.Complete(first, false, "Camera A", FailureReason(0x8007001Fu), 0x8007001Fu);
	CHECK(status.state == "error");
	CHECK(status.reason == "start_failed");
	CHECK(status.errorCode == 0x8007001Fu);
	REQUIRE(status.CanCheckDevice());
	REQUIRE(status.CanReconnect());

	const auto second = status.Reconnect(status.generation);
	REQUIRE(second > first);
	CHECK(status.state == "error");
	CHECK(status.errorCode == 0x8007001Fu);
	status.Complete(second, true, "Camera A", "", 0);
	CHECK(status.state == "running");
	CHECK(status.reason.empty());
	CHECK(status.errorCode == 0);
	CHECK_FALSE(status.CanReconnect());
}

TEST_CASE("Camera reconnect attempts are bounded and a new removal re-arms recovery", "[capture-status]")
{
	CaptureStatus status;
	status.Complete(status.Request(true), false, "Camera A", "device_not_found", 0);
	for (int cycle = 0; cycle < 2; ++cycle) {
		for (uint32_t attempt = 0; attempt < CaptureStatus::MaxReconnectAttempts; ++attempt) {
			REQUIRE(status.CanReconnect());
			const auto retry = status.Reconnect(status.generation);
			REQUIRE(retry != 0);
			status.Complete(retry, false, "Camera A", "start_failed", 0);
		}
		CHECK(status.state == "error");
		CHECK(status.reason == "start_failed");
		CHECK_FALSE(status.CanCheckDevice());
		CHECK(status.Reconnect(status.generation) == 0);
		if (cycle == 0)
			REQUIRE(status.DeviceRemoved(status.generation));
	}
}

TEST_CASE("Camera reconnect budget is cancelled by newer requests and permanent failures", "[capture-status]")
{
	CaptureStatus status;
	status.Complete(status.Request(true), false, "Camera A", "device_not_found", 0);
	const auto retry = status.Reconnect(status.generation);
	REQUIRE(retry != 0);

	SECTION("deactivation rejects a late missing-device result")
	{
		status.Request(false);
		status.Complete(retry, false, "Camera A", "device_not_found", 0);
		CHECK(status.state == "inactive");
		CHECK_FALSE(status.CanCheckDevice());
		CHECK(status.Reconnect(status.generation) == 0);
	}
	SECTION("a different explicit activation does not inherit the retry budget")
	{
		const auto manual = status.Request(true);
		status.Complete(retry, false, "Camera A", "device_not_found", 0);
		status.Complete(manual, false, "Camera B", "start_failed", 0);
		CHECK(status.deviceName == "Camera B");
		CHECK_FALSE(status.CanCheckDevice());
		CHECK(status.Reconnect(status.generation) == 0);
	}
	SECTION("confirmed bandwidth exhaustion stops recovery immediately")
	{
		status.Complete(retry, false, "Camera A", FailureReason(0x800718CFu), 0x800718CFu);
		CHECK(status.reason == "insufficient_bandwidth");
		CHECK_FALSE(status.CanCheckDevice());
		CHECK(status.Reconnect(status.generation) == 0);
	}
}

TEST_CASE("Camera enumeration failure distinguishes absent hardware from an unopenable driver", "[capture-status]")
{
	CaptureStatus status;
	SECTION("present hardware whose filter or capabilities failed to open")
	{
		status.Complete(status.Request(true), false, "Camera A", EnumerationFailureReason(true), 0);
		CHECK(status.reason == "start_failed");
		CHECK_FALSE(status.CanCheckDevice());
	}
	SECTION("unknown presence is not evidence of a missing camera")
	{
		status.Complete(status.Request(true), false, "Camera A", EnumerationFailureReason(std::nullopt), 0);
		CHECK(status.reason == "start_failed");
		CHECK_FALSE(status.CanCheckDevice());
	}
	SECTION("confirmed absence waits for the returning camera")
	{
		status.Complete(status.Request(true), false, "Camera A", EnumerationFailureReason(false), 0);
		CHECK(status.reason == "device_not_found");
		CHECK(status.CanCheckDevice());
		CHECK(status.CanReconnect());
	}
	CHECK(status.state == "error");
	CHECK(status.deviceName == "Camera A");
	CHECK(status.errorCode == 0);
}
