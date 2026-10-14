#include <catch2/catch_test_macros.hpp>

#include <obs-data.h>
#include <obs-properties.h>
#include <util/bmem.h>

#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using Properties = std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)>;
using Settings = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;

struct AllocationBalance {
	const long before = bnum_allocs();
	~AllocationBalance() { CHECK(bnum_allocs() == before); }
};

struct ModificationProbe {
	int calls = 0;
	std::function<void(obs_properties_t *, obs_property_t *, obs_data_t *)> action;
};

bool probeModified(void *priv, obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	auto &probe = *static_cast<ModificationProbe *>(priv);
	++probe.calls;
	if (probe.action)
		probe.action(props, property, settings);
	return true;
}

Properties makeProperties()
{
	return Properties(obs_properties_create(), obs_properties_destroy);
}

Settings makeSettings()
{
	return Settings(obs_data_create(), obs_data_release);
}

void expectFrameRate(media_frames_per_second actual, media_frames_per_second expected)
{
	CHECK(actual.numerator == expected.numerator);
	CHECK(actual.denominator == expected.denominator);
}

TEST_CASE("Properties reject duplicate names throughout an incoming group", "[properties][group]")
{
	auto root = makeProperties();
	auto incoming = makeProperties();
	REQUIRE(root);
	REQUIRE(incoming);
	obs_property_t *existing = obs_properties_add_bool(root.get(), "shared", "Existing property");
	REQUIRE(existing);

	SECTION("Direct duplicate")
	{
		REQUIRE(obs_properties_add_bool(incoming.get(), "shared", "Incoming property"));
	}
	SECTION("Duplicate inside a nested group")
	{
		auto nested = makeProperties();
		REQUIRE(nested);
		REQUIRE(obs_properties_add_bool(nested.get(), "shared", "Nested property"));
		REQUIRE(obs_properties_add_group(incoming.get(), "nested", "Nested group", OBS_GROUP_NORMAL,
						 nested.get()));
		nested.release();
	}
	SECTION("Duplicate inside multiple nested groups")
	{
		auto leaf = makeProperties();
		auto middle = makeProperties();
		REQUIRE(leaf);
		REQUIRE(middle);
		REQUIRE(obs_properties_add_bool(leaf.get(), "shared", "Deep property"));
		REQUIRE(obs_properties_add_group(middle.get(), "leaf", "Leaf group", OBS_GROUP_NORMAL, leaf.get()));
		leaf.release();
		REQUIRE(obs_properties_add_group(incoming.get(), "middle", "Middle group", OBS_GROUP_NORMAL,
						 middle.get()));
		middle.release();
	}

	obs_property_t *group =
		obs_properties_add_group(root.get(), "incoming", "Incoming group", OBS_GROUP_NORMAL, incoming.get());
	// A successful add transfers ownership, even when it violates the expected rejection.
	if (group)
		incoming.release();
	REQUIRE(group == nullptr);
	CHECK(obs_properties_get_parent(incoming.get()) == nullptr);
	CHECK(obs_properties_get(root.get(), "incoming") == nullptr);
	CHECK(obs_properties_get(root.get(), "shared") == existing);
	CHECK(obs_properties_get(incoming.get(), "shared") != nullptr);
}

TEST_CASE("Properties reject a group name that matches an incoming descendant", "[properties][group]")
{
	auto root = makeProperties();
	auto incoming = makeProperties();
	REQUIRE(root);
	REQUIRE(incoming);
	REQUIRE(obs_properties_add_bool(incoming.get(), "incoming", "Colliding property"));

	SECTION("Direct child") {}
	SECTION("Nested child")
	{
		auto wrapper = makeProperties();
		REQUIRE(wrapper);
		REQUIRE(obs_properties_add_group(wrapper.get(), "nested", "Nested group", OBS_GROUP_NORMAL,
						 incoming.get()));
		incoming.release();
		incoming = std::move(wrapper);
	}

	obs_property_t *group =
		obs_properties_add_group(root.get(), "incoming", "Incoming group", OBS_GROUP_NORMAL, incoming.get());
	if (group)
		incoming.release();
	REQUIRE(group == nullptr);
	CHECK(obs_properties_first(root.get()) == nullptr);
	CHECK(obs_properties_get_parent(incoming.get()) == nullptr);
	CHECK(obs_properties_get(incoming.get(), "incoming") != nullptr);

	// Rejection leaves the caller free to attach the same tree under a unique name.
	obs_properties_t *content = incoming.get();
	group = obs_properties_add_group(root.get(), "unique", "Unique group", OBS_GROUP_NORMAL, content);
	REQUIRE(group);
	incoming.release();
	CHECK(obs_property_group_content(group) == content);
	CHECK(obs_properties_get_parent(content) == root.get());
}

TEST_CASE("Properties release button URLs after replacement or clearing", "[properties][button][ownership]")
{
	const long allocationsBefore = bnum_allocs();
	{
		auto properties = makeProperties();
		REQUIRE(properties);
		obs_property_t *button = obs_properties_add_button(properties.get(), "help", "Help", nullptr);
		REQUIRE(button);
		obs_property_button_set_type(button, OBS_BUTTON_URL);
		char first[] = "https://example.com/first";
		obs_property_button_set_url(button, first);
		first[0] = 'X';
		REQUIRE(obs_property_button_url(button));
		CHECK(std::string(obs_property_button_url(button)) == "https://example.com/first");

		SECTION("Replace URL")
		{
			char second[] = "https://example.com/second";
			obs_property_button_set_url(button, second);
			REQUIRE(obs_property_button_url(button));
			CHECK(std::string(obs_property_button_url(button)) == second);
		}
		SECTION("Clear URL")
		{
			obs_property_button_set_url(button, nullptr);
			CHECK(obs_property_button_url(button) == nullptr);
		}
		SECTION("Reuse the current URL")
		{
			obs_property_button_set_url(button, const_cast<char *>(obs_property_button_url(button)));
			REQUIRE(obs_property_button_url(button));
			CHECK(std::string(obs_property_button_url(button)) == "https://example.com/first");
		}
	}
	CHECK(bnum_allocs() == allocationsBefore);
}

