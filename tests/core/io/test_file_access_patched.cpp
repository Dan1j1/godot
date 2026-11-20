/**************************************************************************/
/*  test_file_access_patched.cpp                                          */
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

TEST_FORCE_LINK(test_file_access_patched)

#include "core/io/delta_encoding.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/file_access_patched.h"
#include "tests/test_utils.h"

namespace TestFileAccessPatched {

// Test double: a hand-written fake FileAccess that serves bytes from memory
// and records how it was used, so the tests can verify the interaction.
class FakeOldFile : public FileAccess {
	GDSOFTCLASS(FakeOldFile, FileAccess);

	Vector<uint8_t> bytes;
	String fake_path;
	bool opened = true;
	mutable uint64_t pos = 0;

protected:
	virtual BitField<UnixPermissionFlags> _get_unix_permissions(const String &p_file) override { return 0; }
	virtual Error _set_unix_permissions(const String &p_file, BitField<UnixPermissionFlags> p_permissions) override { return FAILED; }
	virtual bool _get_hidden_attribute(const String &p_file) override { return false; }
	virtual Error _set_hidden_attribute(const String &p_file, bool p_hidden) override { return ERR_UNAVAILABLE; }
	virtual bool _get_read_only_attribute(const String &p_file) override { return false; }
	virtual Error _set_read_only_attribute(const String &p_file, bool p_ro) override { return ERR_UNAVAILABLE; }
	virtual Error open_internal(const String &p_path, int p_mode_flags) override { return ERR_UNAVAILABLE; }
	virtual uint64_t _get_modified_time(const String &p_file) override { return 0; }
	virtual uint64_t _get_access_time(const String &p_file) override { return 0; }
	virtual int64_t _get_size(const String &p_file) override { return -1; }

public:
	// Recorded interactions.
	mutable int get_buffer_calls = 0;
	String last_exists_query;

	FakeOldFile(const String &p_path, const Vector<uint8_t> &p_bytes, bool p_opened = true) {
		fake_path = p_path;
		bytes = p_bytes;
		opened = p_opened;
	}

