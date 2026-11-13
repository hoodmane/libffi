/* -----------------------------------------------------------------------
   ffi.c - Copyright (c) 2018-2023  Hood Chatham, Brion Vibber, Kleis Auke Wolthuizen, and others.
           Copyright (c) 2026  Anthony Green

   wasm32/emscripten Foreign Function Interface

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   ``Software''), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be included
   in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED ``AS IS'', WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   ----------------------------------------------------------------------- */

#include <ffi.h>
#include <ffi_common.h>

#include <stdlib.h>
#include <stdint.h>

#include <emscripten/emscripten.h>

#ifdef DEBUG_F
#define LOG_DEBUG(args...)  \
  console.warn(`====LIBFFI(line __LINE__)`, args)
#else
#define LOG_DEBUG(args...) 0
#endif

#define EM_JS_MACROS(ret, name, args, body...) EM_JS(ret, name, args, body)

EM_JS_DEPS(libffi, "$getWasmTableEntry,$setWasmTableEntry,$getEmptyTableSlot,$wasmTable,$stackSave,$stackAlloc,$stackRestore");

#define DEREF_U8(addr, offset) HEAPU8[addr + offset]
#define DEREF_S8(addr, offset) HEAP8[addr + offset]
#define DEREF_U16(addr, offset) HEAPU16[HEAP_INDEX_16(addr) + offset]
#define DEREF_S16(addr, offset) HEAP16[HEAP_INDEX_16(addr) + offset]
#define DEREF_U32(addr, offset) HEAPU32[HEAP_INDEX_32(addr) + offset]
#define DEREF_S32(addr, offset) HEAP32[HEAP_INDEX_32(addr) + offset]

#define DEREF_F32(addr, offset) HEAPF32[HEAP_INDEX_32(addr) + offset]
#define DEREF_F64(addr, offset) HEAPF64[HEAP_INDEX_64(addr) + offset]
#define DEREF_U64(addr, offset) HEAPU64[HEAP_INDEX_64(addr) + offset]