TEST_CASE("Properties button callbacks resolve the outermost group", "[properties][button][callback]")
{
	AllocationBalance allocations;
	struct CallbackState {
		int calls = 0;
		obs_properties_t *properties = nullptr;
		obs_property_t *property = nullptr;
		void *data = nullptr;
		bool refresh = false;
	} state;
	auto root = makeProperties();
	auto middle = makeProperties();
	auto leaf = makeProperties();
	REQUIRE(root);
	REQUIRE(middle);
	REQUIRE(leaf);
	obs_properties_set_param(root.get(), &state, nullptr);
	const auto callback = +[](obs_properties_t *props, obs_property_t *property, void *data) {
		auto *callbackState = static_cast<CallbackState *>(obs_properties_get_param(props));
		if (!callbackState)
			return false;
		++callbackState->calls;
		callbackState->properties = props;
		callbackState->property = property;
		callbackState->data = data;
		return callbackState->refresh;
	};
	obs_property_t *button = nullptr;
	void *expectedData = nullptr;
	SECTION("Legacy button")
	{
		button = obs_properties_add_button(leaf.get(), "button", "Button", callback);
	}
	SECTION("Explicit non-null private data")
	{
		button = obs_properties_add_button2(leaf.get(), "button", "Button", callback, &state);
		expectedData = &state;
	}
	SECTION("Explicit null private data")
	{
		button = obs_properties_add_button2(leaf.get(), "button", "Button", callback, nullptr);
	}
	REQUIRE(button);
	REQUIRE(obs_properties_add_group(middle.get(), "inner", "Inner", OBS_GROUP_NORMAL, leaf.get()));
	leaf.release();
	REQUIRE(obs_properties_add_group(root.get(), "outer", "Outer", OBS_GROUP_NORMAL, middle.get()));
	middle.release();

	CHECK_FALSE(obs_property_button_clicked(button, nullptr));
	CHECK(state.calls == 1);
	CHECK(state.properties == root.get());
	CHECK(state.property == button);
	CHECK(state.data == expectedData);
	state.refresh = true;
	CHECK(obs_property_button_clicked(button, nullptr));
	CHECK(state.calls == 2);
	CHECK(state.properties == root.get());
	CHECK(state.property == button);
	CHECK(state.data == expectedData);
}

TEST_CASE("Properties button clicks reject missing callbacks and other property types", "[properties][button]")
{
	AllocationBalance allocations;
	auto properties = makeProperties();
	REQUIRE(properties);
	int privateData = 0;
	obs_property_t *legacy = obs_properties_add_button(properties.get(), "legacy", "Legacy", nullptr);
	obs_property_t *privateButton =
		obs_properties_add_button2(properties.get(), "private", "Private", nullptr, &privateData);
	obs_property_t *nullPrivate = obs_properties_add_button2(properties.get(), "null", "Null", nullptr, nullptr);
	obs_property_t *nonButton = obs_properties_add_bool(properties.get(), "bool", "Boolean");
	REQUIRE(legacy);
	REQUIRE(privateButton);
	REQUIRE(nullPrivate);
	REQUIRE(nonButton);
	CHECK_FALSE(obs_property_button_clicked(nullptr, nullptr));
	CHECK_FALSE(obs_property_button_clicked(legacy, nullptr));
	CHECK_FALSE(obs_property_button_clicked(privateButton, nullptr));
	CHECK_FALSE(obs_property_button_clicked(nullPrivate, nullptr));
	CHECK_FALSE(obs_property_button_clicked(nonButton, nullptr));
}

TEST_CASE("Properties apply settings skip callbacks removed by earlier callbacks", "[properties][callback][lifetime]")
{
	struct CallbackState {
		int calls = 0;
		const char *otherName;
	};
	CallbackState leftState{0, "right"};
	CallbackState rightState{0, "left"};
	auto properties = makeProperties();
	std::unique_ptr<obs_data_t, decltype(&obs_data_release)> settings(obs_data_create(), obs_data_release);
	REQUIRE(properties);
	REQUIRE(settings);
	obs_property_t *left = obs_properties_add_bool(properties.get(), "left", "Left property");
	obs_property_t *right = obs_properties_add_bool(properties.get(), "right", "Right property");
	REQUIRE(left);
	REQUIRE(right);
	const auto callback = +[](void *priv, obs_properties_t *props, obs_property_t *, obs_data_t *) {
		auto &state = *static_cast<CallbackState *>(priv);
		++state.calls;
		obs_properties_remove_by_name(props, state.otherName);
		return true; // Removal requires a property layout refresh.
	};
	obs_property_set_modified_callback2(left, callback, &leftState);
	obs_property_set_modified_callback2(right, callback, &rightState);

	// Either callback may run first; it removes the other before that callback can run.
	obs_properties_apply_settings(properties.get(), settings.get());
	CAPTURE(leftState.calls, rightState.calls);
	CHECK(leftState.calls + rightState.calls == 1);
	const bool leftPresent = obs_properties_get(properties.get(), "left") != nullptr;
	const bool rightPresent = obs_properties_get(properties.get(), "right") != nullptr;
	CHECK(leftPresent != rightPresent);
}

