/* Area:	ffi_call
   Purpose:	Check that a function called via ffi_call can suspend with JSPI.
   Limitations:	Emscripten only, needs -sJSPI and a JSPI-enabled engine.
   PR:		none.
   Originator:	Hood Chatham 20260915	 */

/* { dg-do run { target wasm*-*-* } } */
#include "ffitest.h"
#include <emscripten.h>
#include <stdarg.h>

/* JSPI suspends the whole wasm stack while the promise is pending. That
   only works if every frame between the JSPI entry point (main) and the
   suspending import is a wasm frame, so ffi_call must not make the onward
   call from JavaScript. Exercise each kind of return: void, i32 (with
   widening), i64, f32, f64, struct-by-pointer, and a variadic call.  */

EM_ASYNC_JS(int, async_add_one, (int x), {
  await new Promise(resolve => setTimeout(resolve, 1));
  return x + 1;
});

static int n_calls = 0;

typedef struct { double a; int b; long long c; } big_t;

static void void_fn(int *out, int x) { n_calls++; *out = async_add_one(x); }
static signed char schar_fn(int x, short y) { n_calls++; return (signed char)(async_add_one(x) + y); }
static long long i64_fn(long long x) { n_calls++; return ((long long)async_add_one((int)x) << 40) | 7; }
static float float_fn(float x) { n_calls++; return async_add_one((int)x) + 0.25f; }
static double double_fn(double x) { n_calls++; return async_add_one((int)x) + 0.5; }
static big_t struct_fn(big_t in)
{
  big_t out;
  n_calls++;
  out.a = in.a * 2;
  out.b = async_add_one(in.b);
  out.c = in.c + 1;
  return out;
}
static int va_fn(int n, ...)
{
  va_list ap;
  int sum = 0;
  n_calls++;
  va_start(ap, n);
  for (int i = 0; i < n; i++)
    sum += async_add_one(va_arg(ap, int));
  va_end(ap);
  return sum;
}

int main (void)
{
  ffi_cif cif;
  void *values[4];

  /* void f(int *, int) */
  {
    ffi_type *args[] = { &ffi_type_pointer, &ffi_type_sint };
    int out = 0, *outp = &out, x = 41;
    values[0] = &outp; values[1] = &x;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_void, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(void_fn), NULL, values);
    CHECK(out == 42);
  }

  /* signed char f(int, short): result widened to ffi_arg */
  {
    ffi_type *args[] = { &ffi_type_sint, &ffi_type_sshort };
    int x = -10; short y = 3;
    ffi_arg rc = 0x7777;
    values[0] = &x; values[1] = &y;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_schar, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(schar_fn), &rc, values);
    CHECK((ffi_sarg)rc == -6);
  }

  /* long long f(long long) */
  {
    ffi_type *args[] = { &ffi_type_sint64 };
    long long x = 5, rc = 0;
    values[0] = &x;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_sint64, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(i64_fn), &rc, values);
    CHECK(rc == ((6LL << 40) | 7));
  }

  /* float f(float) */
  {
    ffi_type *args[] = { &ffi_type_float };
    float x = 2.0f, rc = 0;
    values[0] = &x;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_float, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(float_fn), &rc, values);
    CHECK(rc == 3.25f);
  }

  /* double f(double) */
  {
    ffi_type *args[] = { &ffi_type_double };
    double x = 2.0, rc = 0;
    values[0] = &x;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_double, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(double_fn), &rc, values);
    CHECK(rc == 3.5);
  }

  /* big_t f(big_t): returned through a pointer argument */
  {
    ffi_type *elements[] = { &ffi_type_double, &ffi_type_sint, &ffi_type_sint64, NULL };
    ffi_type big = { 0, 0, FFI_TYPE_STRUCT, elements };
    ffi_type *args[] = { &big };
    big_t in = { 1.5, 10, 1LL << 33 }, out;
    values[0] = &in;
    CHECK(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &big, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(struct_fn), &out, values);
    CHECK(out.a == 3.0);
    CHECK(out.b == 11);
    CHECK(out.c == (1LL << 33) + 1);
    /* Discarded struct result: the callee still needs scratch space. */
    ffi_call(&cif, FFI_FN(struct_fn), NULL, values);
  }

  /* int f(int n, ...) */
  {
    ffi_type *args[] = { &ffi_type_sint, &ffi_type_sint, &ffi_type_sint };
    int n = 2, a = 10, b = 20;
    ffi_arg rc = 0;
    values[0] = &n; values[1] = &a; values[2] = &b;
    CHECK(ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI, 1, 3, &ffi_type_sint, args) == FFI_OK);
    ffi_call(&cif, FFI_FN(va_fn), &rc, values);
    CHECK((int)rc == 11 + 21);
  }

  printf("%d calls\n", n_calls);
  /* { dg-output "8 calls" } */
  exit(0);
}