#define CHECK_FIELD_OFFSET(struct, field, offset)                                  \
  _Static_assert(                                                                  \
    offsetof(struct, field) == offset,                                             \
    "Memory layout of '" #struct "' has changed: '" #field "' is in an unexpected location");

#if __SIZEOF_POINTER__ == 4

#define FFI_EMSCRIPTEN_ABI FFI_WASM32_EMSCRIPTEN
#define PTR_SIG 'i'
#define PTR_WASM_TYPE 0x7f

#define DEC_PTR(p) p
#define ENC_PTR(p) p

// Pointers reach JS as signed 32-bit Numbers, so an address at or above 2 GiB
// is negative and needs an unsigned shift to become a heap view index.
#define HEAP_INDEX_16(addr) ((addr) >>> 1)
#define HEAP_INDEX_32(addr) ((addr) >>> 2)
#define HEAP_INDEX_64(addr) ((addr) >>> 3)

#define DEREF_PTR(addr, offset) DEREF_U32(addr, offset)
#define DEREF_PTR_NUMBER(addr, offset) DEREF_PTR(addr, offset)

// Store an integral return value widened to ffi_arg (32 bits on wasm32), as
// the libffi API promises for integral returns narrower than a register.
// The typed-array store wraps modulo 2^32, so a sign-extended negative
// Number is stored correctly by both variants.
#define STORE_ARG_WIDENED_UNSIGNED(rvalue, x) (DEREF_U32(rvalue, 0) = (x))
#define STORE_ARG_WIDENED_SIGNED(rvalue, x) (DEREF_U32(rvalue, 0) = (x))

CHECK_FIELD_OFFSET(ffi_cif, abi, 4*0);
CHECK_FIELD_OFFSET(ffi_cif, nargs, 4*1);
CHECK_FIELD_OFFSET(ffi_cif, arg_types, 4*2);
CHECK_FIELD_OFFSET(ffi_cif, rtype, 4*3);
CHECK_FIELD_OFFSET(ffi_cif, flags, 4*5);
CHECK_FIELD_OFFSET(ffi_cif, nfixedargs, 4*6);

#define CIF__ABI(addr) DEREF_U32(addr, 0)
#define CIF__NARGS(addr) DEREF_U32(addr, 1)
#define CIF__ARGTYPES(addr) DEREF_U32(addr, 2)
#define CIF__RTYPE(addr) DEREF_U32(addr, 3)
#define CIF__FLAGS(addr) DEREF_U32(addr, 5)
#define CIF__NFIXEDARGS(addr) DEREF_U32(addr, 6)

CHECK_FIELD_OFFSET(ffi_type, size, 0);
CHECK_FIELD_OFFSET(ffi_type, alignment, 4);
CHECK_FIELD_OFFSET(ffi_type, type, 6);
CHECK_FIELD_OFFSET(ffi_type, elements, 8);

#define FFI_TYPE__SIZE(addr) DEREF_U32(addr, 0)
#define FFI_TYPE__ALIGN(addr) DEREF_U16(addr + 4, 0)
#define FFI_TYPE__TYPEID(addr) DEREF_U16(addr + 6, 0)
#define FFI_TYPE__ELEMENTS(addr) DEREF_U32(addr + 8, 0)

#elif __SIZEOF_POINTER__ == 8

#define FFI_EMSCRIPTEN_ABI FFI_WASM64_EMSCRIPTEN
#define PTR_SIG 'j'
#define PTR_WASM_TYPE 0x7e

// DEC_PTR casts a pointer value (coming from Wasm) represented as BigInt (i64) to Number (i53).
// This should be used for a pointer that is expected to be within the i53 range. If the pointer
// value is outside the Number's range, the value will become NaN.
#define DEC_PTR(p) bigintToI53Checked(p)
// ENC_PTR casts a pointer value represented as Number to BigInt (i64)
#define ENC_PTR(p) BigInt(p)

// JS shifts truncate their operand to 32 bits, so divide to keep addresses
// at or above 4 GiB intact.
#define HEAP_INDEX_16(addr) ((addr) / 2)
#define HEAP_INDEX_32(addr) ((addr) / 4)
#define HEAP_INDEX_64(addr) ((addr) / 8)

#define DEREF_PTR(addr, offset) DEREF_U64(addr, offset)
#define DEREF_PTR_NUMBER(addr, offset) DEC_PTR(DEREF_PTR(addr, offset))

// Store an integral return value widened to ffi_arg (64 bits on wasm64), as
// the libffi API promises for integral returns narrower than a register.
// BigInt masking treats the value as infinite-precision two's complement, so
// the unsigned variant zero-extends a possibly-negative i32 Number, while
// the BigUint64Array store wraps the signed variant modulo 2^64, which is
// sign extension.
#define STORE_ARG_WIDENED_UNSIGNED(rvalue, x) \
  (DEREF_U64(rvalue, 0) = BigInt(x) & BigInt(4294967295))
#define STORE_ARG_WIDENED_SIGNED(rvalue, x) (DEREF_U64(rvalue, 0) = BigInt(x))

CHECK_FIELD_OFFSET(ffi_cif, abi, 0);
CHECK_FIELD_OFFSET(ffi_cif, nargs, 4);
CHECK_FIELD_OFFSET(ffi_cif, arg_types, 8);
CHECK_FIELD_OFFSET(ffi_cif, rtype, 16);
CHECK_FIELD_OFFSET(ffi_cif, flags, 28);
CHECK_FIELD_OFFSET(ffi_cif, nfixedargs, 32);

#define CIF__ABI(addr) DEREF_U32(addr, 0)
#define CIF__NARGS(addr) DEREF_U32(addr + 4,  0)
#define CIF__ARGTYPES(addr) DEREF_U64(addr + 8,  0)
#define CIF__RTYPE(addr) DEREF_U64(addr + 16, 0)
#define CIF__FLAGS(addr) DEREF_U32(addr + 28, 0)
#define CIF__NFIXEDARGS(addr) DEREF_U32(addr + 32, 0)

CHECK_FIELD_OFFSET(ffi_type, size, 0);
CHECK_FIELD_OFFSET(ffi_type, alignment, 8);
CHECK_FIELD_OFFSET(ffi_type, type, 10);
CHECK_FIELD_OFFSET(ffi_type, elements, 16);

#define FFI_TYPE__SIZE(addr) DEREF_U64(addr, 0)
#define FFI_TYPE__ALIGN(addr) DEREF_U16(addr + 8, 0)
#define FFI_TYPE__TYPEID(addr) DEREF_U16(addr + 10, 0)
#define FFI_TYPE__ELEMENTS(addr) DEREF_U64(addr + 16, 0)

#else
#error "Unknown pointer size"
#endif

#define ALIGN_ADDRESS(addr, align) (addr &= (~((align) - 1)))
#define STACK_ALLOC(stack, size, align) ((stack -= (size)), ALIGN_ADDRESS(stack, align))

// Wasm engines allow at most 1000 params and 1000 results per function. The
// ffi_call prelude returns all of the arguments plus four bookkeeping values
// as a multivalue result, so leave room for those.
#define MAX_ARGS 996

#include <stddef.h>

#define VARARGS_FLAG 1

#define FFI_OK_MACRO 0
_Static_assert(FFI_OK_MACRO == FFI_OK, "FFI_OK must be 0");

#define FFI_BAD_TYPEDEF_MACRO 1
_Static_assert(FFI_BAD_TYPEDEF_MACRO == FFI_BAD_TYPEDEF, "FFI_BAD_TYPEDEF must be 1");

ffi_status FFI_HIDDEN
ffi_prep_cif_machdep(ffi_cif *cif)
{
  if (cif->abi != FFI_EMSCRIPTEN_ABI)
    return FFI_BAD_ABI;
  // This is called after ffi_prep_cif_machdep_var so we need to avoid
  // overwriting cif->nfixedargs.
  if (!(cif->flags & VARARGS_FLAG))
    cif->nfixedargs = cif->nargs;
  if (cif->nargs > MAX_ARGS)
    return FFI_BAD_TYPEDEF;
  if (cif->rtype->type == FFI_TYPE_COMPLEX)
    return FFI_BAD_TYPEDEF;
  // If they put the COMPLEX type into a struct we won't notice, but whatever.
  for (int i = 0; i < cif->nargs; i++)
    if (cif->arg_types[i]->type == FFI_TYPE_COMPLEX)
      return FFI_BAD_TYPEDEF;
  return FFI_OK;
}

ffi_status FFI_HIDDEN
ffi_prep_cif_machdep_var(ffi_cif *cif, unsigned nfixedargs, unsigned ntotalargs)
{
  cif->flags |= VARARGS_FLAG;
  cif->nfixedargs = nfixedargs;
  // The varargs takes up one extra argument
  if (cif->nfixedargs + 1 > MAX_ARGS)
    return FFI_BAD_TYPEDEF;
  return FFI_OK;
}

/**
 * A Javascript helper function. This takes an argument typ which is a wasm
 * pointer to an ffi_type object. It returns a pair a type and a type id.
 *
 *    - If it is not a struct, return its type and its typeid field.
 *    - If it is a struct of size >= 2, return the type and its typeid (which
 *      will be FFI_TYPE_STRUCT)
 *    - If it is a struct of size 0, return FFI_TYPE_VOID (????? this is broken)
 *    - If it is a struct of size 1, replace it with the single field and apply
 *      the same logic again to that.
 *
 * By always unboxing structs up front, we can avoid messy casework later.
 */
EM_JS_MACROS(
void,
unbox_small_structs, (ffi_type type_ptr), {
  type_ptr = DEC_PTR(type_ptr);
  var type_id = FFI_TYPE__TYPEID(type_ptr);
  while (type_id === FFI_TYPE_STRUCT) {
    // Don't unbox single element structs if they are bigger than 16 bytes. This
    // is a work around for the fact that Python will give incorrect values for
    // the size of the field in these cases: it says that the struct has pointer
    // size and alignment and are of type pointer, even though it is more
    // accurately a struct and has a larger size. Keeping it as a struct here
    // will let us get the ABI right (which is in fact that the true argument is
    // a pointer to the stack... so maybe Python issn't so wrong??)
    //
    // See the Python comment here:
    // https://github.com/python/cpython/blob/a16a9f978f42b8a09297c1efbf33877f6388c403/Modules/_ctypes/stgdict.c#L718-L779
    if (DEC_PTR(FFI_TYPE__SIZE(type_ptr)) > 16) {
      break;
    }
    var elements = DEC_PTR(FFI_TYPE__ELEMENTS(type_ptr));
    var first_element = DEREF_PTR_NUMBER(elements, 0);
    if (first_element === 0) {
      type_id = FFI_TYPE_VOID;
      break;
    } else if (DEREF_PTR_NUMBER(elements, 1) === 0) {
      type_ptr = first_element;
      type_id = FFI_TYPE__TYPEID(first_element);
    } else {
      break;
    }
  }
  return [type_ptr, type_id];
})

/**
 * ffi_call and JSPI
 *
 * ffi_call has to end up calling fn with the marshalled arguments. Doing the
 * call from JavaScript (getWasmTableEntry(fn)(...args)) leaves a JS frame on
 * the stack, so if fn suspends via JSPI the engine traps with "trying to
 * suspend JS frames". Instead, ffi_call calls a small generated wasm
 * trampoline through the function table, and the trampoline makes the onward
 * call with call_indirect:
 *
 *    trampoline(cif, fn, rvalue, avalue):
 *      call prelude(cif, fn, rvalue, avalue)
 *      call_indirect fn(...args)
 *      call epilogue(cif, orig_stack_ptr, rvalue, result)
 *
 * prelude() is a JavaScript helper that marshals the arguments from avalue
 * onto the C stack / into wasm values and returns everything the rest of the
 * body consumes, in stack order: cif, orig_stack_ptr, rvalue, ...args, fn.
 * epilogue() restores the stack pointer and stores the result into rvalue.
 *
 * The trampoline's wasm type only depends on the cif's signature string, so
 * we compile one module per signature, instantiate it once, and cache the
 * table index it lives at.
 */
EM_JS_MACROS(
void *,
ffi_call_get_trampoline_js, (ffi_cif *cif),
{
  cif = DEC_PTR(cif);
  var sig = ffi_cif_to_call_info(cif).sig;
  var index = ffi_call_trampoline_cache.get(sig);
  if (index === undefined) {
    LOG_DEBUG("CREATE_CALL_TRAMPOLINE", "sig:", sig);
    var instance = new WebAssembly.Instance(
      ffi_call_trampoline_module(sig),
      {
        e: {
          p: ffi_call_prelude,
          e: ffi_call_epilogue(sig),
          t: wasmTable,
        },
      },
    );
    index = getEmptyTableSlot();
    setWasmTableEntry(index, instance.exports["f"]);
    ffi_call_trampoline_cache.set(sig, index);
  }
  return ENC_PTR(index);
}

var ffi_call_trampoline_cache = new Map();

// Called by the ffi_call trampoline. Marshals the arguments from avalue and
// returns what the trampoline needs to make the onward call, in operand stack
// order: the epilogue's arguments, then the call_indirect arguments, then the
// table index.
function ffi_call_prelude(cif, fn, rvalue, avalue) {
  cif = DEC_PTR(cif);
  rvalue = DEC_PTR(rvalue);
  avalue = DEC_PTR(avalue);
  var nargs = CIF__NARGS(cif);
  var nfixedargs = CIF__NFIXEDARGS(cif);
  var arg_types_ptr = DEC_PTR(CIF__ARGTYPES(cif));
  var flags = CIF__FLAGS(cif);
  var rtype_unboxed = unbox_small_structs(CIF__RTYPE(cif));
  var rtype_ptr = rtype_unboxed[0];
  var rtype_id = rtype_unboxed[1];
  var orig_stack_ptr = stackSave();
  var cur_stack_ptr = orig_stack_ptr;

  // The epilogue's arguments go first. It gets the original rvalue, which may
  // be NULL, so it knows whether there is anything to store.
  var args = [ENC_PTR(cif), ENC_PTR(orig_stack_ptr), ENC_PTR(rvalue)];

  // If the return type is a struct with multiple entries or a long double, the
  // function takes an extra first argument which is a pointer to return value.
  // Conveniently, we've already received a pointer to return value, so we can
  // just use this.
  if (rtype_id === FFI_TYPE_LONGDOUBLE || rtype_id === FFI_TYPE_STRUCT) {
    if (rvalue === 0) {
      // NULL rvalue: the caller discards the result, but the callee still
      // needs somewhere to write it. Allocate scratch space on the stack;
      // it is released by the stackRestore in the epilogue.
      var rsize = DEC_PTR(FFI_TYPE__SIZE(rtype_ptr));
      var ralign = FFI_TYPE__ALIGN(rtype_ptr);
      STACK_ALLOC(cur_stack_ptr, rsize, ralign);
      rvalue = cur_stack_ptr;
    }
    args.push(ENC_PTR(rvalue));
  }

  // Convert each C argument into the wasm value the callee expects.
  for (var i = 0; i < nfixedargs; i++) {
    var arg_ptr = DEREF_PTR_NUMBER(avalue, i);
    var arg_unboxed = unbox_small_structs(DEREF_PTR(arg_types_ptr, i));
    var arg_type_ptr = arg_unboxed[0];
    var arg_type_id = arg_unboxed[1];

    // It's okay here to always use unsigned integers as long as the size is 32
    // or 64 bits. Smaller sizes get extended to 32 bits differently according
    // to whether they are signed or unsigned.
    switch (arg_type_id) {
    case FFI_TYPE_INT:
    case FFI_TYPE_SINT32:
    case FFI_TYPE_UINT32:
      args.push(DEREF_U32(arg_ptr, 0));
      break;
    case FFI_TYPE_FLOAT:
      args.push(DEREF_F32(arg_ptr, 0));
      break;
    case FFI_TYPE_DOUBLE:
      args.push(DEREF_F64(arg_ptr, 0));
      break;
    case FFI_TYPE_UINT8:
      args.push(DEREF_U8(arg_ptr, 0));
      break;
    case FFI_TYPE_SINT8:
      args.push(DEREF_S8(arg_ptr, 0));
      break;
    case FFI_TYPE_UINT16:
      args.push(DEREF_U16(arg_ptr, 0));
      break;
    case FFI_TYPE_SINT16:
      args.push(DEREF_S16(arg_ptr, 0));
      break;
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
      args.push(DEREF_U64(arg_ptr, 0));
      break;
    case FFI_TYPE_LONGDOUBLE:
      // long double is passed as a pair of BigInts.
      args.push(DEREF_U64(arg_ptr, 0));
      args.push(DEREF_U64(arg_ptr, 1));
      break;
    case FFI_TYPE_STRUCT:
      // Nontrivial structs are passed by pointer.
      // Have to copy the struct onto the stack though because C ABI says it's
      // call by value.
      var size = DEC_PTR(FFI_TYPE__SIZE(arg_type_ptr));
      var align = FFI_TYPE__ALIGN(arg_type_ptr);
      STACK_ALLOC(cur_stack_ptr, size, align);
      HEAP8.subarray(cur_stack_ptr, cur_stack_ptr+size).set(HEAP8.subarray(arg_ptr, arg_ptr + size));
      args.push(ENC_PTR(cur_stack_ptr));
      break;
    case FFI_TYPE_POINTER:
      args.push(DEREF_PTR(arg_ptr, 0));
      break;
    case FFI_TYPE_COMPLEX:
      throw new Error('complex marshalling nyi');
    default:
      throw new Error('Unexpected type ' + arg_type_id);
    }
  }

  // Wasm functions can't directly manipulate the callstack, so varargs
  // arguments have to go on a separate stack. A varags function takes one extra
  // argument which is a pointer to where on the separate stack the args are
  // located. Because stacks are allocated backwards, we have to loop over the
  // varargs backwards.
  //
  // We don't have any way of knowing how many args were actually passed, so we
  // just always copy extra nonsense past the end. The ownwards call will know
  // not to look at it.
  if (flags & VARARGS_FLAG) {
    var struct_arg_info = [];
    for (var i = nargs - 1;  i >= nfixedargs; i--) {
      var arg_ptr = DEREF_PTR_NUMBER(avalue, i);
      var arg_unboxed = unbox_small_structs(DEREF_PTR(arg_types_ptr, i));
      var arg_type_ptr = arg_unboxed[0];
      var arg_type_id = arg_unboxed[1];
      switch (arg_type_id) {
      case FFI_TYPE_UINT8:
      case FFI_TYPE_SINT8:
        STACK_ALLOC(cur_stack_ptr, 1, 1);
        DEREF_U8(cur_stack_ptr, 0) = DEREF_U8(arg_ptr, 0);
        break;
      case FFI_TYPE_UINT16:
      case FFI_TYPE_SINT16:
        STACK_ALLOC(cur_stack_ptr, 2, 2);
        DEREF_U16(cur_stack_ptr, 0) = DEREF_U16(arg_ptr, 0);
        break;
      case FFI_TYPE_INT:
      case FFI_TYPE_UINT32:
      case FFI_TYPE_SINT32:
      case FFI_TYPE_FLOAT:
        STACK_ALLOC(cur_stack_ptr, 4, 4);
        DEREF_U32(cur_stack_ptr, 0) = DEREF_U32(arg_ptr, 0);
        break;
      case FFI_TYPE_DOUBLE:
      case FFI_TYPE_UINT64:
      case FFI_TYPE_SINT64:
        STACK_ALLOC(cur_stack_ptr, 8, 8);
        DEREF_U32(cur_stack_ptr, 0) = DEREF_U32(arg_ptr, 0);
        DEREF_U32(cur_stack_ptr, 1) = DEREF_U32(arg_ptr, 1);
        break;
      case FFI_TYPE_LONGDOUBLE:
        STACK_ALLOC(cur_stack_ptr, 16, 8);
        DEREF_U32(cur_stack_ptr, 0) = DEREF_U32(arg_ptr, 0);
        DEREF_U32(cur_stack_ptr, 1) = DEREF_U32(arg_ptr, 1);
        DEREF_U32(cur_stack_ptr, 2) = DEREF_U32(arg_ptr, 2);
        DEREF_U32(cur_stack_ptr, 3) = DEREF_U32(arg_ptr, 3);
        break;
      case FFI_TYPE_STRUCT:
        // Again, struct must be passed by pointer.
        // But ABI is by value, so have to copy struct onto stack.
        // Currently arguments are going onto stack so we can't put it there now. Come back for this.
        STACK_ALLOC(cur_stack_ptr, __SIZEOF_POINTER__, __SIZEOF_POINTER__);
        struct_arg_info.push([cur_stack_ptr, arg_ptr, DEC_PTR(FFI_TYPE__SIZE(arg_type_ptr)), FFI_TYPE__ALIGN(arg_type_ptr)]);
        break;
      case FFI_TYPE_POINTER:
        STACK_ALLOC(cur_stack_ptr, __SIZEOF_POINTER__, __SIZEOF_POINTER__);
        DEREF_PTR(cur_stack_ptr, 0) = DEREF_PTR(arg_ptr, 0);
        break;
      case FFI_TYPE_COMPLEX:
        throw new Error('complex arg marshalling nyi');
      default:
        throw new Error('Unexpected argtype ' + arg_type_id);
      }
    }
    // extra normal argument which is the pointer to the varargs.
    args.push(ENC_PTR(cur_stack_ptr));
    // Now allocate variable struct args on stack too.
    for (var i = 0; i < struct_arg_info.length; i++) {
      var struct_info = struct_arg_info[i];
      var arg_target = struct_info[0];
      var arg_ptr = struct_info[1];
      var size = struct_info[2];
      var align = struct_info[3];
      STACK_ALLOC(cur_stack_ptr, size, align);
      HEAP8.subarray(cur_stack_ptr, cur_stack_ptr+size).set(HEAP8.subarray(arg_ptr, arg_ptr + size));
      DEREF_PTR(arg_target, 0) = ENC_PTR(cur_stack_ptr);
    }
  }
  stackRestore(cur_stack_ptr);
  stackAlloc(0); // stackAlloc enforces alignment invariants on the stack pointer
  LOG_DEBUG("CALL_FUNC_PTR", "fn:", fn, "args:", args);
  // Finally the table index for call_indirect.
  args.push(fn);
  return args;
}

// Returns the epilogue for the ffi_call trampoline with the given signature.
// It restores the stack pointer and stores the callee's result into rvalue.
function ffi_call_epilogue(sig) {
  if (sig[0] === 'v') {
    // Void return, or the callee already wrote the result through the return
    // pointer argument. Nothing to store.
    return function epilogue(cif, orig_stack_ptr, rvalue) {
      stackRestore(DEC_PTR(orig_stack_ptr));
    };
  }
  return function epilogue(cif, orig_stack_ptr, rvalue, result) {
    stackRestore(DEC_PTR(orig_stack_ptr));
    cif = DEC_PTR(cif);
    rvalue = DEC_PTR(rvalue);
    // A NULL rvalue means the caller discards the return value.
    if (rvalue === 0) {
      return;
    }
    var rtype_id = unbox_small_structs(CIF__RTYPE(cif))[1];
    // A small struct return is unboxed to a scalar type id above, but it is
    // still a struct to the caller: its buffer is exactly the struct's size,
    // so it must not get the integral widening to ffi_arg.
    var rtype_widen = FFI_TYPE__TYPEID(DEC_PTR(CIF__RTYPE(cif))) !== FFI_TYPE_STRUCT;
    // Integral returns narrower than a register are widened to ffi_arg, as
    // documented and as every native port does -- but only genuine scalar
    // returns: an unboxed small-struct return keeps its natural width.
    switch (rtype_id) {
    case FFI_TYPE_INT:
    case FFI_TYPE_SINT32:
      if (rtype_widen)
        STORE_ARG_WIDENED_SIGNED(rvalue, result | 0);
      else
        DEREF_U32(rvalue, 0) = result;
      break;
    case FFI_TYPE_UINT32:
      if (rtype_widen)
        STORE_ARG_WIDENED_UNSIGNED(rvalue, result | 0);
      else
        DEREF_U32(rvalue, 0) = result;
      break;
    case FFI_TYPE_FLOAT:
      DEREF_F32(rvalue, 0) = result;
      break;
    case FFI_TYPE_DOUBLE:
      DEREF_F64(rvalue, 0) = result;
      break;
    case FFI_TYPE_UINT8:
      if (rtype_widen)
        STORE_ARG_WIDENED_UNSIGNED(rvalue, result & 0xff);
      else
        DEREF_U8(rvalue, 0) = result;
      break;
    case FFI_TYPE_SINT8:
      if (rtype_widen)
        STORE_ARG_WIDENED_SIGNED(rvalue, (result << 24) >> 24);
      else
        DEREF_U8(rvalue, 0) = result;
      break;
    case FFI_TYPE_UINT16:
      if (rtype_widen)
        STORE_ARG_WIDENED_UNSIGNED(rvalue, result & 0xffff);
      else
        DEREF_U16(rvalue, 0) = result;
      break;
    case FFI_TYPE_SINT16:
      if (rtype_widen)
        STORE_ARG_WIDENED_SIGNED(rvalue, (result << 16) >> 16);
      else
        DEREF_U16(rvalue, 0) = result;
      break;
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
      DEREF_U64(rvalue, 0) = result;
      break;
    case FFI_TYPE_POINTER:
      DEREF_PTR(rvalue, 0) = result;
      break;
    default:
      throw new Error('Unexpected rtype ' + rtype_id);
    }
  };
}

var ffi_call_trampoline_module_cache = new Map();
function ffi_call_trampoline_module(sig) {
  var cache = ffi_call_trampoline_module_cache;
  var module = cache.get(sig);
  if (module) {
    return module;
  }
  var P = PTR_WASM_TYPE;
  var params = ffi_sig_to_wasm_types(sig.slice(1));
  var results = ffi_sig_to_wasm_types(sig[0] === 'v' ? "" : sig[0]);
  module = ffi_build_trampoline_module({
    trampoline: [[P, P, P, P], []],                // (cif, fn, rvalue, avalue)
    prelude:    [[P, P, P, P], [P, P, P, ...params, P]], // -> cif, orig_sp, rvalue, ...args, fn
    callee:     [params, results],                 // fn(...args)
    epilogue:   [[P, P, P, ...results], []],       // (cif, orig_sp, rvalue, result?)
    nparams: 4,
  });
  LOG_DEBUG("CALL_MODULE", "sig:", sig);
  cache.set(sig, module);
  return module;
}

// Shared machinery for the ffi_call and closure trampolines. Both have the
// shape
//
//    (func (export "f") (type trampoline)
//      local.get 0 ... local.get nparams-1
//      call prelude
//      [i32.wrap_i64]
//      call_indirect callee
//      call epilogue)
//

// with prelude and epilogue imported from JS as "e.p" and "e.e" and the
// function table imported as "e.t". prelude returns, in this order, epilogue's
// leading arguments, callee's arguments, and callee's table index, so that
// every value is consumed straight off the operand stack and the function needs
// no locals.
function ffi_build_trampoline_module({ trampoline, prelude, callee, epilogue, nparams }) {
  function uleb128(n) {
    var result = [];
    do {
      var byte = n & 0x7f;
      n >>>= 7;
      if (n) {
        byte |= 0x80;
      }
      result.push(byte);
    } while (n);
    return result;
  }
  // A vector of single-byte items
  function vec(bytes) {
    return [...uleb128(bytes.length), ...bytes];
  }
  function section(id, bytes) {
    return [id, ...vec(bytes)];
  }
  function funcType([params, results]) {
    return [0x60, ...vec(params), ...vec(results)];
  }

  // Is the function table 64-bit indexed?
  // -sMEMORY64=2 compiles with 64-bit pointers but uses a 32-bit memory and
  // table, so we have to i32.wrap_i64 pointers.
  var idx64 = typeof wasmTable.length === 'bigint';
  var idxFlag = idx64 ? 0x04 : 0x00;
  // Pointers are wrapped to i32 to be used as a table index.
  var wrapPtr = (PTR_WASM_TYPE === 0x7e && !idx64) ? [0xa7] : []; // i32.wrap_i64

  var typeSection = section(0x01, [
    ...uleb128(4),
    ...funcType(trampoline), // type 0
    ...funcType(prelude),    // type 1
    ...funcType(callee),     // type 2
    ...funcType(epilogue),   // type 3
  ]);
  var importSection = section(0x02, [
    ...uleb128(3),
    // prelude: (import "e" "p" (func (type 1)))
    0x01, 0x65, 0x01, 0x70, 0x00, 0x01,
    // epilogue: (import "e" "e" (func (type 3)))
    0x01, 0x65, 0x01, 0x65, 0x00, 0x03,
    // (import "e" "t" (table 0 funcref))
    0x01, 0x65, 0x01, 0x74, 0x01, 0x70, 0x00 | idxFlag, 0x00,
  ]);
  // One function, of type 0.
  var functionSection = section(0x03, [0x01, 0x00]);
  // (export "f" (func 2)) -- funcs 0 and 1 are the imports.
  var exportSection = section(0x07, [0x01, 0x01, 0x66, 0x00, 0x02]);

  // Function body
  var body = [0x00]; // no locals
  for (var i = 0; i < nparams; i++) {
    body.push(0x20, ...uleb128(i)); // local.get i
  }
  body.push(0x10, 0x00); // call prelude
  body.push(...wrapPtr); // the table index is on top of the stack
  body.push(0x11, 0x02, 0x00); // call_indirect (type 2) (table 0)
  body.push(0x10, 0x01); // call epilogue
  body.push(0x0b); // end
  var codeSection = section(0x0a, [...uleb128(1), ...vec(body)]);

  var bytes = new Uint8Array([
    0x00, 0x61, 0x73, 0x6d, // magic
    0x01, 0x00, 0x00, 0x00, // version
    ...typeSection,
    ...importSection,
    ...functionSection,
    ...exportSection,
    ...codeSection,
  ]);
  // The module is tiny so compiling synchronously is fine.
  return new WebAssembly.Module(bytes);
}

function ffi_sig_to_wasm_types(sig) {
  var typeCodes = { 'i': 0x7f, 'j': 0x7e, 'f': 0x7d, 'd': 0x7c };
  return Array.from(sig, (c) => typeCodes[c]);
}
)

typedef void (*ffi_call_trampoline)(ffi_cif *, void (*)(void), void *, void **);

void ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue) {
  ffi_call_trampoline tramp = ffi_call_get_trampoline_js(cif);
  tramp(cif, fn, rvalue, avalue);
}