TEST_CASE("Properties apply settings preserve callback order and arguments", "[properties][callback]")
{
	AllocationBalance balance;
	std::vector<std::string> calls;
	bool validArguments = true;
	ModificationProbe firstProbe, nestedFirstProbe, nestedLastProbe, groupProbe, lastProbe, ignoredLastProbe;
	auto properties = makeProperties();
	auto group = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(group);
	REQUIRE(settings);
	obs_data_set_int(settings.get(), "value", 42);
	const auto record = [&](obs_properties_t *props, obs_property_t *property, obs_data_t *data) {
		validArguments &= props == properties.get() && data == settings.get();
		validArguments &= obs_data_get_int(data, "value") == 42;
		calls.emplace_back(obs_property_name(property));
	};
	for (auto *probe : {&firstProbe, &nestedFirstProbe, &nestedLastProbe, &groupProbe, &lastProbe})
		probe->action = record;
	obs_property_t *first = obs_properties_add_bool(properties.get(), "first", "First");
	obs_property_t *nestedFirst = obs_properties_add_bool(group.get(), "nested_first", "Nested first");
	obs_property_t *nestedLast = obs_properties_add_bool(group.get(), "nested_last", "Nested last");
	REQUIRE(first);
	REQUIRE(nestedFirst);
	REQUIRE(nestedLast);
	obs_property_set_modified_callback2(first, probeModified, &firstProbe);
	obs_property_set_modified_callback2(nestedFirst, probeModified, &nestedFirstProbe);
	obs_property_set_modified_callback2(nestedLast, probeModified, &nestedLastProbe);
	obs_property_t *container =
		obs_properties_add_group(properties.get(), "group", "Group", OBS_GROUP_NORMAL, group.get());
	REQUIRE(container);
	group.release();
	obs_property_set_modified_callback2(container, probeModified, &groupProbe);
	obs_property_t *last = obs_properties_add_bool(properties.get(), "last", "Last");
	REQUIRE(last);
	obs_property_set_modified_callback2(last, probeModified, &ignoredLastProbe);

	// A surviving property's original callback retains precedence over modified2.
	obs_properties_set_param(properties.get(), &lastProbe, nullptr);
	const auto original = +[](obs_properties_t *props, obs_property_t *property, obs_data_t *data) {
		return probeModified(obs_properties_get_param(props), props, property, data);
	};
	obs_property_set_modified_callback(last, original);
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(validArguments);
	CHECK(calls == std::vector<std::string>{"last", "group", "nested_last", "nested_first", "first"});
	CHECK(lastProbe.calls == 1);
	CHECK(ignoredLastProbe.calls == 0);
	CHECK(groupProbe.calls == 1);
}

TEST_CASE("Properties apply settings skip removed groups and their queued descendants",
	  "[properties][callback][group][lifetime]")
{
	AllocationBalance balance;
	ModificationProbe childProbe, groupProbe, driverProbe, survivorProbe;
	auto properties = makeProperties();
	auto group = makeProperties();
	auto nested = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(group);
	REQUIRE(nested);
	REQUIRE(settings);
	obs_property_t *survivor = obs_properties_add_bool(properties.get(), "survivor", "Survivor");
	obs_property_t *child = obs_properties_add_bool(nested.get(), "child", "Child");
	REQUIRE(survivor);
	REQUIRE(child);
	obs_property_set_modified_callback2(survivor, probeModified, &survivorProbe);
	obs_property_set_modified_callback2(child, probeModified, &childProbe);
	REQUIRE(obs_properties_add_group(group.get(), "nested", "Nested", OBS_GROUP_NORMAL, nested.get()));
	nested.release();
	obs_property_t *container =
		obs_properties_add_group(properties.get(), "group", "Group", OBS_GROUP_NORMAL, group.get());
	REQUIRE(container);
	group.release();
	obs_property_set_modified_callback2(container, probeModified, &groupProbe);
	obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
	REQUIRE(driver);
	driverProbe.action = [](obs_properties_t *props, obs_property_t *, obs_data_t *) {
		obs_properties_remove_by_name(props, "group");
	};
	obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(driverProbe.calls == 1);
	CHECK(survivorProbe.calls == 1);
	CHECK(groupProbe.calls == 0);
	CHECK(childProbe.calls == 0);
	CHECK(obs_properties_get(properties.get(), "group") == nullptr);
	CHECK(obs_properties_get(properties.get(), "child") == nullptr);
}

TEST_CASE("Properties apply settings defer same named replacements from independent trees",
	  "[properties][callback][identity]")
{
	AllocationBalance balance;
	for (size_t round = 0; round < 128; ++round) {
		CAPTURE(round);
		ModificationProbe oldChildProbe, oldGroupProbe, replacementProbe, replacementGroupProbe, driverProbe;
		auto properties = makeProperties();
		auto original = makeProperties();
		auto replacement = makeProperties();
		auto settings = makeSettings();
		REQUIRE(properties);
		REQUIRE(original);
		REQUIRE(replacement);
		REQUIRE(settings);
		obs_property_t *oldChild = obs_properties_add_bool(original.get(), "target", "Old target");
		obs_property_t *newChild = obs_properties_add_bool(replacement.get(), "target", "Replacement target");
		REQUIRE(oldChild);
		REQUIRE(newChild);
		obs_property_set_modified_callback2(oldChild, probeModified, &oldChildProbe);
		obs_property_set_modified_callback2(newChild, probeModified, &replacementProbe);
		obs_property_t *oldGroup = obs_properties_add_group(properties.get(), "container", "Container",
								    OBS_GROUP_NORMAL, original.get());
		REQUIRE(oldGroup);
		original.release();
		obs_property_set_modified_callback2(oldGroup, probeModified, &oldGroupProbe);
		obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
		REQUIRE(driver);
		bool attached = false;
		driverProbe.action = [&](obs_properties_t *props, obs_property_t *property, obs_data_t *) {
			obs_properties_remove_by_name(props, "container");
			obs_property_t *newGroup = obs_properties_add_group(props, "container", "Replacement container",
									    OBS_GROUP_NORMAL, replacement.get());
			attached = newGroup != nullptr;
			if (newGroup) {
				replacement.release();
				obs_property_set_modified_callback2(newGroup, probeModified, &replacementGroupProbe);
			}
			obs_property_set_modified_callback2(property, nullptr, nullptr);
		};
		obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
		obs_properties_apply_settings(properties.get(), settings.get());
		REQUIRE(attached);
		CHECK(driverProbe.calls == 1);
		CHECK(oldChildProbe.calls == 0);
		CHECK(oldGroupProbe.calls == 0);
		CHECK(replacementProbe.calls == 0);
		CHECK(replacementGroupProbe.calls == 0);
		CHECK(obs_properties_get(properties.get(), "target") == newChild);
		obs_properties_apply_settings(properties.get(), settings.get());
		CHECK(replacementProbe.calls == 1);
		CHECK(replacementGroupProbe.calls == 1);
	}
}

