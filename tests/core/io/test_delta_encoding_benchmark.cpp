/**************************************************************************/
/*  test_delta_encoding_benchmark.cpp                                     */
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

TEST_FORCE_LINK(test_delta_encoding_benchmark)

#include "core/io/delta_encoding.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

namespace TestDeltaEncodingBenchmark {

// Semi-compressible deterministic data, similar in spirit to real resource files.
static Vector<uint8_t> make_data(int p_size) {
	Vector<uint8_t> out;
	out.resize(p_size);
	uint32_t state = 12345;
	for (int i = 0; i < p_size; i++) {
		state = state * 1664525u + 1013904223u;
		out.write[i] = (((state >> 24) % 16) == 0) ? (uint8_t)(state >> 16) : (uint8_t)(i % 61);
	}
	return out;
}

// Returns the best (lowest) average time per call in microseconds over several rounds.
template <typename F>
static double bench_us_per_call(int p_iterations, int p_rounds, F p_func) {
	double best = 1e30;
	for (int r = 0; r < p_rounds; r++) {
		uint64_t start = OS::get_singleton()->get_ticks_usec();
		for (int i = 0; i < p_iterations; i++) {
			p_func();
		}
		uint64_t end = OS::get_singleton()->get_ticks_usec();
		double per_call = double(end - start) / double(p_iterations);
		if (per_call < best) {
			best = per_call;
		}
	}
	return best;
}

// Skipped by default. Run it with: --test-case="*Benchmark*" --no-skip
// Environment variable DELTA_BENCH_SCALE (percent, default 100) scales the iteration counts.
TEST_CASE("[DeltaEncoding][Benchmark] Encode and decode timing" * doctest::skip(true)) {
	int scale = 100;
	String scale_env = OS::get_singleton()->get_environment("DELTA_BENCH_SCALE");
	if (!scale_env.is_empty()) {
		scale = MAX(1, scale_env.to_int());
	}
	const int rounds = scale < 10 ? 1 : 5;

	struct Scenario {
		int size;
		int iterations;
	};
	const Scenario scenarios[] = {
		{ 64, 20000 },
		{ 4096, 2000 },
		{ 262144, 50 },
	};

	print_line("DELTA_BENCHMARK size_bytes encode_us_per_call decode_us_per_call delta_bytes");

	for (const Scenario &s : scenarios) {
		Vector<uint8_t> old_data = make_data(s.size);
		Vector<uint8_t> new_data = old_data;
		for (int i = 0; i < s.size; i += MAX(1, s.size / 16)) {
			new_data.write[i] ^= 0x5A;
		}

		int iterations = MAX(1, s.iterations * scale / 100);

		Vector<uint8_t> delta;
		REQUIRE_EQ(DeltaEncoding::encode_delta(old_data, new_data, delta), Error::OK);

		size_t sink = 0;
		double enc_us = bench_us_per_call(iterations, rounds, [&]() {
			Vector<uint8_t> d;
			DeltaEncoding::encode_delta(old_data, new_data, d);
			sink += d.size();
		});
		double dec_us = bench_us_per_call(iterations, rounds, [&]() {
			Vector<uint8_t> n;
			DeltaEncoding::decode_delta(old_data, delta, n);
			sink += n.size();
		});

		// Use the result so the calls cannot be optimized away, and check correctness once.
		Vector<uint8_t> check;
		REQUIRE_EQ(DeltaEncoding::decode_delta(old_data, delta, check), Error::OK);
		CHECK_EQ(check, new_data);
		CHECK_GT(sink, size_t(0));

		print_line(vformat("DELTA_BENCHMARK %d %s %s %d", s.size, String::num(enc_us, 2), String::num(dec_us, 2), delta.size()));
	}
}

} // namespace TestDeltaEncodingBenchmark