#if __SIZEOF_POINTER__ == 4

CHECK_FIELD_OFFSET(ffi_closure, ftramp, 4*0);
CHECK_FIELD_OFFSET(ffi_closure, cif, 4*1);
CHECK_FIELD_OFFSET(ffi_closure, fun, 4*2);
CHECK_FIELD_OFFSET(ffi_closure, user_data, 4*3);

#define CLOSURE__wrapper(addr) DEREF_U32(addr, 0)
#define CLOSURE__cif(addr) DEREF_U32(addr, 1)
#define CLOSURE__fun(addr) DEREF_U32(addr, 2)
#define CLOSURE__user_data(addr) DEREF_U32(addr, 3)

#elif __SIZEOF_POINTER__ == 8

CHECK_FIELD_OFFSET(ffi_closure, ftramp, 0);
CHECK_FIELD_OFFSET(ffi_closure, cif, 8);
CHECK_FIELD_OFFSET(ffi_closure, fun, 16);
CHECK_FIELD_OFFSET(ffi_closure, user_data, 24);

#define CLOSURE__wrapper(addr) DEREF_U64(addr, 0)
#define CLOSURE__cif(addr) DEREF_U64(addr, 1)
#define CLOSURE__fun(addr) DEREF_U64(addr, 2)
#define CLOSURE__user_data(addr) DEREF_U64(addr, 3)