TEST_CASE("Properties apply settings allow removal of the active property or its group",
	  "[properties][callback][lifetime]")
{
	AllocationBalance balance;
	ModificationProbe activeProbe, siblingProbe, survivorProbe;
	auto properties = makeProperties();
	auto group = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(group);
	REQUIRE(settings);
	obs_property_t *survivor = obs_properties_add_bool(properties.get(), "survivor", "Survivor");
	REQUIRE(survivor);
	obs_property_set_modified_callback2(survivor, probeModified, &survivorProbe);

	SECTION("Active property")
	{
		obs_property_t *active = obs_properties_add_bool(properties.get(), "active", "Active");
		REQUIRE(active);
		activeProbe.action = [](obs_properties_t *props, obs_property_t *, obs_data_t *) {
			obs_properties_remove_by_name(props, "active");
		};
		obs_property_set_modified_callback2(active, probeModified, &activeProbe);
	}
	SECTION("Group containing the active property")
	{
		obs_property_t *sibling = obs_properties_add_bool(group.get(), "sibling", "Sibling");
		obs_property_t *active = obs_properties_add_bool(group.get(), "active", "Active");
		REQUIRE(sibling);
		REQUIRE(active);
		obs_property_set_modified_callback2(sibling, probeModified, &siblingProbe);
		activeProbe.action = [](obs_properties_t *props, obs_property_t *, obs_data_t *) {
			obs_properties_remove_by_name(props, "group");
		};
		obs_property_set_modified_callback2(active, probeModified, &activeProbe);
		REQUIRE(obs_properties_add_group(properties.get(), "group", "Group", OBS_GROUP_NORMAL, group.get()));
		group.release();
	}
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(activeProbe.calls == 1);
	CHECK(survivorProbe.calls == 1);
	CHECK(siblingProbe.calls == 0);
	CHECK(obs_properties_get(properties.get(), "active") == nullptr);
	CHECK(obs_properties_get(properties.get(), "group") == nullptr);
}

TEST_CASE("Properties apply settings defer new properties until another pass", "[properties][callback]")
{
	AllocationBalance balance;
	ModificationProbe driverProbe, addedProbe, addedChildProbe;
	auto properties = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(settings);
	obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
	REQUIRE(driver);
	bool added = false;
	driverProbe.action = [&](obs_properties_t *props, obs_property_t *property, obs_data_t *) {
		obs_property_t *late = obs_properties_add_bool(props, "late", "Added property");
		auto group = makeProperties();
		obs_property_t *child = obs_properties_add_bool(group.get(), "late_child", "Added child");
		obs_property_set_modified_callback2(late, probeModified, &addedProbe);
		obs_property_set_modified_callback2(child, probeModified, &addedChildProbe);
		obs_property_t *container =
			obs_properties_add_group(props, "late_group", "Added group", OBS_GROUP_NORMAL, group.get());
		added = late && child && container;
		if (container)
			group.release();
		obs_property_set_modified_callback2(property, nullptr, nullptr);
	};
	obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
	obs_properties_apply_settings(properties.get(), settings.get());
	REQUIRE(added);
	CHECK(driverProbe.calls == 1);
	CHECK(addedProbe.calls == 0);
	CHECK(addedChildProbe.calls == 0);
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(addedProbe.calls == 1);
	CHECK(addedChildProbe.calls == 1);
}

TEST_CASE("Properties apply settings use the current handler on surviving properties", "[properties][callback]")
{
	AllocationBalance balance;
	ModificationProbe oldProbe, currentProbe, driverProbe;
	auto properties = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(settings);
	obs_property_t *target = obs_properties_add_bool(properties.get(), "target", "Target");
	obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
	REQUIRE(target);
	REQUIRE(driver);
	obs_property_set_modified_callback2(target, probeModified, &oldProbe);
	bool replaceHandler = false;
	SECTION("Clear handler") {}
	SECTION("Replace handler")
	{
		replaceHandler = true;
	}
	driverProbe.action = [&](obs_properties_t *, obs_property_t *, obs_data_t *) {
		obs_property_set_modified_callback2(target, replaceHandler ? probeModified : nullptr, &currentProbe);
	};
	obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(driverProbe.calls == 1);
	CHECK(oldProbe.calls == 0);
	CHECK(currentProbe.calls == (replaceHandler ? 1 : 0));
}

TEST_CASE("Properties nested settings application keeps independent snapshots", "[properties][callback][lifetime]")
{
	AllocationBalance balance;
	ModificationProbe oldProbe, replacementProbe, survivorProbe, driverProbe;
	std::vector<std::string> events;
	auto properties = makeProperties();
	auto outerSettings = makeSettings();
	auto innerSettings = makeSettings();
	REQUIRE(properties);
	REQUIRE(outerSettings);
	REQUIRE(innerSettings);
	obs_data_set_int(outerSettings.get(), "pass", 1);
	obs_data_set_int(innerSettings.get(), "pass", 2);
	const auto record = [&](obs_properties_t *, obs_property_t *property, obs_data_t *settings) {
		events.push_back(std::string(obs_property_name(property)) + ":" +
				 std::to_string(obs_data_get_int(settings, "pass")));
	};
	survivorProbe.action = record;
	replacementProbe.action = record;
	obs_property_t *survivor = obs_properties_add_bool(properties.get(), "survivor", "Survivor");
	obs_property_t *old = obs_properties_add_bool(properties.get(), "target", "Old target");
	obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
	REQUIRE(survivor);
	REQUIRE(old);
	REQUIRE(driver);
	obs_property_set_modified_callback2(survivor, probeModified, &survivorProbe);
	obs_property_set_modified_callback2(old, probeModified, &oldProbe);
	bool replaced = false;
	driverProbe.action = [&](obs_properties_t *props, obs_property_t *property, obs_data_t *settings) {
		record(props, property, settings);
		obs_property_set_modified_callback2(property, nullptr, nullptr);
		obs_properties_remove_by_name(props, "target");
		obs_property_t *replacement = obs_properties_add_bool(props, "target", "Replacement target");
		replaced = replacement != nullptr;
		obs_property_set_modified_callback2(replacement, probeModified, &replacementProbe);
		obs_properties_apply_settings(props, innerSettings.get());
	};
	obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
	obs_properties_apply_settings(properties.get(), outerSettings.get());
	REQUIRE(replaced);
	CHECK(driverProbe.calls == 1);
	CHECK(oldProbe.calls == 0);
	CHECK(replacementProbe.calls == 1);
	CHECK(survivorProbe.calls == 2);
	CHECK(events == std::vector<std::string>{"driver:1", "target:2", "survivor:2", "survivor:1"});
}

