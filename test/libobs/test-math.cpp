#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <graphics/bounds.h>
#include <graphics/matrix4.h>
#include <graphics/quat.h>
#include <graphics/vec2.h>

#include <array>

namespace {

using Catch::Matchers::WithinAbs;
constexpr float tolerance = 0.0001f;

vec3 vector3(float x, float y, float z)
{
	vec3 value;
	vec3_set(&value, x, y, z);
	return value;
}

void expectVector(const vec3 &actual, const vec3 &expected)
{
	CHECK_THAT(actual.x, WithinAbs(expected.x, tolerance));
	CHECK_THAT(actual.y, WithinAbs(expected.y, tolerance));
	CHECK_THAT(actual.z, WithinAbs(expected.z, tolerance));
}

void expectMatrix(const matrix4 &actual, const matrix4 &expected)
{
	const std::array<vec4, 4> rows{actual.x, actual.y, actual.z, actual.t};
	const std::array<vec4, 4> expectedRows{expected.x, expected.y, expected.z, expected.t};
	for (size_t row = 0; row < 4; ++row)
		for (size_t column = 0; column < 4; ++column) {
			CAPTURE(row, column);
			CHECK_THAT(rows[row].ptr[column], WithinAbs(expectedRows[row].ptr[column], tolerance));
		}
}

matrix4 transform()
{
	matrix4 value;
	matrix4_identity(&value);
	matrix4_scale3f(&value, &value, 2, 3, 4);
	matrix4_rotate_aa4f(&value, &value, 0, 0, 1, static_cast<float>(M_PI / 2));
	matrix4_translate3f(&value, &value, 10, 20, 30);
	return value;
}

TEST_CASE("Math vectors normalize and cross correctly including in place", "[graphics][math]")
{
	vec2 two;
	vec2_set(&two, 3, 4);
	CHECK_THAT(vec2_len(&two), WithinAbs(5, tolerance));
	vec2_norm(&two, &two);
	CHECK_THAT(two.x, WithinAbs(0.6, tolerance));
	CHECK_THAT(two.y, WithinAbs(0.8, tolerance));

	vec3 three = vector3(2, -3, 6);
	CHECK_THAT(vec3_len(&three), WithinAbs(7, tolerance));
	vec3_norm(&three, &three);
	expectVector(three, vector3(2.0f / 7, -3.0f / 7, 6.0f / 7));
	vec3 zero = vector3(0, 0, 0);
	vec3_norm(&three, &zero);
	expectVector(three, zero);

	vec3 x = vector3(1, 0, 0);
	vec3 y = vector3(0, 1, 0);
	vec3 cross;
	vec3_cross(&cross, &x, &y);
	expectVector(cross, vector3(0, 0, 1));
	CHECK_THAT(vec3_dot(&cross, &x), WithinAbs(0, tolerance));
	CHECK_THAT(vec3_dot(&cross, &y), WithinAbs(0, tolerance));
	vec3_cross(&y, &y, &x);
	expectVector(y, vector3(0, 0, -1));
}

TEST_CASE("Math vector proximity is symmetric", "[graphics][math]")
{
	const vec3 origin = vector3(0, 0, 0);
	const vec3 near = vector3(0.001f, -0.001f, 0);
	const vec3 far = vector3(10, 20, 30);
	CHECK(vec3_close(&origin, &near, 0.01f));
	CHECK(vec3_close(&near, &origin, 0.01f));
	CHECK_FALSE(vec3_close(&origin, &far, 0.01f));
	CHECK_FALSE(vec3_close(&far, &origin, 0.01f));
}

TEST_CASE("Math quaternion construction preserves component order", "[graphics][math]")
{
	quat value;
	quat_set(&value, 1, 2, 3, 4);
	CHECK(value.x == 1);
	CHECK(value.y == 2);
	CHECK(value.z == 3);
	CHECK(value.w == 4);
}

TEST_CASE("Math transforms apply scale rotation and translation in order", "[graphics][math]")
{
	const matrix4 matrix = transform();
	vec3 point = vector3(1, 2, 3);
	vec3_transform(&point, &point, &matrix);
	expectVector(point, vector3(4, 22, 42));

	// Homogeneous directions have w=0 and must not receive translation.
	vec4 direction;
	vec4_set(&direction, 1, 2, 3, 0);
	vec4_transform(&direction, &direction, &matrix);
	CHECK_THAT(direction.x, WithinAbs(-6, tolerance));
	CHECK_THAT(direction.y, WithinAbs(2, tolerance));
	CHECK_THAT(direction.z, WithinAbs(12, tolerance));
	CHECK_THAT(direction.w, WithinAbs(0, tolerance));
}

TEST_CASE("Math matrix multiplication supports identity and aliased operands", "[graphics][math]")
{
	const matrix4 matrix = transform();
	matrix4 identity, actual;
	matrix4_identity(&identity);
	matrix4_mul(&actual, &identity, &matrix);
	expectMatrix(actual, matrix);
	matrix4_mul(&actual, &matrix, &identity);
	expectMatrix(actual, matrix);

	matrix4 scale;
	matrix4_identity(&scale);
	matrix4_scale3f(&scale, &scale, 2, -1, 0.5f);
	matrix4 expected;
	matrix4_mul(&expected, &matrix, &scale);
	actual = matrix;
	matrix4_mul(&actual, &actual, &scale);
	expectMatrix(actual, expected);
	actual = scale;
	matrix4_mul(&actual, &matrix, &actual);
	expectMatrix(actual, expected);
	vec3 point = vector3(1, 2, 3);
	vec3_transform(&point, &point, &actual);
	expectVector(point, vector3(8, -22, 21));
}

TEST_CASE("Math matrix inverse restores points and rejects singular matrices", "[graphics][math]")
{
	matrix4 identity;
	matrix4_identity(&identity);
	SECTION("Invertible affine transform")
	{
		const matrix4 matrix = transform();
		CHECK_THAT(matrix4_determinant(&matrix), WithinAbs(24, tolerance));
		matrix4 inverse, product;
		REQUIRE(matrix4_inv(&inverse, &matrix));
		matrix4_mul(&product, &matrix, &inverse);
		expectMatrix(product, identity);
		matrix4_mul(&product, &inverse, &matrix);
		expectMatrix(product, identity);
		vec3 point = vector3(4, 22, 42);
		vec3_transform(&point, &point, &inverse);
		expectVector(point, vector3(1, 2, 3));
		matrix4 inPlace = matrix;
		REQUIRE(matrix4_inv(&inPlace, &inPlace));
		expectMatrix(inPlace, inverse);
	}
	SECTION("Singular transform")
	{
		matrix4 singular = identity;
		matrix4_scale3f(&singular, &singular, 1, 0, 1);
		matrix4 inverse;
		CHECK_FALSE(matrix4_inv(&inverse, &singular));
		CHECK_FALSE(matrix4_inv(&singular, &singular));
	}
}

TEST_CASE("Math matrix transpose handles every element and in place operation", "[graphics][math]")
{
	matrix4 input, expected;
	vec4_set(&input.x, 1, 2, 3, 4);
	vec4_set(&input.y, 5, 6, 7, 8);
	vec4_set(&input.z, 9, 10, 11, 12);
	vec4_set(&input.t, 13, 14, 15, 16);
	vec4_set(&expected.x, 1, 5, 9, 13);
	vec4_set(&expected.y, 2, 6, 10, 14);
	vec4_set(&expected.z, 3, 7, 11, 15);
	vec4_set(&expected.t, 4, 8, 12, 16);
	matrix4 actual;
	matrix4_transpose(&actual, &input);
	expectMatrix(actual, expected);
	matrix4_transpose(&input, &input);
	expectMatrix(input, expected);
}

TEST_CASE("Math quaternion matrix round trips preserve quarter and half turns", "[graphics][math]")
{
	for (const vec3 axis : {vector3(1, 0, 0), vector3(0, 1, 0), vector3(0, 0, 1)}) {
		for (const float angle : {0.0f, static_cast<float>(M_PI / 2), static_cast<float>(M_PI)}) {
			CAPTURE(axis.x, axis.y, axis.z, angle);
			axisang rotation;
			axisang_set(&rotation, axis.x, axis.y, axis.z, angle);
			quat original, restored;
			quat_from_axisang(&original, &rotation);
			matrix4 matrix, roundTrip;
			matrix4_from_quat(&matrix, &original);
			quat_from_matrix4(&restored, &matrix);
			matrix4_from_quat(&roundTrip, &restored);
			expectMatrix(roundTrip, matrix);
			// q and -q describe the same rotation.
			for (auto &component : original.ptr)
				component = -component;
			matrix4_from_quat(&roundTrip, &original);
			expectMatrix(roundTrip, matrix);
		}
	}
}

TEST_CASE("Math bounds transformation encloses rotated and reflected geometry", "[graphics][math]")
{
	bounds box{vector3(1, 2, 3), vector3(4, 5, 6)};
	matrix4 matrix;
	matrix4_identity(&matrix);
	matrix4_scale3f(&matrix, &matrix, -2, 3, 0.5f);
	matrix4_rotate_aa4f(&matrix, &matrix, 0, 0, 1, static_cast<float>(M_PI / 2));
	matrix4_translate3f(&matrix, &matrix, 10, 20, -1);
	bounds transformed;
	bounds_transform(&transformed, &box, &matrix);
	expectVector(transformed.min, vector3(-5, 12, 0.5f));
	expectVector(transformed.max, vector3(4, 18, 2));
	bounds_transform(&box, &box, &matrix);
	expectVector(box.min, transformed.min);
	expectVector(box.max, transformed.max);
}

TEST_CASE("Math ray intersections distinguish hits misses and boundary hits", "[graphics][math]")
{
	const bounds box{vector3(-1, -1, -1), vector3(1, 1, 1)};
	const struct {
		vec3 origin;
		vec3 direction;
		bool hit;
		float distance;
	} cases[] = {
		{vector3(-3, 0, 0), vector3(1, 0, 0), true, 2},  {vector3(-3, 0, 0), vector3(2, 0, 0), true, 1},
		{vector3(0, 0, 0), vector3(0, 0, 1), true, 1},   {vector3(-3, 1, 1), vector3(1, 0, 0), true, 2},
		{vector3(-3, 2, 0), vector3(1, 0, 0), false, 0}, {vector3(-3, 0, 0), vector3(-1, 0, 0), false, 0},
	};
	for (const auto &test : cases) {
		CAPTURE(test.origin.x, test.origin.y, test.origin.z, test.direction.x, test.direction.z);
		float distance = -1;
		const bool hit = bounds_intersection_ray(&box, &test.origin, &test.direction, &distance);
		CHECK(hit == test.hit);
		if (hit && test.hit)
			CHECK_THAT(distance, WithinAbs(test.distance, tolerance));
	}
}

} // namespace
