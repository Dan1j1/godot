/**************************************************************************/
/*  test_file_access_memory.cpp                                           */
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

TEST_FORCE_LINK(test_file_access_memory)

#include "core/io/file_access_memory.h"

namespace TestFileAccessMemory {

// Bytes 1..p_size.
static Vector<uint8_t> make_range(int p_size) {
	Vector<uint8_t> out;
	out.resize(p_size);
	for (int i = 0; i < p_size; i++) {
		out.write[i] = (uint8_t)(i + 1);
	}
	return out;
}

TEST_CASE("[FileAccessMemory] Open a registered file and read it") {
	const String path = "/godot_test_memory/sample.bin";
	FileAccessMemory::register_file(path, make_range(10));

	Ref<FileAccessMemory> file;
	file.instantiate();
	CHECK_FALSE(file->is_open());

	REQUIRE_EQ(file->open_internal(path, FileAccess::READ), Error::OK);
	CHECK(file->is_open());
	CHECK_EQ(file->get_length(), uint64_t(10));
	CHECK_EQ(file->get_position(), uint64_t(0));
	CHECK_FALSE(file->eof_reached());
	CHECK_EQ(file->get_error(), Error::OK);

	uint8_t head[4];
	CHECK_EQ(file->get_buffer(head, 4), uint64_t(4));
	CHECK_EQ(head[0], 1);
	CHECK_EQ(head[3], 4);
	CHECK_EQ(file->get_position(), uint64_t(4));
	CHECK_FALSE(file->eof_reached());

	uint8_t rest[6];
	CHECK_EQ(file->get_buffer(rest, 6), uint64_t(6));
	CHECK_EQ(rest[5], 10);
	CHECK(file->eof_reached());
	CHECK_EQ(file->get_error(), Error::ERR_FILE_EOF);
}

TEST_CASE("[FileAccessMemory] file_exists and opening a missing file") {
	const String path = "/godot_test_memory/exists.bin";
	FileAccessMemory::register_file(path, make_range(2));

	Ref<FileAccessMemory> file;
	file.instantiate();

	CHECK(file->file_exists(path));
	CHECK_FALSE(file->file_exists("/godot_test_memory/missing.bin"));

	ERR_PRINT_OFF;
	CHECK_EQ(file->open_internal("/godot_test_memory/missing.bin", FileAccess::READ), Error::ERR_FILE_NOT_FOUND);
	ERR_PRINT_ON;
	CHECK_FALSE(file->is_open());
}

TEST_CASE("[FileAccessMemory] Seeking") {
	uint8_t data[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
	Ref<FileAccessMemory> file;
	file.instantiate();
	REQUIRE_EQ(file->open_custom(data, 10), Error::OK);

	file->seek(2);
	CHECK_EQ(file->get_position(), uint64_t(2));
	uint8_t b = 0;
	CHECK_EQ(file->get_buffer(&b, 1), uint64_t(1));
	CHECK_EQ(b, 3);

	file->seek(10); // Seeking to exactly the end is allowed.
	CHECK_EQ(file->get_position(), uint64_t(10));
	CHECK(file->eof_reached());

	file->seek_end(-3);
	CHECK_EQ(file->get_position(), uint64_t(7));

	file->seek_end(0);
	CHECK_EQ(file->get_position(), uint64_t(10));

	// Out-of-range seeks are rejected and leave the position unchanged.
	file->seek(7);
	ERR_PRINT_OFF;
	file->seek(11);
	CHECK_EQ(file->get_position(), uint64_t(7));
	file->seek_end(-11);
	CHECK_EQ(file->get_position(), uint64_t(7));
	file->seek_end(1);
	CHECK_EQ(file->get_position(), uint64_t(7));
	ERR_PRINT_ON;
}

TEST_CASE("[FileAccessMemory] Reading edge cases") {
	uint8_t data[4] = { 1, 2, 3, 4 };
	Ref<FileAccessMemory> file;
	file.instantiate();
	REQUIRE_EQ(file->open_custom(data, 4), Error::OK);

	uint8_t buf[8] = {};

	CHECK_EQ(file->get_buffer(buf, 0), uint64_t(0));

	ERR_PRINT_OFF;
	CHECK_EQ(file->get_buffer(nullptr, 1), uint64_t(-1));

	// Asking for more than is left returns only what is left.
	file->seek(2);
	CHECK_EQ(file->get_buffer(buf, 8), uint64_t(2));
	ERR_PRINT_ON;
	CHECK_EQ(buf[0], 3);
	CHECK_EQ(buf[1], 4);
	CHECK(file->eof_reached());
}

TEST_CASE("[FileAccessMemory] Operations on a file that is not open") {
	Ref<FileAccessMemory> file;
	file.instantiate();

	ERR_PRINT_OFF;
	CHECK_FALSE(file->is_open());
	CHECK_EQ(file->get_length(), uint64_t(0));
	CHECK_EQ(file->get_position(), uint64_t(0));
	file->seek(0);
	file->seek_end(0);
	file->flush();
	uint8_t buf[2];
	CHECK_EQ(file->get_buffer(buf, 2), uint64_t(-1));
	ERR_PRINT_ON;
}

TEST_CASE("[FileAccessMemory] Writing with store_buffer") {
	uint8_t backing[6] = { 0, 0, 0, 0, 0, 0 };
	Ref<FileAccessMemory> file;
	file.instantiate();
	REQUIRE_EQ(file->open_custom(backing, 6), Error::OK);

	const uint8_t first[3] = { 7, 8, 9 };
	CHECK(file->store_buffer(first, 3));
	CHECK_EQ(file->get_position(), uint64_t(3));
	CHECK_EQ(backing[0], 7);
	CHECK_EQ(backing[2], 9);

	// An empty write succeeds even without a source pointer.
	CHECK(file->store_buffer(nullptr, 0));

	ERR_PRINT_OFF;
	// A null source with a non-zero length is an error.
	CHECK_FALSE(file->store_buffer(nullptr, 1));

	// Writing more than fits stores what fits, then reports failure.
	const uint8_t second[5] = { 1, 2, 3, 4, 5 };
	CHECK_FALSE(file->store_buffer(second, 5));
	ERR_PRINT_ON;
	CHECK_EQ(file->get_position(), uint64_t(6));
	CHECK_EQ(backing[3], 1);
	CHECK_EQ(backing[5], 3);

	file->flush();
}

TEST_CASE("[FileAccessMemory] Unsupported operations report so") {
	uint8_t data[1] = { 0 };
	Ref<FileAccessMemory> file;
	file.instantiate();
	REQUIRE_EQ(file->open_custom(data, 1), Error::OK);

	CHECK_EQ(file->resize(10), Error::ERR_UNAVAILABLE);
	CHECK_EQ(file->_get_modified_time("x"), uint64_t(0));
	CHECK_EQ(file->_get_access_time("x"), uint64_t(0));
	CHECK_EQ(file->_get_size("x"), int64_t(-1));
	CHECK_FALSE(file->_get_hidden_attribute("x"));
	CHECK_EQ(file->_set_hidden_attribute("x", true), Error::ERR_UNAVAILABLE);
	CHECK_FALSE(file->_get_read_only_attribute("x"));
	CHECK_EQ(file->_set_read_only_attribute("x", true), Error::ERR_UNAVAILABLE);
	file->close();
}

} // namespace TestFileAccessMemory