#else
#error "Unknown pointer size"
#endif

EM_JS_MACROS(void *, ffi_closure_alloc_js, (size_t size, void **code), {
  size = DEC_PTR(size);
  code = DEC_PTR(code);
  var closure = _malloc(size);
  var index = getEmptyTableSlot();
  DEREF_PTR(code, 0) = ENC_PTR(index);
  CLOSURE__wrapper(closure) = ENC_PTR(index);
  return ENC_PTR(closure);
})

void * __attribute__ ((visibility ("default")))
ffi_closure_alloc(size_t size, void **code) {
  return ffi_closure_alloc_js(size, code);
}

EM_JS_MACROS(void, ffi_closure_free_js, (void *closure), {
  closure = DEC_PTR(closure);
  var index = DEC_PTR(CLOSURE__wrapper(closure));
  freeTableIndexes.push(index);
  _free(closure);
})

void __attribute__ ((visibility ("default")))
ffi_closure_free(void *closure) {
  return ffi_closure_free_js(closure);
}



/**
 * A closure has to appear to its caller as an ordinary wasm function with the
 * signature of the cif. In order for it to work with JSPI, the trampoline has
 * to be implemented in WebAssembly -- otherwise attempting to suspend inside of
 * the closure will trap with "trying to suspend JS frames".
 *
 * It's a bit annoying to do things in generated WebAssembly so we put most of
 * the logic in JS prelude/epilogue.
 *
 *    call prelude(... all closure arguments)
 *    call_indirect closure->fun(cif, ret_ptr, args_ptr, user_data)
 *    call epilogue(ret_ptr, orig_stack_ptr)
 *
 * prelude() which copies the closure arguments into the appropriate places on
 * the C stack, builds the argument pointer array, and returns 7 values: The
 * closure->fun pointer and its four arguments, and the two arguments for epilogue.

 * epilogue() is a JavaScript helper that restores the stack pointer and loads
 * the return value from ret_ptr. Doing the load in JavaScript rather than in
 * the trampoline avoids needing to specify the memory size in the imports
 * section.
 *
 * The module bytes are a pure function of the signature string. We cache the
 * compiled WebAssembly.Module per signature and instantiate it once per closure
 * with that closure's prelude.
 */
