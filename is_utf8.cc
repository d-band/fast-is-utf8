#include <node_api.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

static inline __m256i push_last_byte_of_a_to_b(__m256i a, __m256i b) {
  return _mm256_alignr_epi8(b, _mm256_permute2x128_si256(a, b, 0x21), 15);
}

static inline __m256i push_last_2bytes_of_a_to_b(__m256i a, __m256i b) {
  return _mm256_alignr_epi8(b, _mm256_permute2x128_si256(a, b, 0x21), 14);
}

// all byte values must be no larger than 0xF4
static inline void avxcheckSmallerThan0xF4(__m256i current_bytes, __m256i *has_error) {
  // unsigned, saturates to 0 below max
  *has_error = _mm256_or_si256(*has_error, _mm256_subs_epu8(current_bytes, _mm256_set1_epi8(0xF4)));
}

static inline __m256i avxcontinuationLengths(__m256i high_nibbles) {
  return _mm256_shuffle_epi8(
    _mm256_setr_epi8(1, 1, 1, 1, 1, 1, 1, 1, // 0xxx (ASCII)
                     0, 0, 0, 0,             // 10xx (continuation)
                     2, 2,                   // 110x
                     3,                      // 1110
                     4, // 1111, next should be 0 (not checked here)
                     1, 1, 1, 1, 1, 1, 1, 1, // 0xxx (ASCII)
                     0, 0, 0, 0,             // 10xx (continuation)
                     2, 2,                   // 110x
                     3,                      // 1110
                     4 // 1111, next should be 0 (not checked here)
                     ),
    high_nibbles);
}

static inline __m256i avxcarryContinuations(__m256i initial_lengths, __m256i previous_carries) {

  __m256i right1 = _mm256_subs_epu8(
    push_last_byte_of_a_to_b(previous_carries, initial_lengths),
    _mm256_set1_epi8(1));
  __m256i sum = _mm256_add_epi8(initial_lengths, right1);
  __m256i right2 = _mm256_subs_epu8(
    push_last_2bytes_of_a_to_b(previous_carries, sum), _mm256_set1_epi8(2));
  return _mm256_add_epi8(sum, right2);
}

static inline void avxcheckContinuations(__m256i initial_lengths, __m256i carries, __m256i *has_error) {
  // overlap || underlap
  // carry > length && length > 0 || !(carry > length) && !(length > 0)
  // (carries > length) == (lengths > 0)
  __m256i overunder = _mm256_cmpeq_epi8(
    _mm256_cmpgt_epi8(carries, initial_lengths),
    _mm256_cmpgt_epi8(initial_lengths, _mm256_setzero_si256()));

  *has_error = _mm256_or_si256(*has_error, overunder);
}

// when 0xED is found, next byte must be no larger than 0x9F
// when 0xF4 is found, next byte must be no larger than 0x8F
// next byte must be continuation, ie sign bit is set, so signed < is ok
static inline void avxcheckFirstContinuationMax(__m256i current_bytes, __m256i off1_current_bytes, __m256i *has_error) {
  __m256i maskED = _mm256_cmpeq_epi8(off1_current_bytes, _mm256_set1_epi8(0xED));
  __m256i maskF4 = _mm256_cmpeq_epi8(off1_current_bytes, _mm256_set1_epi8(0xF4));

  __m256i badfollowED = _mm256_and_si256(_mm256_cmpgt_epi8(current_bytes, _mm256_set1_epi8(0x9F)), maskED);
  __m256i badfollowF4 = _mm256_and_si256(_mm256_cmpgt_epi8(current_bytes, _mm256_set1_epi8(0x8F)), maskF4);

  *has_error = _mm256_or_si256(*has_error, _mm256_or_si256(badfollowED, badfollowF4));
}

// map off1_hibits => error condition
// hibits     off1    cur
// C       => < C2 && true
// E       => < E1 && < A0
// F       => < F1 && < 90
// else      false && false
static inline void avxcheckOverlong(
  __m256i current_bytes,
  __m256i off1_current_bytes, __m256i hibits,
  __m256i previous_hibits,
  __m256i *has_error
) {
  __m256i off1_hibits = push_last_byte_of_a_to_b(previous_hibits, hibits);
  __m256i initial_mins = _mm256_shuffle_epi8(
    _mm256_setr_epi8(-128, -128, -128, -128, -128, -128, -128, -128, -128,
                     -128, -128, -128, // 10xx => false
                     0xC2, -128,       // 110x
                     0xE1,             // 1110
                     0xF1, -128, -128, -128, -128, -128, -128, -128, -128,
                     -128, -128, -128, -128, // 10xx => false
                     0xC2, -128,             // 110x
                     0xE1,                   // 1110
                     0xF1),
    off1_hibits);

  __m256i initial_under = _mm256_cmpgt_epi8(initial_mins, off1_current_bytes);

  __m256i second_mins = _mm256_shuffle_epi8(
    _mm256_setr_epi8(-128, -128, -128, -128, -128, -128, -128, -128, -128,
                     -128, -128, -128, // 10xx => false
                     127, 127,         // 110x => true
                     0xA0,             // 1110
                     0x90, -128, -128, -128, -128, -128, -128, -128, -128,
                     -128, -128, -128, -128, // 10xx => false
                     127, 127,               // 110x => true
                     0xA0,                   // 1110
                     0x90),
    off1_hibits);
  __m256i second_under = _mm256_cmpgt_epi8(second_mins, current_bytes);
  *has_error = _mm256_or_si256(*has_error, _mm256_and_si256(initial_under, second_under));
}

