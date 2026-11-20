/**************************************************************************/
/*  test_delta_encoding.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


#include "tests/test_macros.h"

TEST_FORCE_LINK(test_delta_encoding)

#include "core/io/delta_encoding.h"
#include "core/templates/vector.h"

namespace TestDeltaEncoding {

// Deterministic, compressible data (period 251), so a delta against similar data is small.
static Vector<uint8_t> make_pattern(int p_size, int p_mul = 7) {
	Vector<uint8_t> out;
	out.resize(p_size);
	for (int i = 0; i < p_size; i++) {
		out.write[i] = (uint8_t)((i * p_mul + 3) % 251);
	}
	return out;
}

TEST_CASE("[DeltaEncoding] Encode then decode restores the new data") {
	Vector<uint8_t> old_data = String("The quick brown fox jumps over the lazy dog.").to_utf8_buffer();
	Vector<uint8_t> new_data = String("The quick brown cat jumps over the lazy dog!").to_utf8_buffer();

	int level = 19;
	SUBCASE("Default compression level") {
		level = 19;
	}
	SUBCASE("Fast compression level") {
		level = 1;
	}

	Vector<uint8_t> delta;
	CHECK_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta, level), Error::OK);

	Vector<uint8_t> decoded;
	CHECK_EQ(DeltaEncoding::decode_delta(old_data, delta, decoded), Error::OK);
	CHECK_EQ(decoded, new_data);
}

TEST_CASE("[DeltaEncoding] Encoded delta starts with the GDDL magic and version 1") {
	Vector<uint8_t> old_data = make_pattern(64);
	Vector<uint8_t> new_data = make_pattern(64, 11);

	Vector<uint8_t> delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);

	REQUIRE_GT(delta.size(), 5);
	CHECK_EQ(delta[0], 'G');
	CHECK_EQ(delta[1], 'D');
	CHECK_EQ(delta[2], 'D');
	CHECK_EQ(delta[3], 'L');
	CHECK_EQ(delta[4], 1);
}

TEST_CASE("[DeltaEncoding] A small change to large data gives a small delta") {
	Vector<uint8_t> old_data = make_pattern(4096);
	Vector<uint8_t> new_data = make_pattern(4096);
	new_data.write[2000] = 0xFF;

	Vector<uint8_t> delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);
	CHECK_LT(delta.size(), new_data.size() / 2);

	Vector<uint8_t> decoded;
	CHECK_EQ(DeltaEncoding::decode_delta(old_data, delta, decoded), Error::OK);
	CHECK_EQ(decoded, new_data);
}

TEST_CASE("[DeltaEncoding] Encoding against empty old data works") {
	Vector<uint8_t> old_data;
	Vector<uint8_t> new_data = String("Completely new content").to_utf8_buffer();

	Vector<uint8_t> delta;
	CHECK_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);

	Vector<uint8_t> decoded;
	CHECK_EQ(DeltaEncoding::decode_delta(old_data, delta, decoded), Error::OK);
	CHECK_EQ(decoded, new_data);
}

TEST_CASE("[DeltaEncoding] Decoding rejects invalid input") {
	Vector<uint8_t> old_data = make_pattern(256);
	Vector<uint8_t> new_data = make_pattern(256, 11);

	Vector<uint8_t> valid_delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, valid_delta), Error::OK);

	Vector<uint8_t> result;

	ERR_PRINT_OFF;

	SUBCASE("Delta shorter than the header") {
		Vector<uint8_t> too_short;
		too_short.resize(3);
		CHECK_EQ(DeltaEncoding::decode_delta(old_data, too_short, result), Error::ERR_INVALID_DATA);
	}

	SUBCASE("Wrong magic") {
		Vector<uint8_t> bad = valid_delta;
		bad.write[0] = 'X';
		CHECK_EQ(DeltaEncoding::decode_delta(old_data, bad, result), Error::ERR_FILE_CORRUPT);
	}

	SUBCASE("Unsupported version") {
		Vector<uint8_t> bad = valid_delta;
		bad.write[4] = 2;
		CHECK_EQ(DeltaEncoding::decode_delta(old_data, bad, result), Error::ERR_FILE_UNRECOGNIZED);
	}

	SUBCASE("Valid header but no compressed payload") {
		Vector<uint8_t> header_only = valid_delta;
		header_only.resize(5);
		CHECK_EQ(DeltaEncoding::decode_delta(old_data, header_only, result), Error::FAILED);
	}

	SUBCASE("Valid header but garbage payload") {
		Vector<uint8_t> garbage = valid_delta;
		garbage.resize(5);
		for (int i = 0; i < 32; i++) {
			garbage.push_back((uint8_t)(0xA0 + i));
		}
		CHECK_EQ(DeltaEncoding::decode_delta(old_data, garbage, result), Error::FAILED);
	}

	SUBCASE("Truncated payload") {
		Vector<uint8_t> truncated = valid_delta;
		truncated.resize(truncated.size() - 3);
		CHECK_NE(DeltaEncoding::decode_delta(old_data, truncated, result), Error::OK);
	}

	ERR_PRINT_ON;
}

TEST_CASE("[DeltaEncoding] Decoding with the wrong old data fails") {
	Vector<uint8_t> old_data = make_pattern(2048);
	Vector<uint8_t> new_data = make_pattern(2048); // Identical, so the delta is made of references into old_data.

	Vector<uint8_t> delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);

	Vector<uint8_t> wrong_old;
	wrong_old.resize(2048);
	wrong_old.fill(0xFF);

	Vector<uint8_t> result;
	ERR_PRINT_OFF;
	CHECK_NE(DeltaEncoding::decode_delta(wrong_old, delta, result), Error::OK);
	ERR_PRINT_ON;
}

} // namespace TestDeltaEncoding