	virtual String get_path() const override { return fake_path; }
	virtual bool is_open() const override { return opened; }
	virtual void seek(uint64_t p_position) override { pos = MIN(p_position, (uint64_t)bytes.size()); }
	virtual void seek_end(int64_t p_position = 0) override { pos = bytes.size() + p_position; }
	virtual uint64_t get_position() const override { return pos; }
	virtual uint64_t get_length() const override { return bytes.size(); }
	virtual bool eof_reached() const override { return pos >= (uint64_t)bytes.size(); }
	virtual uint64_t get_buffer(uint8_t *p_dst, uint64_t p_length) const override {
		get_buffer_calls++;
		uint64_t n = MIN(p_length, (uint64_t)bytes.size() - pos);
		memcpy(p_dst, bytes.ptr() + pos, n);
		pos += n;
		return n;
	}
	virtual Error get_error() const override { return OK; }
	virtual Error resize(int64_t p_length) override { return ERR_UNAVAILABLE; }
	virtual void flush() override {}
	virtual bool store_buffer(const uint8_t *p_src, uint64_t p_length) override { return false; }
	virtual void close() override { opened = false; }
	virtual bool file_exists(const String &p_name) override {
		last_exists_query = p_name;
		return p_name == fake_path;
	}
};

// Makes sure a PackedData singleton exists for the duration of a test.
struct PackedDataScope {
	PackedData *owned = nullptr;
	PackedDataScope() {
		if (!PackedData::get_singleton()) {
			owned = memnew(PackedData);
		}
	}
	~PackedDataScope() {
		if (owned) {
			memdelete(owned);
		}
	}
};

static Vector<uint8_t> bytes_of(const String &p_text) {
	return p_text.to_utf8_buffer();
}

// get_buffer(int) is hidden by the override in FileAccessPatched, so read via the pointer overload.
static Vector<uint8_t> read_bytes(const Ref<FileAccessPatched> &p_file, int p_length) {
	Vector<uint8_t> out;
	out.resize(p_length);
	uint64_t got = p_file->get_buffer(out.ptrw(), p_length);
	out.resize(got);
	return out;
}

// Writes `p_prefix_padding` junk bytes followed by the delta into a temp file and
// registers it as a delta patch for `p_virtual_path`.
static String register_patch(const String &p_virtual_path, const Vector<uint8_t> &p_delta, int p_prefix_padding = 0) {
	String pack_path = TestUtils::get_temp_path(p_virtual_path.get_file() + ".patch");
	Ref<FileAccess> f = FileAccess::open(pack_path, FileAccess::WRITE);
	REQUIRE(f.is_valid());
	for (int i = 0; i < p_prefix_padding; i++) {
		f->store_8(0xEE);
	}
	f->store_buffer(p_delta.ptr(), p_delta.size());
	f.unref();

	uint8_t md5[16] = {};
	PackedData::get_singleton()->add_path(pack_path, p_virtual_path, p_prefix_padding, p_delta.size(), md5, nullptr, true, false, false, true);
	return pack_path;
}

// NOTE: every test uses its own virtual path, because patches accumulate per path in PackedData.

TEST_CASE("[FileAccessPatched] open_custom rejects a closed file") {
	Ref<FakeOldFile> closed = memnew(FakeOldFile("res://godot_test_patched/closed.bin", bytes_of("abc"), false));

	Ref<FileAccessPatched> patched;
	patched.instantiate();
	CHECK_FALSE(patched->is_open());
	CHECK_EQ(patched->open_custom(closed), Error::ERR_FILE_CANT_OPEN);
	CHECK_FALSE(patched->is_open());
	CHECK_EQ(patched->get_error(), Error::ERR_FILE_CANT_OPEN);
}

TEST_CASE("[FileAccessPatched] Without patches the old content is served unchanged") {
	PackedDataScope scope;
	Vector<uint8_t> content = bytes_of("unpatched content");
	Ref<FakeOldFile> old_file = memnew(FakeOldFile("res://godot_test_patched/none.bin", content));

	Ref<FileAccessPatched> patched;
	patched.instantiate();
	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);
	CHECK(patched->is_open());

	CHECK_EQ(patched->get_length(), uint64_t(content.size()));
	CHECK_EQ(patched->get_position(), uint64_t(0));
	CHECK_FALSE(patched->eof_reached());
	CHECK_EQ(read_bytes(patched, content.size()), content);
	CHECK(patched->eof_reached());

	// The fake was asked to provide its data (mock-style interaction check).
	CHECK_GT(old_file->get_buffer_calls, 0);
}

TEST_CASE("[FileAccessPatched] A registered delta patch is applied") {
	PackedDataScope scope;
	Vector<uint8_t> old_data = bytes_of("Version one of the file, with some shared text.");
	Vector<uint8_t> new_data = bytes_of("Version two of the file, with some shared text!");

	Vector<uint8_t> delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);

	int padding = 0;
	SUBCASE("Patch at the start of the pack file") {
		padding = 0;
	}
	SUBCASE("Patch at an offset inside the pack file") {
		padding = 7;
	}
	const String vpath = String("res://godot_test_patched/apply_") + itos(padding) + ".bin";
	String pack_path = register_patch(vpath, delta, padding);

	Ref<FakeOldFile> old_file = memnew(FakeOldFile(vpath, old_data));
	Ref<FileAccessPatched> patched;
	patched.instantiate();
	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);

	CHECK_EQ(patched->get_length(), uint64_t(new_data.size()));
	CHECK_EQ(read_bytes(patched, new_data.size()), new_data);
	CHECK_EQ(patched->get_error(), Error::ERR_FILE_EOF);

	DirAccess::remove_absolute(pack_path);
}