TEST_CASE("Properties empty settings application releases all temporary storage", "[properties][callback]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	auto settings = makeSettings();
	REQUIRE(properties);
	REQUIRE(settings);
	obs_properties_apply_settings(nullptr, settings.get());
	obs_properties_apply_settings(properties.get(), settings.get());
	REQUIRE(obs_properties_add_bool(properties.get(), "no_callback", "No callback"));
	obs_properties_apply_settings(properties.get(), settings.get());
	CHECK(obs_properties_first(properties.get()) == obs_properties_get(properties.get(), "no_callback"));
}

TEST_CASE("Properties retain owned metadata flags and numeric constraints", "[properties][metadata]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	char name[] = "integer";
	char description[] = "Integer setting";
	obs_property_t *integer = obs_properties_add_int_slider(properties.get(), name, description, -40, 80, 5);
	obs_property_t *floating =
		obs_properties_add_float(properties.get(), "floating", "Floating setting", 0.0, 1.0, 0.25);
	REQUIRE(integer);
	REQUIRE(floating);
	name[0] = 'X';
	description[0] = 'X';
	CHECK(std::string(obs_property_name(integer)) == "integer");
	CHECK(std::string(obs_property_description(integer)) == "Integer setting");
	CHECK(obs_properties_get(properties.get(), "integer") == integer);
	CHECK(obs_property_get_type(integer) == OBS_PROPERTY_INT);
	CHECK(obs_property_int_type(integer) == OBS_NUMBER_SLIDER);
	CHECK(obs_property_int_min(integer) == -40);
	CHECK(obs_property_int_max(integer) == 80);
	CHECK(obs_property_int_step(integer) == 5);
	CHECK(obs_property_float_type(floating) == OBS_NUMBER_SCROLLER);
	CHECK(obs_property_float_min(floating) == 0.0);
	CHECK(obs_property_float_max(floating) == 1.0);
	CHECK(obs_property_float_step(floating) == 0.25);
	obs_property_int_set_limits(integer, -100, 100, 10);
	obs_property_float_set_limits(floating, -2.0, 2.0, 0.5);
	CHECK(obs_property_int_min(integer) == -100);
	CHECK(obs_property_int_max(integer) == 100);
	CHECK(obs_property_int_step(integer) == 10);
	CHECK(obs_property_float_min(floating) == -2.0);
	CHECK(obs_property_float_max(floating) == 2.0);
	CHECK(obs_property_float_step(floating) == 0.5);
	char suffix[] = " pixels";
	obs_property_int_set_suffix(integer, suffix);
	obs_property_float_set_suffix(floating, suffix);
	suffix[0] = 'X';
	CHECK(std::string(obs_property_int_suffix(integer)) == " pixels");
	CHECK(std::string(obs_property_float_suffix(floating)) == " pixels");
	CHECK(obs_property_visible(integer));
	CHECK(obs_property_enabled(integer));
	obs_property_set_visible(integer, false);
	obs_property_set_enabled(integer, false);
	CHECK_FALSE(obs_property_visible(integer));
	CHECK_FALSE(obs_property_enabled(integer));
	CHECK(obs_property_is_visible(integer) == 0);
	obs_properties_set_flags(properties.get(), OBS_PROPERTIES_DEFER_UPDATE);
	CHECK(obs_properties_get_flags(properties.get()) == OBS_PROPERTIES_DEFER_UPDATE);
	CHECK(obs_properties_first(properties.get()) == integer);
	obs_property_t *iterator = integer;
	REQUIRE(obs_property_next(&iterator));
	CHECK(iterator == floating);
	CHECK_FALSE(obs_property_next(&iterator));
	CHECK(iterator == nullptr);
}

TEST_CASE("Properties descriptions own replacement text and support clearing", "[properties][metadata][ownership]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *property = obs_properties_add_bool(properties.get(), "toggle", "Toggle");
	REQUIRE(property);
	auto setter = obs_property_set_description;
	auto getter = obs_property_description;
	SECTION("Description") {}
	SECTION("Long description")
	{
		setter = obs_property_set_long_description;
		getter = obs_property_long_description;
	}

	char initial[] = "Initial property text";
	setter(property, initial);
	initial[0] = 'X';
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "Initial property text");
	char replacement[] = "Replacement property text";
	setter(property, replacement);
	replacement[0] = 'X';
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "Replacement property text");
	std::unique_ptr<char, decltype(&bfree)> temporary(bstrdup("Temporary property text"), bfree);
	REQUIRE(temporary);
	setter(property, temporary.get());
	temporary.reset();
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "Temporary property text");
	setter(property, "");
	CHECK(getter(property) == nullptr);
	setter(property, "Restored property text");
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "Restored property text");
	setter(property, nullptr);
	CHECK(getter(property) == nullptr);
	setter(nullptr, "Ignored property text");
	CHECK(getter(nullptr) == nullptr);
}