EM_JS_MACROS(
ffi_status,
ffi_prep_closure_loc_js,
(ffi_closure *closure, ffi_cif *cif, void *fun, void *user_data, void *codeloc),
{
  closure = DEC_PTR(closure);
  cif = DEC_PTR(cif);
  fun = DEC_PTR(fun);
  user_data = DEC_PTR(user_data);
  codeloc = DEC_PTR(codeloc);
  var { sig, call_info } = ffi_cif_to_call_info(cif);

  LOG_DEBUG("CREATE_CLOSURE", "sig:", sig);
  try {
    var instance = new WebAssembly.Instance(
      ffi_closure_trampoline_module(sig),
      {
        e: {
          p: ffi_closure_prelude(closure, call_info),
          e: ffi_closure_epilogue(sig),
          t: wasmTable,
        },
      },
    );
  } catch (e) {
    LOG_DEBUG("CREATE_CLOSURE_FAILED", e);
    return FFI_BAD_TYPEDEF_MACRO;
  }
  var wasm_trampoline = instance.exports["f"];
  setWasmTableEntry(codeloc, wasm_trampoline);
  CLOSURE__cif(closure) = ENC_PTR(cif);
  CLOSURE__fun(closure) = ENC_PTR(fun);
  CLOSURE__user_data(closure) = ENC_PTR(user_data);
  return FFI_OK_MACRO;
}

// Walk the CIF and compute the metadata we need for ffi_closure_prelude
// and ffi_closure_trampoline_module.
function ffi_cif_to_call_info(cif) {
  var abi = CIF__ABI(cif);
  var nargs = CIF__NARGS(cif);
  var nfixedargs = CIF__NFIXEDARGS(cif);
  var arg_types_ptr = DEC_PTR(CIF__ARGTYPES(cif));
  var rtype_unboxed = unbox_small_structs(CIF__RTYPE(cif));
  var rtype_ptr = rtype_unboxed[0];
  var rtype_id = rtype_unboxed[1];

  // First construct the signature of the javascript trampoline we are going to create.
  // Important: this is the signature for calling us, the onward call always has sig viiii.
  var sig;
  var ret_by_arg = false;
  switch (rtype_id) {
  case FFI_TYPE_VOID:
    sig = 'v';
    break;
  case FFI_TYPE_STRUCT:
  case FFI_TYPE_LONGDOUBLE:
    // Return via a first pointer argument.
    sig = 'v' + PTR_SIG;
    ret_by_arg = true;
    break;
  case FFI_TYPE_INT:
  case FFI_TYPE_UINT8:
  case FFI_TYPE_SINT8:
  case FFI_TYPE_UINT16:
  case FFI_TYPE_SINT16:
  case FFI_TYPE_UINT32:
  case FFI_TYPE_SINT32:
    sig = 'i';
    break;
  case FFI_TYPE_FLOAT:
    sig = 'f';
    break;
  case FFI_TYPE_DOUBLE:
    sig = 'd';
    break;
  case FFI_TYPE_UINT64:
  case FFI_TYPE_SINT64:
    sig = 'j';
    break;
  case FFI_TYPE_POINTER:
    sig = PTR_SIG;
    break;
  case FFI_TYPE_COMPLEX:
    throw new Error('complex ret marshalling nyi');
  default:
    throw new Error('Unexpected rtype ' + rtype_id);
  }
  var unboxed_arg_type_id_list = [];
  var unboxed_arg_type_info_list = [];
  for (var i = 0; i < nargs; i++) {
    var arg_unboxed = unbox_small_structs(DEREF_PTR(arg_types_ptr, i));
    var arg_type_ptr = arg_unboxed[0];
    var arg_type_id = arg_unboxed[1];
    unboxed_arg_type_id_list.push(arg_type_id);
    unboxed_arg_type_info_list.push([DEC_PTR(FFI_TYPE__SIZE(arg_type_ptr)), FFI_TYPE__ALIGN(arg_type_ptr)]);
  }
  for (var i = 0; i < nfixedargs; i++) {
    switch (unboxed_arg_type_id_list[i]) {
    case FFI_TYPE_INT:
    case FFI_TYPE_UINT8:
    case FFI_TYPE_SINT8:
    case FFI_TYPE_UINT16:
    case FFI_TYPE_SINT16:
    case FFI_TYPE_UINT32:
    case FFI_TYPE_SINT32:
      sig += 'i';
      break;
    case FFI_TYPE_FLOAT:
      sig += 'f';
      break;
    case FFI_TYPE_DOUBLE:
      sig += 'd';
      break;
    case FFI_TYPE_LONGDOUBLE:
      sig += 'jj';
      break;
    case FFI_TYPE_UINT64:
    case FFI_TYPE_SINT64:
      sig += 'j';
      break;
    case FFI_TYPE_STRUCT:
    case FFI_TYPE_POINTER:
      sig += PTR_SIG;
      break;
    case FFI_TYPE_COMPLEX:
      throw new Error('complex marshalling nyi');
    default:
      throw new Error('Unexpected argtype ' + arg_type_id);
    }
  }
  if (CIF__FLAGS(cif) & VARARGS_FLAG) {
    // A variadic function takes an extra pointer to the varargs stack, even
    // when no variadic arguments are actually passed (nfixedargs == nargs).
    sig += PTR_SIG;
  }
  return {
    sig,
    call_info: {
      nfixedargs,
      nargs,
      ret_by_arg,
      unboxed_arg_type_info_list,
      unboxed_arg_type_id_list,
    },
  };
}

// The prelude() function is called by the wasm trampoline with the closure's
// arguments. It marshals them onto the C stack and returns what the trampoline
// needs to make the onward call.
function ffi_closure_prelude(
  closure,
  {
    nfixedargs,
    nargs,
    ret_by_arg,
    unboxed_arg_type_info_list,
    unboxed_arg_type_id_list,
  },
) {
  return function prelude() {
    var args = arguments;
    var orig_stack_ptr = stackSave();
    var cur_ptr = orig_stack_ptr;
    var ret_ptr;
    var jsarg_idx = 0;
    // Should we return by argument or not? The onwards call returns by argument
    // no matter what. (Warning: ret_by_arg means the opposite in ffi_call)
    if (ret_by_arg) {
      ret_ptr = DEC_PTR(args[jsarg_idx++]);
    } else {
      // We might return 4 bytes or 8 bytes, allocate 8 just in case.
      STACK_ALLOC(cur_ptr, 8, 8);
      ret_ptr = cur_ptr;
    }
    cur_ptr -= __SIZEOF_POINTER__ * nargs;
    var args_ptr = cur_ptr;
    var carg_idx = 0;
    // Here we either have the actual argument, or a pair of BigInts for long
    // double, or a pointer to struct. We have to store into args_ptr[i] a
    // pointer to the ith argument. If the argument is a struct, just store the
    // pointer. Otherwise allocate stack space and copy the js argument onto the
    // stack.
    for (; carg_idx < nfixedargs; carg_idx++) {
      // jsarg_idx might start out as 0 or 1 depending on ret_by_arg
      // it advances an extra time for long double
      var cur_arg = args[jsarg_idx++];
      var arg_type_info = unboxed_arg_type_info_list[carg_idx];
      var arg_size = arg_type_info[0];
      var arg_align = arg_type_info[1];
      var arg_type_id = unboxed_arg_type_id_list[carg_idx];
      switch (arg_type_id) {
      case FFI_TYPE_UINT8:
      case FFI_TYPE_SINT8:
        // Bad things happen if we don't align to 4 here
        STACK_ALLOC(cur_ptr, 1, 4);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_U8(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_UINT16:
      case FFI_TYPE_SINT16:
        // Bad things happen if we don't align to 4 here
        STACK_ALLOC(cur_ptr, 2, 4);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_U16(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_INT:
      case FFI_TYPE_UINT32:
      case FFI_TYPE_SINT32:
        STACK_ALLOC(cur_ptr, 4, 4);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_U32(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_STRUCT:
        // cur_arg is already a pointer to struct
        // copy it onto stack to pass by value
        STACK_ALLOC(cur_ptr, arg_size, arg_align);
        HEAP8.subarray(cur_ptr, cur_ptr + arg_size).set(HEAP8.subarray(DEC_PTR(cur_arg), DEC_PTR(cur_arg) + arg_size));
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        break;
      case FFI_TYPE_FLOAT:
        STACK_ALLOC(cur_ptr, 4, 4);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_F32(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_DOUBLE:
        STACK_ALLOC(cur_ptr, 8, 8);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_F64(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_UINT64:
      case FFI_TYPE_SINT64:
        STACK_ALLOC(cur_ptr, 8, 8);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_U64(cur_ptr, 0) = cur_arg;
        break;
      case FFI_TYPE_LONGDOUBLE:
        STACK_ALLOC(cur_ptr, 16, 8);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_U64(cur_ptr, 0) = cur_arg;
        cur_arg = args[jsarg_idx++];
        DEREF_U64(cur_ptr, 1) = cur_arg;
        break;
      case FFI_TYPE_POINTER:
        STACK_ALLOC(cur_ptr, __SIZEOF_POINTER__, __SIZEOF_POINTER__);
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
        DEREF_PTR(cur_ptr, 0) = cur_arg;
        break;
      }
    }
    // If its a varargs call, last js argument is a pointer to the varargs.
    var varargs = DEC_PTR(args[args.length - 1]);
    // We have no way of knowing how many varargs were actually provided, this
    // fills the rest of the stack space allocated with nonsense. The onward
    // call will know to ignore the nonsense.

    // We either have a pointer to the argument if the argument is not a struct
    // or a pointer to pointer to struct. We need to store a pointer to the
    // argument into args_ptr[i]
    for (; carg_idx < nargs; carg_idx++) {
      var arg_type_id = unboxed_arg_type_id_list[carg_idx];
      var arg_type_info = unboxed_arg_type_info_list[carg_idx];
      var arg_size = arg_type_info[0];
      var arg_align = arg_type_info[1];
      if (arg_type_id === FFI_TYPE_STRUCT) {
        // In this case varargs is a pointer to pointer to struct so we need to
        // deref once
        var struct_ptr = DEREF_PTR_NUMBER(varargs, 0);
        STACK_ALLOC(cur_ptr, arg_size, arg_align);
        HEAP8.subarray(cur_ptr, cur_ptr + arg_size).set(HEAP8.subarray(struct_ptr, struct_ptr + arg_size));
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(cur_ptr);
      } else {
        DEREF_PTR(args_ptr, carg_idx) = ENC_PTR(varargs);
      }
      varargs += __SIZEOF_POINTER__;
    }
    stackRestore(cur_ptr);
    stackAlloc(0); // stackAlloc enforces alignment invariants on the stack pointer
    LOG_DEBUG("CALL_CLOSURE", "closure:", closure, "fptr", CLOSURE__fun(closure), "cif", CLOSURE__cif(closure));
    // The trampoline now does
    //    fun(cif, ret_ptr, args_ptr, user_data);
    //    return epilogue(ret_ptr, orig_stack_ptr);
    //
    // We return these arguments in reverse order because stack is LIFO.
    return [
      ENC_PTR(ret_ptr),          // epilogue args (in correct order)
      ENC_PTR(orig_stack_ptr),

      CLOSURE__cif(closure),     // call_indirect args (in correct order)
      ENC_PTR(ret_ptr),
      ENC_PTR(args_ptr),
      CLOSURE__user_data(closure),

      CLOSURE__fun(closure),     // ...and the table index for call_indirect
    ];
  }
}

// Returns function which restores the stack pointer and loads the closure's
// return value from ret_ptr.
function ffi_closure_epilogue(sig) {
  var load;
  switch (sig[0]) {
  case 'i': load = (ptr) => DEREF_S32(ptr, 0); break;
  case 'j': load = (ptr) => DEREF_U64(ptr, 0); break;
  case 'f': load = (ptr) => DEREF_F32(ptr, 0); break;
  case 'd': load = (ptr) => DEREF_F64(ptr, 0); break;
  default:  load = (ptr) => undefined; break;
  }
  return function epilogue(ret_ptr, orig_stack_ptr) {
    stackRestore(DEC_PTR(orig_stack_ptr));
    return load(DEC_PTR(ret_ptr));
  };
}

var ffi_closure_trampoline_module_cache = new Map();
function ffi_closure_trampoline_module(sig) {
  var cache = ffi_closure_trampoline_module_cache;
  var module = cache.get(sig);
  if (module) {
    return module;
  }
  var P = PTR_WASM_TYPE;
  var params = ffi_sig_to_wasm_types(sig.slice(1));
  var results = ffi_sig_to_wasm_types(sig[0] === 'v' ? "" : sig[0]);
  module = ffi_build_trampoline_module({
    trampoline: [params, results],                 // the closure's own signature
    prelude:    [params, [P, P, P, P, P, P, P]],   // -> ret_ptr, orig_sp, cif, ret_ptr, args_ptr, user_data, fun
    callee:     [[P, P, P, P], []],                // closure->fun(cif, ret, args, user_data)
    epilogue:   [[P, P], results],                 // (ret_ptr, orig_sp) -> result
    nparams: params.length,
  });
  LOG_DEBUG("CLOSURE_MODULE", "sig:", sig);
  cache.set(sig, module);
  return module;
}
)

// EM_JS does not correctly handle function pointer arguments, so we need a
// helper
ffi_status ffi_prep_closure_loc(ffi_closure *closure, ffi_cif *cif,
                                void (*fun)(ffi_cif *, void *, void **, void *),
                                void *user_data, void *codeloc) {
  if (cif->abi != FFI_EMSCRIPTEN_ABI)
    return FFI_BAD_ABI;
  return ffi_prep_closure_loc_js(closure, cif, (void *)fun, user_data,
                                     codeloc);
}