TEST_CASE("[FileAccessPatched] Navigation and writing operate on the patched data") {
	PackedDataScope scope;
	const String vpath = "res://godot_test_patched/nav.bin";
	Vector<uint8_t> old_data = bytes_of("0123456789");
	Vector<uint8_t> new_data = bytes_of("ABCDEFGHIJKL");

	Vector<uint8_t> delta;
	REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);
	String pack_path = register_patch(vpath, delta);

	Ref<FakeOldFile> old_file = memnew(FakeOldFile(vpath, old_data));
	Ref<FileAccessPatched> patched;
	patched.instantiate();
	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);

	patched->seek(3);
	CHECK_EQ(patched->get_position(), uint64_t(3));
	uint8_t b = 0;
	CHECK_EQ(patched->get_buffer(&b, 1), uint64_t(1));
	CHECK_EQ(b, 'D');

	patched->seek_end(-2);
	CHECK_EQ(patched->get_position(), uint64_t(10));

	// store_buffer writes into the in-memory patched copy.
	patched->seek(0);
	const uint8_t z[2] = { 'Z', 'Z' };
	CHECK(patched->store_buffer(z, 2));
	patched->flush();
	patched->seek(0);
	CHECK_EQ(read_bytes(patched, 2), bytes_of("ZZ"));

	// Unsupported operations.
	CHECK_EQ(patched->resize(5), Error::ERR_UNAVAILABLE);

	DirAccess::remove_absolute(pack_path);
}

TEST_CASE("[FileAccessPatched] A corrupt patch puts the file into an error state") {
	PackedDataScope scope;
	const String vpath = "res://godot_test_patched/corrupt.bin";
	Vector<uint8_t> garbage = bytes_of("this is definitely not a GDDL delta");
	String pack_path = register_patch(vpath, garbage);

	Ref<FakeOldFile> old_file = memnew(FakeOldFile(vpath, bytes_of("old")));
	Ref<FileAccessPatched> patched;
	patched.instantiate();
	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);

	ERR_PRINT_OFF;
	CHECK_EQ(patched->get_length(), uint64_t(0));
	ERR_PRINT_ON;
	CHECK_NE(patched->get_error(), Error::OK);

	// Once failed, every operation falls back to its error value.
	CHECK_EQ(patched->get_position(), uint64_t(0));
	CHECK(patched->eof_reached());
	uint8_t buf[4];
	CHECK_EQ(patched->get_buffer(buf, 4), uint64_t(0));
	const uint8_t src[1] = { 1 };
	CHECK_FALSE(patched->store_buffer(src, 1));
	patched->seek(1);
	patched->seek_end(0);
	patched->flush();

	DirAccess::remove_absolute(pack_path);
}

TEST_CASE("[FileAccessPatched] A missing pack file for a patch is reported") {
	PackedDataScope scope;
	const String vpath = "res://godot_test_patched/missing_pack.bin";
	uint8_t md5[16] = {};
	PackedData::get_singleton()->add_path(TestUtils::get_temp_path("does_not_exist.patch"), vpath, 0, 10, md5, nullptr, true, false, false, true);

	Ref<FakeOldFile> old_file = memnew(FakeOldFile(vpath, bytes_of("old")));
	Ref<FileAccessPatched> patched;
	patched.instantiate();
	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);

	ERR_PRINT_OFF;
	CHECK_EQ(patched->get_length(), uint64_t(0));
	ERR_PRINT_ON;
	CHECK_NE(patched->get_error(), Error::OK);
}

TEST_CASE("[FileAccessPatched] close resets the state and file_exists delegates") {
	PackedDataScope scope;
	const String vpath = "res://godot_test_patched/close.bin";
	Ref<FakeOldFile> old_file = memnew(FakeOldFile(vpath, bytes_of("abc")));

	Ref<FileAccessPatched> patched;
	patched.instantiate();

	ERR_PRINT_OFF;
	CHECK_FALSE(patched->file_exists("anything")); // No old file yet.
	ERR_PRINT_ON;

	REQUIRE_EQ(patched->open_custom(old_file), Error::OK);
	CHECK(patched->file_exists(vpath));
	CHECK_EQ(old_file->last_exists_query, vpath);
	CHECK_FALSE(patched->file_exists("res://other.bin"));

	patched->close();
	CHECK_FALSE(patched->is_open());
	CHECK_EQ(patched->get_error(), Error::OK);
}

} // namespace TestFileAccessPatched