TEST_CASE("Properties descriptions can reuse their current text", "[properties][metadata][ownership][lifetime]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *property = obs_properties_add_bool(properties.get(), "toggle", "Toggle");
	REQUIRE(property);
	auto setter = obs_property_set_description;
	auto getter = obs_property_description;
	SECTION("Description") {}
	SECTION("Long description")
	{
		setter = obs_property_set_long_description;
		getter = obs_property_long_description;
	}

	setter(property, "Current property text");
	const char *current = getter(property);
	REQUIRE(current);
	setter(property, current);
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "Current property text");
	current = getter(property);
	setter(property, current + 8);
	REQUIRE(getter(property));
	CHECK(std::string(getter(property)) == "property text");
	current = getter(property);
	setter(property, current + std::char_traits<char>::length(current));
	CHECK(getter(property) == nullptr);
}

TEST_CASE("Properties typed lists preserve values and order through edits", "[properties][list]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *strings = obs_properties_add_list(properties.get(), "strings", "Strings", OBS_COMBO_TYPE_LIST,
							  OBS_COMBO_FORMAT_STRING);
	obs_property_t *integers = obs_properties_add_list(properties.get(), "integers", "Integers",
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_t *floats = obs_properties_add_list(properties.get(), "floats", "Floats", OBS_COMBO_TYPE_LIST,
							 OBS_COMBO_FORMAT_FLOAT);
	obs_property_t *booleans = obs_properties_add_list(properties.get(), "booleans", "Booleans",
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_BOOL);
	REQUIRE(strings);
	REQUIRE(integers);
	REQUIRE(floats);
	REQUIRE(booleans);
	CHECK(obs_property_list_type(strings) == OBS_COMBO_TYPE_LIST);
	CHECK(obs_property_list_format(strings) == OBS_COMBO_FORMAT_STRING);
	char label[] = "First";
	char value[] = "one";
	CHECK(obs_property_list_add_string(strings, label, value) == 0);
	label[0] = 'X';
	value[0] = 'X';
	CHECK(obs_property_list_add_string(strings, "Last", "three") == 1);
	obs_property_list_insert_string(strings, 1, "Middle", "two");
	CHECK(obs_property_list_item_count(strings) == 3);
	CHECK(std::string(obs_property_list_item_name(strings, 0)) == "First");
	CHECK(std::string(obs_property_list_item_string(strings, 0)) == "one");
	CHECK(std::string(obs_property_list_item_string(strings, 1)) == "two");
	CHECK(std::string(obs_property_list_item_string(strings, 2)) == "three");
	obs_property_list_item_disable(strings, 1, true);
	CHECK(obs_property_list_item_disabled(strings, 1));
	obs_property_list_item_remove(strings, 0);
	CHECK(obs_property_list_item_count(strings) == 2);
	CHECK(std::string(obs_property_list_item_name(strings, 0)) == "Middle");
	CHECK(obs_property_list_item_disabled(strings, 0));
	CHECK(obs_property_list_item_name(strings, 99) == nullptr);
	CHECK(obs_property_list_add_int(integers, "Negative", -1234567890123LL) == 0);
	obs_property_list_insert_int(integers, 0, "Positive", 1234567890123LL);
	CHECK(obs_property_list_item_int(integers, 0) == 1234567890123LL);
	CHECK(obs_property_list_item_int(integers, 1) == -1234567890123LL);
	CHECK(obs_property_list_add_float(floats, "Negative", -1.5) == 0);
	obs_property_list_insert_float(floats, 0, "Positive", 2.25);
	CHECK(obs_property_list_item_float(floats, 0) == 2.25);
	CHECK(obs_property_list_item_float(floats, 1) == -1.5);
	CHECK(obs_property_list_add_bool(booleans, "Enabled", true) == 0);
	obs_property_list_insert_bool(booleans, 0, "Disabled", false);
	CHECK_FALSE(obs_property_list_item_bool(booleans, 0));
	CHECK(obs_property_list_item_bool(booleans, 1));
	// A mismatched value setter must not insert a wrongly typed list item.
	obs_property_list_add_int(strings, "Wrong type", 7);
	CHECK(obs_property_list_item_count(strings) == 2);
	for (auto *property : {strings, integers, floats, booleans}) {
		obs_property_list_clear(property);
		CHECK(obs_property_list_item_count(property) == 0);
	}
	CHECK(obs_property_list_add_string(strings, "Reused", "again") == 0);
	CHECK(std::string(obs_property_list_item_string(strings, 0)) == "again");
}

TEST_CASE("Properties frame rate options own text and preserve insertion order", "[properties][frame-rate][ownership]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *rate = obs_properties_add_frame_rate(properties.get(), "rate", "Frame rate");
	REQUIRE(rate);
	CHECK(obs_property_get_type(rate) == OBS_PROPERTY_FRAME_RATE);
	CHECK(obs_property_frame_rate_options_count(rate) == 0);
	CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 0);
	char name[] = "second";
	char description[] = "Second option";
	REQUIRE(obs_property_frame_rate_option_add(rate, name, description) == 0);
	name[0] = 'X';
	description[0] = 'X';
	REQUIRE(obs_property_frame_rate_option_add(rate, "fourth", "Fourth option") == 1);
	char insertedName[] = "third";
	char insertedDescription[] = "Third option";
	obs_property_frame_rate_option_insert(rate, 1, insertedName, insertedDescription);
	insertedName[0] = 'X';
	insertedDescription[0] = 'X';
	obs_property_frame_rate_option_insert(rate, 0, "first", "First option");
	obs_property_frame_rate_option_insert(rate, obs_property_frame_rate_options_count(rate), "fifth",
					      "Fifth option");
	const std::array<const char *, 5> names{"first", "second", "third", "fourth", "fifth"};
	const std::array<const char *, 5> descriptions{"First option", "Second option", "Third option", "Fourth option",
						       "Fifth option"};
	REQUIRE(obs_property_frame_rate_options_count(rate) == names.size());
	for (size_t idx = 0; idx < names.size(); ++idx) {
		CAPTURE(idx);
		REQUIRE(obs_property_frame_rate_option_name(rate, idx));
		REQUIRE(obs_property_frame_rate_option_description(rate, idx));
		CHECK(std::string(obs_property_frame_rate_option_name(rate, idx)) == names[idx]);
		CHECK(std::string(obs_property_frame_rate_option_description(rate, idx)) == descriptions[idx]);
	}
	CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 0);
}