struct avx_processed_utf_bytes {
  __m256i rawbytes;
  __m256i high_nibbles;
  __m256i carried_continuations;
};

static inline void avx_count_nibbles(__m256i bytes, struct avx_processed_utf_bytes *answer) {
  answer->rawbytes = bytes;
  answer->high_nibbles = _mm256_and_si256(_mm256_srli_epi16(bytes, 4), _mm256_set1_epi8(0x0F));
}

// check whether the current bytes are valid UTF-8
// at the end of the function, previous gets updated
static struct avx_processed_utf_bytes avxcheckUTF8Bytes(
  __m256i current_bytes, struct avx_processed_utf_bytes *previous, __m256i *has_error
) {
  struct avx_processed_utf_bytes pb;
  avx_count_nibbles(current_bytes, &pb);

  avxcheckSmallerThan0xF4(current_bytes, has_error);

  __m256i initial_lengths = avxcontinuationLengths(pb.high_nibbles);

  pb.carried_continuations = avxcarryContinuations(initial_lengths, previous->carried_continuations);

  avxcheckContinuations(initial_lengths, pb.carried_continuations, has_error);

  __m256i off1_current_bytes = push_last_byte_of_a_to_b(previous->rawbytes, pb.rawbytes);
  avxcheckFirstContinuationMax(current_bytes, off1_current_bytes, has_error);

  avxcheckOverlong(current_bytes, off1_current_bytes, pb.high_nibbles, previous->high_nibbles, has_error);
  return pb;
}

static inline void check_utf8(__m256i input_lo, __m256i input_hi, __m256i &has_error, struct avx_processed_utf_bytes &previous) {
  __m256i highbit = _mm256_set1_epi8(0x80);
  if ((_mm256_testz_si256(_mm256_or_si256(input_lo, input_hi), highbit)) == 1) {
    // it is ascii, we just check continuation
    has_error = _mm256_or_si256(
      _mm256_cmpgt_epi8(
        previous.carried_continuations,
        _mm256_setr_epi8(9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                         9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 1)),
      has_error);
  } else {
    // it is not ascii so we have to do heavy work
    previous = avxcheckUTF8Bytes(input_lo, &previous, &has_error);
    previous = avxcheckUTF8Bytes(input_hi, &previous, &has_error);
  }
}

bool is_utf8(const uint8_t *buf, size_t len) {
  __m256i has_error = _mm256_setzero_si256();
  struct avx_processed_utf_bytes previous;
  previous.rawbytes = _mm256_setzero_si256();
  previous.high_nibbles = _mm256_setzero_si256();
  previous.carried_continuations = _mm256_setzero_si256();

  size_t lenminus64 = len < 64 ? 0 : len - 64;
  size_t idx = 0;
  for (; idx < lenminus64; idx += 64) {
    __m256i input_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(buf + idx + 0));
    __m256i input_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(buf + idx + 32));
    check_utf8(input_lo, input_hi, has_error, previous);
    if (_mm256_testz_si256(has_error, has_error) == 0) {
      return false;
    }
  }
  if (idx < len) {
    uint8_t tmpbuf[64];
    memset(tmpbuf, 0x20, 64);
    memcpy(tmpbuf, buf + idx, len - idx);
    __m256i input_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(tmpbuf + 0));
    __m256i input_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(tmpbuf + 32));
    check_utf8(input_lo, input_hi, has_error, previous);
  }
  return _mm256_testz_si256(has_error, has_error) != 0;
}

bool is_utf8(const void *buf, size_t len) {
  return is_utf8(reinterpret_cast<const uint8_t *>(buf), len);
}

napi_value is_utf8_api (napi_env env, napi_callback_info info) {
  napi_value argv[1];
  size_t argc = 1;

  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);

  if (argc < 1) {
    napi_throw_error(env, "EINVAL", "Too few arguments");
    return NULL;
  }

  void *buf;
  size_t len;
  napi_value result;

  if (napi_get_buffer_info(env, argv[0], &buf, &len) != napi_ok) {
    napi_throw_error(env, "EINVAL", "Expected buffer");
    return NULL;
  }
  napi_get_boolean(env, is_utf8(buf, len), &result);
  return result;
}


napi_value init (napi_env env, napi_value exports) {
  napi_value fn;
  napi_create_function(env, NULL, 0, is_utf8_api, NULL, &fn);
  napi_set_named_property(env, exports, "is_utf8", fn);
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