TEST_CASE("Properties frame rate ranges retain exact fractions and insertion order", "[properties][frame-rate]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *rate = obs_properties_add_frame_rate(properties.get(), "rate", "Frame rate");
	REQUIRE(rate);
	const media_frames_per_second fractionalMin{24000, 1001}, fractionalMax{30000, 1001};
	const media_frames_per_second integerMin{30, 1}, integerMax{60, 1};
	REQUIRE(obs_property_frame_rate_fps_range_add(rate, fractionalMin, fractionalMax) == 0);
	REQUIRE(obs_property_frame_rate_fps_range_add(rate, integerMin, integerMax) == 1);
	obs_property_frame_rate_fps_range_insert(rate, 1, {30000, 1001}, {60000, 1001});
	obs_property_frame_rate_fps_range_insert(rate, 0, {15, 1}, {24, 1});
	obs_property_frame_rate_fps_range_insert(rate, obs_property_frame_rate_fps_ranges_count(rate), {60, 1},
						 {120, 1});
	const std::array<media_frames_per_second, 5> minimums{
		{{15, 1}, fractionalMin, {30000, 1001}, integerMin, {60, 1}}};
	const std::array<media_frames_per_second, 5> maximums{
		{{24, 1}, fractionalMax, {60000, 1001}, integerMax, {120, 1}}};
	REQUIRE(obs_property_frame_rate_fps_ranges_count(rate) == minimums.size());
	for (size_t idx = 0; idx < minimums.size(); ++idx) {
		CAPTURE(idx);
		expectFrameRate(obs_property_frame_rate_fps_range_min(rate, idx), minimums[idx]);
		expectFrameRate(obs_property_frame_rate_fps_range_max(rate, idx), maximums[idx]);
	}
	CHECK(obs_property_frame_rate_options_count(rate) == 0);
}

TEST_CASE("Properties frame rate options and ranges clear independently and can be reused",
	  "[properties][frame-rate][ownership]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *rate = obs_properties_add_frame_rate(properties.get(), "rate", "Frame rate");
	REQUIRE(rate);
	REQUIRE(obs_property_frame_rate_option_add(rate, "automatic", "Automatic") == 0);
	REQUIRE(obs_property_frame_rate_fps_range_add(rate, {24000, 1001}, {60000, 1001}) == 0);
	SECTION("Options only")
	{
		obs_property_frame_rate_options_clear(rate);
		CHECK(obs_property_frame_rate_options_count(rate) == 0);
		REQUIRE(obs_property_frame_rate_fps_ranges_count(rate) == 1);
		expectFrameRate(obs_property_frame_rate_fps_range_min(rate, 0), {24000, 1001});
		expectFrameRate(obs_property_frame_rate_fps_range_max(rate, 0), {60000, 1001});
		CHECK(obs_property_frame_rate_option_add(rate, "replacement", "Replacement") == 0);
	}
	SECTION("Ranges only")
	{
		obs_property_frame_rate_fps_ranges_clear(rate);
		CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 0);
		REQUIRE(obs_property_frame_rate_options_count(rate) == 1);
		REQUIRE(obs_property_frame_rate_option_name(rate, 0));
		REQUIRE(obs_property_frame_rate_option_description(rate, 0));
		CHECK(std::string(obs_property_frame_rate_option_name(rate, 0)) == "automatic");
		CHECK(std::string(obs_property_frame_rate_option_description(rate, 0)) == "Automatic");
		CHECK(obs_property_frame_rate_fps_range_add(rate, {25, 1}, {50, 1}) == 0);
	}
	SECTION("Both")
	{
		obs_property_frame_rate_clear(rate);
		CHECK(obs_property_frame_rate_options_count(rate) == 0);
		CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 0);
	}
	obs_property_frame_rate_clear(rate);
	obs_property_frame_rate_clear(rate);
	CHECK(obs_property_frame_rate_options_count(rate) == 0);
	CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 0);
	CHECK(obs_property_frame_rate_option_name(rate, 0) == nullptr);
	CHECK(obs_property_frame_rate_option_description(rate, 0) == nullptr);
	expectFrameRate(obs_property_frame_rate_fps_range_min(rate, 0), {});
	expectFrameRate(obs_property_frame_rate_fps_range_max(rate, 0), {});
	CHECK(obs_property_frame_rate_option_add(rate, "reused", "Reused") == 0);
	CHECK(obs_property_frame_rate_fps_range_add(rate, {30000, 1001}, {60000, 1001}) == 0);
	REQUIRE(obs_property_frame_rate_option_name(rate, 0));
	REQUIRE(obs_property_frame_rate_option_description(rate, 0));
	CHECK(std::string(obs_property_frame_rate_option_name(rate, 0)) == "reused");
	CHECK(std::string(obs_property_frame_rate_option_description(rate, 0)) == "Reused");
	expectFrameRate(obs_property_frame_rate_fps_range_min(rate, 0), {30000, 1001});
	expectFrameRate(obs_property_frame_rate_fps_range_max(rate, 0), {60000, 1001});
}

TEST_CASE("Properties frame rate helpers handle absent controls and missing entries", "[properties][frame-rate]")
{
	AllocationBalance balance;
	auto properties = makeProperties();
	REQUIRE(properties);
	obs_property_t *rate = obs_properties_add_frame_rate(properties.get(), "rate", "Frame rate");
	obs_property_t *toggle = obs_properties_add_bool(properties.get(), "toggle", "Toggle");
	REQUIRE(rate);
	REQUIRE(toggle);
	const size_t missing = std::numeric_limits<size_t>::max();
	for (auto *property : {rate, toggle, static_cast<obs_property_t *>(nullptr)}) {
		CHECK(obs_property_frame_rate_options_count(property) == 0);
		CHECK(obs_property_frame_rate_fps_ranges_count(property) == 0);
		for (size_t idx : {size_t{0}, missing}) {
			CHECK(obs_property_frame_rate_option_name(property, idx) == nullptr);
			CHECK(obs_property_frame_rate_option_description(property, idx) == nullptr);
			expectFrameRate(obs_property_frame_rate_fps_range_min(property, idx), {});
			expectFrameRate(obs_property_frame_rate_fps_range_max(property, idx), {});
		}
	}
	for (auto *property : {toggle, static_cast<obs_property_t *>(nullptr)}) {
		CHECK(obs_property_frame_rate_option_add(property, "ignored", "Ignored") == missing);
		CHECK(obs_property_frame_rate_fps_range_add(property, {24, 1}, {60, 1}) == missing);
		obs_property_frame_rate_option_insert(property, 0, "ignored", "Ignored");
		obs_property_frame_rate_fps_range_insert(property, 0, {24, 1}, {60, 1});
		obs_property_frame_rate_clear(property);
		obs_property_frame_rate_options_clear(property);
		obs_property_frame_rate_fps_ranges_clear(property);
	}
	CHECK(obs_property_get_type(toggle) == OBS_PROPERTY_BOOL);
	REQUIRE(obs_property_description(toggle));
	CHECK(std::string(obs_property_description(toggle)) == "Toggle");
	REQUIRE(obs_property_frame_rate_option_add(rate, "automatic", "Automatic") == 0);
	REQUIRE(obs_property_frame_rate_fps_range_add(rate, {24, 1}, {60, 1}) == 0);
	for (size_t idx : {size_t{1}, missing}) {
		CHECK(obs_property_frame_rate_option_name(rate, idx) == nullptr);
		CHECK(obs_property_frame_rate_option_description(rate, idx) == nullptr);
		expectFrameRate(obs_property_frame_rate_fps_range_min(rate, idx), {});
		expectFrameRate(obs_property_frame_rate_fps_range_max(rate, idx), {});
	}
	CHECK(obs_property_frame_rate_options_count(rate) == 1);
	CHECK(obs_property_frame_rate_fps_ranges_count(rate) == 1);
}

TEST_CASE("Properties group ownership releases cleanup parameters exactly once", "[properties][group][ownership]")
{
	AllocationBalance balance;
	int rootDestroyed = 0;
	int replacedDestroyed = 0;
	int groupDestroyed = 0;
	const auto countDestruction = +[](void *param) {
		++*static_cast<int *>(param);
	};
	Properties properties(obs_properties_create_param(&replacedDestroyed, countDestruction),
			      obs_properties_destroy);
	Properties group(obs_properties_create_param(&groupDestroyed, countDestruction), obs_properties_destroy);
	REQUIRE(properties);
	REQUIRE(group);
	obs_properties_set_param(properties.get(), &rootDestroyed, countDestruction);
	CHECK(replacedDestroyed == 1);
	CHECK(rootDestroyed == 0);
	CHECK(obs_properties_get_param(properties.get()) == &rootDestroyed);
	obs_property_t *child = obs_properties_add_bool(group.get(), "child", "Child");
	REQUIRE(child);
	obs_properties_t *content = group.get();
	obs_property_t *container =
		obs_properties_add_group(properties.get(), "group", "Group", OBS_GROUP_CHECKABLE, content);
	REQUIRE(container);
	group.release();
	CHECK(obs_property_group_type(container) == OBS_GROUP_CHECKABLE);
	CHECK(obs_properties_get_parent(content) == properties.get());
	CHECK(obs_properties_get(properties.get(), "child") == child);
	CHECK(groupDestroyed == 0);
	obs_properties_remove_by_name(properties.get(), "child");
	CHECK(obs_properties_get(content, "child") == nullptr);
	obs_properties_remove_by_name(properties.get(), "group");
	CHECK(groupDestroyed == 1);
	CHECK(obs_properties_get(properties.get(), "group") == nullptr);
	properties.reset();
	CHECK(rootDestroyed == 1);
	CHECK(replacedDestroyed == 1);
	CHECK(groupDestroyed == 1);
}

TEST_CASE("Properties independent trees apply safely during concurrent creation", "[properties][callback][identity]")
{
	AllocationBalance balance;
	std::array<bool, 4> results{};
	std::vector<std::thread> workers;
	for (size_t worker = 0; worker < results.size(); ++worker) {
		workers.emplace_back([&, worker] {
			bool valid = true;
			for (size_t round = 0; round < 64; ++round) {
				ModificationProbe oldProbe, newProbe, driverProbe;
				auto properties = makeProperties();
				auto incoming = makeProperties();
				auto settings = makeSettings();
				obs_property_t *old = obs_properties_add_bool(properties.get(), "target", "Old");
				obs_property_t *replacement = obs_properties_add_bool(incoming.get(), "target", "New");
				obs_property_t *driver = obs_properties_add_bool(properties.get(), "driver", "Driver");
				if (!old || !replacement || !driver) {
					valid = false;
					break;
				}
				obs_property_set_modified_callback2(old, probeModified, &oldProbe);
				obs_property_set_modified_callback2(replacement, probeModified, &newProbe);
				bool attached = false;
				driverProbe.action = [&](obs_properties_t *props, obs_property_t *property,
							 obs_data_t *) {
					obs_properties_remove_by_name(props, "target");
					obs_property_t *container = obs_properties_add_group(
						props, "replacement", "Replacement", OBS_GROUP_NORMAL, incoming.get());
					attached = container != nullptr;
					if (container)
						incoming.release();
					obs_property_set_modified_callback2(property, nullptr, nullptr);
				};
				obs_property_set_modified_callback2(driver, probeModified, &driverProbe);
				obs_properties_apply_settings(properties.get(), settings.get());
				valid &= attached && oldProbe.calls == 0 && newProbe.calls == 0 &&
					 driverProbe.calls == 1;
				obs_properties_apply_settings(properties.get(), settings.get());
				valid &= newProbe.calls == 1;
			}
			results[worker] = valid;
		});
	}
	for (auto &worker : workers)
		worker.join();
	for (size_t worker = 0; worker < results.size(); ++worker) {
		CAPTURE(worker);
		CHECK(results[worker]);
	}
}

} // namespace
