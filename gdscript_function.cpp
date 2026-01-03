/**************************************************************************/
/*  gdscript_function.cpp                                                 */
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

#include "gdscript_function.h"

#include "gdscript.h"

Variant GDScriptFunction::get_constant(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, constants.size(), "<errconst>");
	return constants[p_idx];
}

bool GDScriptFunction::is_math_operator(Variant::Operator p_operator) {
	switch (p_operator) {
		case Variant::OP_ADD:
		case Variant::OP_SUBTRACT:
		case Variant::OP_MULTIPLY:
		case Variant::OP_DIVIDE:
		case Variant::OP_NEGATE:
		case Variant::OP_MODULE:
		case Variant::OP_POWER:
			return true;
		default:
			return false;
	}
}

void GDScriptFunction::prepare_native_jit() {
	native_operator_segments.clear();
	native_segment_lookup.clear();
	native_segment_index_by_ip.clear();
	native_segments_ready = false;

	if (!_code_ptr) {
		native_segments_ready = true;
		return;
	}

	auto opcode_size_at = [&](int p_ip) -> int {
		if (p_ip < 0 || p_ip >= _code_size) {
			return 1;
		}
		int op = _code_ptr[p_ip];
		switch (op) {
			case OPCODE_OPERATOR_VALIDATED:
				return 5;
			case OPCODE_SET_NAMED_VALIDATED:
			case OPCODE_GET_NAMED_VALIDATED:
				return 4;
			case OPCODE_SET_KEYED_VALIDATED:
			case OPCODE_GET_KEYED_VALIDATED:
			case OPCODE_SET_INDEXED_VALIDATED:
			case OPCODE_GET_INDEXED_VALIDATED:
				return 5;
			case OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
			case OPCODE_CALL_UTILITY_VALIDATED:
			case OPCODE_CALL_GDSCRIPT_UTILITY:
				return 4 + _code_ptr[p_ip + 1];
			case OPCODE_TYPE_ADJUST_BOOL:
			case OPCODE_TYPE_ADJUST_INT:
			case OPCODE_TYPE_ADJUST_FLOAT:
			case OPCODE_TYPE_ADJUST_STRING:
			case OPCODE_TYPE_ADJUST_VECTOR2:
			case OPCODE_TYPE_ADJUST_VECTOR2I:
			case OPCODE_TYPE_ADJUST_RECT2:
			case OPCODE_TYPE_ADJUST_RECT2I:
			case OPCODE_TYPE_ADJUST_VECTOR3:
			case OPCODE_TYPE_ADJUST_VECTOR3I:
			case OPCODE_TYPE_ADJUST_TRANSFORM2D:
			case OPCODE_TYPE_ADJUST_VECTOR4:
			case OPCODE_TYPE_ADJUST_VECTOR4I:
			case OPCODE_TYPE_ADJUST_PLANE:
			case OPCODE_TYPE_ADJUST_QUATERNION:
			case OPCODE_TYPE_ADJUST_AABB:
			case OPCODE_TYPE_ADJUST_BASIS:
			case OPCODE_TYPE_ADJUST_TRANSFORM3D:
			case OPCODE_TYPE_ADJUST_PROJECTION:
			case OPCODE_TYPE_ADJUST_COLOR:
			case OPCODE_TYPE_ADJUST_STRING_NAME:
			case OPCODE_TYPE_ADJUST_NODE_PATH:
			case OPCODE_TYPE_ADJUST_RID:
			case OPCODE_TYPE_ADJUST_OBJECT:
			case OPCODE_TYPE_ADJUST_CALLABLE:
			case OPCODE_TYPE_ADJUST_SIGNAL:
			case OPCODE_TYPE_ADJUST_DICTIONARY:
			case OPCODE_TYPE_ADJUST_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY:
				return 2;
			default:
				return 1;
		}
	};

	auto is_supported = [&](int op) -> bool {
		switch (op) {
			case OPCODE_OPERATOR_VALIDATED:
				return true;
			case OPCODE_SET_NAMED_VALIDATED:
			case OPCODE_GET_NAMED_VALIDATED:
				return true;
			case OPCODE_SET_KEYED_VALIDATED:
			case OPCODE_GET_KEYED_VALIDATED:
			case OPCODE_SET_INDEXED_VALIDATED:
			case OPCODE_GET_INDEXED_VALIDATED:
				return true;
			case OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
			case OPCODE_CALL_UTILITY_VALIDATED:
			case OPCODE_CALL_GDSCRIPT_UTILITY:
				return true;
			case OPCODE_TYPE_ADJUST_BOOL:
			case OPCODE_TYPE_ADJUST_INT:
			case OPCODE_TYPE_ADJUST_FLOAT:
			case OPCODE_TYPE_ADJUST_STRING:
			case OPCODE_TYPE_ADJUST_VECTOR2:
			case OPCODE_TYPE_ADJUST_VECTOR2I:
			case OPCODE_TYPE_ADJUST_RECT2:
			case OPCODE_TYPE_ADJUST_RECT2I:
			case OPCODE_TYPE_ADJUST_VECTOR3:
			case OPCODE_TYPE_ADJUST_VECTOR3I:
			case OPCODE_TYPE_ADJUST_TRANSFORM2D:
			case OPCODE_TYPE_ADJUST_VECTOR4:
			case OPCODE_TYPE_ADJUST_VECTOR4I:
			case OPCODE_TYPE_ADJUST_PLANE:
			case OPCODE_TYPE_ADJUST_QUATERNION:
			case OPCODE_TYPE_ADJUST_AABB:
			case OPCODE_TYPE_ADJUST_BASIS:
			case OPCODE_TYPE_ADJUST_TRANSFORM3D:
			case OPCODE_TYPE_ADJUST_PROJECTION:
			case OPCODE_TYPE_ADJUST_COLOR:
			case OPCODE_TYPE_ADJUST_STRING_NAME:
			case OPCODE_TYPE_ADJUST_NODE_PATH:
			case OPCODE_TYPE_ADJUST_RID:
			case OPCODE_TYPE_ADJUST_OBJECT:
			case OPCODE_TYPE_ADJUST_CALLABLE:
			case OPCODE_TYPE_ADJUST_SIGNAL:
			case OPCODE_TYPE_ADJUST_DICTIONARY:
			case OPCODE_TYPE_ADJUST_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY:
			case OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY:
				return true;
			default:
				return false;
		}
	};

	HashMap<int, bool> unary_map;
	for (const NativeOperatorHint &hint : native_operator_hints) {
		unary_map.insert(hint.ip, hint.unary);
	}

	auto build_operator_step = [&](NativeStep &r_step, int ip) -> bool {
		int operator_func_index = _code_ptr[ip + 4];
		if (operator_func_index < 0 || operator_func_index >= _operator_funcs_count) {
			return false;
		}
		int a_address = _code_ptr[ip + 1];
		int b_address = _code_ptr[ip + 2];
		int dst_address = _code_ptr[ip + 3];
		r_step.kind = NativeStep::STEP_OPERATOR;
		r_step.op.a_type = (uint8_t)((a_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.op.b_type = (uint8_t)((b_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.op.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.op.a_index = (uint32_t)(a_address & ADDR_MASK);
		r_step.op.b_index = (uint32_t)(b_address & ADDR_MASK);
		r_step.op.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.op.evaluator = _operator_funcs_ptr[operator_func_index];
		const HashMap<int, bool>::ConstIterator it = unary_map.find(ip);
		r_step.op.unary = it ? it->value : false;
		return true;
	};

	auto build_keyed_set_step = [&](NativeStep &r_step, int ip) -> bool {
		int setter_idx = _code_ptr[ip + 4];
		if (setter_idx < 0 || setter_idx >= _keyed_setters_count) {
			return false;
		}
		int dst_address = _code_ptr[ip + 1];
		int key_address = _code_ptr[ip + 2];
		int value_address = _code_ptr[ip + 3];
		r_step.kind = NativeStep::STEP_KEYED_SET;
		r_step.keyed_set.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_set.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.keyed_set.key_type = (uint8_t)((key_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_set.key_index = (uint32_t)(key_address & ADDR_MASK);
		r_step.keyed_set.value_type = (uint8_t)((value_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_set.value_index = (uint32_t)(value_address & ADDR_MASK);
		r_step.keyed_set.setter = _keyed_setters_ptr[setter_idx];
		return true;
	};

	auto build_keyed_get_step = [&](NativeStep &r_step, int ip) -> bool {
		int getter_idx = _code_ptr[ip + 4];
		if (getter_idx < 0 || getter_idx >= _keyed_getters_count) {
			return false;
		}
		int src_address = _code_ptr[ip + 1];
		int key_address = _code_ptr[ip + 2];
		int dst_address = _code_ptr[ip + 3];
		r_step.kind = NativeStep::STEP_KEYED_GET;
		r_step.keyed_get.src_type = (uint8_t)((src_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_get.src_index = (uint32_t)(src_address & ADDR_MASK);
		r_step.keyed_get.key_type = (uint8_t)((key_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_get.key_index = (uint32_t)(key_address & ADDR_MASK);
		r_step.keyed_get.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.keyed_get.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.keyed_get.getter = _keyed_getters_ptr[getter_idx];
		return true;
	};

	auto build_indexed_set_step = [&](NativeStep &r_step, int ip) -> bool {
		int setter_idx = _code_ptr[ip + 4];
		if (setter_idx < 0 || setter_idx >= _indexed_setters_count) {
			return false;
		}
		int dst_address = _code_ptr[ip + 1];
		int index_address = _code_ptr[ip + 2];
		int value_address = _code_ptr[ip + 3];
		r_step.kind = NativeStep::STEP_INDEXED_SET;
		r_step.indexed_set.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_set.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.indexed_set.index_type = (uint8_t)((index_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_set.index_index = (uint32_t)(index_address & ADDR_MASK);
		r_step.indexed_set.value_type = (uint8_t)((value_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_set.value_index = (uint32_t)(value_address & ADDR_MASK);
		r_step.indexed_set.setter = _indexed_setters_ptr[setter_idx];
		return true;
	};

	auto build_indexed_get_step = [&](NativeStep &r_step, int ip) -> bool {
		int getter_idx = _code_ptr[ip + 4];
		if (getter_idx < 0 || getter_idx >= _indexed_getters_count) {
			return false;
		}
		int src_address = _code_ptr[ip + 1];
		int index_address = _code_ptr[ip + 2];
		int dst_address = _code_ptr[ip + 3];
		r_step.kind = NativeStep::STEP_INDEXED_GET;
		r_step.indexed_get.src_type = (uint8_t)((src_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_get.src_index = (uint32_t)(src_address & ADDR_MASK);
		r_step.indexed_get.index_type = (uint8_t)((index_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_get.index_index = (uint32_t)(index_address & ADDR_MASK);
		r_step.indexed_get.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.indexed_get.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.indexed_get.getter = _indexed_getters_ptr[getter_idx];
		return true;
	};

	auto build_named_set_step = [&](NativeStep &r_step, int ip) -> bool {
		int setter_idx = _code_ptr[ip + 3];
		if (setter_idx < 0 || setter_idx >= _setters_count) {
			return false;
		}
		int dst_address = _code_ptr[ip + 1];
		int value_address = _code_ptr[ip + 2];
		r_step.kind = NativeStep::STEP_NAMED_SET;
		r_step.named_set.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.named_set.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.named_set.value_type = (uint8_t)((value_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.named_set.value_index = (uint32_t)(value_address & ADDR_MASK);
		r_step.named_set.setter = _setters_ptr[setter_idx];
		return true;
	};

	auto build_named_get_step = [&](NativeStep &r_step, int ip) -> bool {
		int getter_idx = _code_ptr[ip + 3];
		if (getter_idx < 0 || getter_idx >= _getters_count) {
			return false;
		}
		int src_address = _code_ptr[ip + 1];
		int dst_address = _code_ptr[ip + 2];
		r_step.kind = NativeStep::STEP_NAMED_GET;
		r_step.named_get.src_type = (uint8_t)((src_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.named_get.src_index = (uint32_t)(src_address & ADDR_MASK);
		r_step.named_get.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.named_get.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.named_get.getter = _getters_ptr[getter_idx];
		return true;
	};

	auto build_call_step = [&](NativeStep &r_step, int ip) -> bool {
		int instr_argc = _code_ptr[ip + 1];
		if (instr_argc < 0) {
			return false;
		}
		int argc_value = _code_ptr[ip + 2 + instr_argc];
		if (argc_value < 0) {
			return false;
		}

		r_step.kind = NativeStep::STEP_CALL_VALIDATED;
		r_step.call.argc = argc_value;
		r_step.call.arg_types.resize(argc_value);
		r_step.call.arg_indices.resize(argc_value);

		switch (_code_ptr[ip]) {
			case OPCODE_CALL_BUILTIN_TYPE_VALIDATED: {
				r_step.call.call_kind = NativeCallStep::CALL_BUILTIN;
				int addr_base = ip + 2;
				int base_pos = addr_base + argc_value;
				int dst_pos = base_pos + 1;
				int func_pos = dst_pos + 2;
				int func_index = _code_ptr[func_pos];
				if (func_index < 0 || func_index >= _builtin_methods_count) {
					return false;
				}
				r_step.call.base_type = (uint8_t)((_code_ptr[base_pos] & ADDR_TYPE_MASK) >> ADDR_BITS);
				r_step.call.base_index = (uint32_t)(_code_ptr[base_pos] & ADDR_MASK);
				r_step.call.dst_type = (uint8_t)((_code_ptr[dst_pos] & ADDR_TYPE_MASK) >> ADDR_BITS);
				r_step.call.dst_index = (uint32_t)(_code_ptr[dst_pos] & ADDR_MASK);
				r_step.call.builtin = _builtin_methods_ptr[func_index];
				for (int i = 0; i < argc_value; i++) {
					int addr = _code_ptr[addr_base + i];
					r_step.call.arg_types.write[i] = (uint8_t)((addr & ADDR_TYPE_MASK) >> ADDR_BITS);
					r_step.call.arg_indices.write[i] = (uint32_t)(addr & ADDR_MASK);
				}
			} break;
			case OPCODE_CALL_UTILITY_VALIDATED: {
				r_step.call.call_kind = NativeCallStep::CALL_UTILITY;
				int addr_base = ip + 2;
				int dst_pos = addr_base + argc_value;
				int func_pos = dst_pos + 2;
				Variant::ValidatedUtilityFunction func = reinterpret_cast<Variant::ValidatedUtilityFunction>(_code_ptr[func_pos]);
				r_step.call.dst_type = (uint8_t)((_code_ptr[dst_pos] & ADDR_TYPE_MASK) >> ADDR_BITS);
				r_step.call.dst_index = (uint32_t)(_code_ptr[dst_pos] & ADDR_MASK);
				r_step.call.utility = func;
				for (int i = 0; i < argc_value; i++) {
					int addr = _code_ptr[addr_base + i];
					r_step.call.arg_types.write[i] = (uint8_t)((addr & ADDR_TYPE_MASK) >> ADDR_BITS);
					r_step.call.arg_indices.write[i] = (uint32_t)(addr & ADDR_MASK);
				}
			} break;
			case OPCODE_CALL_GDSCRIPT_UTILITY: {
				r_step.call.call_kind = NativeCallStep::CALL_GDS_UTILITY;
				int addr_base = ip + 2;
				int dst_pos = addr_base + argc_value;
				int func_pos = dst_pos + 2;
				GDScriptUtilityFunctions::FunctionPtr func = reinterpret_cast<GDScriptUtilityFunctions::FunctionPtr>(_code_ptr[func_pos]);
				r_step.call.dst_type = (uint8_t)((_code_ptr[dst_pos] & ADDR_TYPE_MASK) >> ADDR_BITS);
				r_step.call.dst_index = (uint32_t)(_code_ptr[dst_pos] & ADDR_MASK);
				r_step.call.gds_utility = func;
				for (int i = 0; i < argc_value; i++) {
					int addr = _code_ptr[addr_base + i];
					r_step.call.arg_types.write[i] = (uint8_t)((addr & ADDR_TYPE_MASK) >> ADDR_BITS);
					r_step.call.arg_indices.write[i] = (uint32_t)(addr & ADDR_MASK);
				}
			} break;
			default:
				return false;
		}

		return true;
	};

	auto build_type_adjust_step = [&](NativeStep &r_step, int ip, Variant::Type type) {
		int dst_address = _code_ptr[ip + 1];
		r_step.kind = NativeStep::STEP_TYPE_ADJUST;
		r_step.adjust.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		r_step.adjust.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		r_step.adjust.target_type = type;
	};

	Vector<NativeOperatorSegment> segments;
	int ip = 0;
	while (ip < _code_size) {
		int op = _code_ptr[ip];
		if (!is_supported(op)) {
			ip += opcode_size_at(ip);
			continue;
		}

		NativeOperatorSegment segment;
		segment.start_ip = ip;
		int cursor = ip;
		while (cursor < _code_size) {
			int current_op = _code_ptr[cursor];
			if (!is_supported(current_op)) {
				break;
			}
			NativeStep step;
			switch (current_op) {
				case OPCODE_OPERATOR_VALIDATED: {
					if (!build_operator_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_SET_NAMED_VALIDATED: {
					if (!build_named_set_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_GET_NAMED_VALIDATED: {
					if (!build_named_get_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_SET_KEYED_VALIDATED: {
					if (!build_keyed_set_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_GET_KEYED_VALIDATED: {
					if (!build_keyed_get_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_SET_INDEXED_VALIDATED: {
					if (!build_indexed_set_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_GET_INDEXED_VALIDATED: {
					if (!build_indexed_get_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				case OPCODE_CALL_BUILTIN_TYPE_VALIDATED:
				case OPCODE_CALL_UTILITY_VALIDATED:
				case OPCODE_CALL_GDSCRIPT_UTILITY: {
					if (!build_call_step(step, cursor)) {
						cursor = _code_size;
						continue;
					}
				} break;
				default: {
					Variant::Type type = Variant::NIL;
					switch (current_op) {
						case OPCODE_TYPE_ADJUST_BOOL: type = Variant::BOOL; break;
						case OPCODE_TYPE_ADJUST_INT: type = Variant::INT; break;
						case OPCODE_TYPE_ADJUST_FLOAT: type = Variant::FLOAT; break;
						case OPCODE_TYPE_ADJUST_STRING: type = Variant::STRING; break;
						case OPCODE_TYPE_ADJUST_VECTOR2: type = Variant::VECTOR2; break;
						case OPCODE_TYPE_ADJUST_VECTOR2I: type = Variant::VECTOR2I; break;
						case OPCODE_TYPE_ADJUST_RECT2: type = Variant::RECT2; break;
						case OPCODE_TYPE_ADJUST_RECT2I: type = Variant::RECT2I; break;
						case OPCODE_TYPE_ADJUST_VECTOR3: type = Variant::VECTOR3; break;
						case OPCODE_TYPE_ADJUST_VECTOR3I: type = Variant::VECTOR3I; break;
						case OPCODE_TYPE_ADJUST_TRANSFORM2D: type = Variant::TRANSFORM2D; break;
						case OPCODE_TYPE_ADJUST_VECTOR4: type = Variant::VECTOR4; break;
						case OPCODE_TYPE_ADJUST_VECTOR4I: type = Variant::VECTOR4I; break;
						case OPCODE_TYPE_ADJUST_PLANE: type = Variant::PLANE; break;
						case OPCODE_TYPE_ADJUST_QUATERNION: type = Variant::QUATERNION; break;
						case OPCODE_TYPE_ADJUST_AABB: type = Variant::AABB; break;
						case OPCODE_TYPE_ADJUST_BASIS: type = Variant::BASIS; break;
						case OPCODE_TYPE_ADJUST_TRANSFORM3D: type = Variant::TRANSFORM3D; break;
						case OPCODE_TYPE_ADJUST_PROJECTION: type = Variant::PROJECTION; break;
						case OPCODE_TYPE_ADJUST_COLOR: type = Variant::COLOR; break;
						case OPCODE_TYPE_ADJUST_STRING_NAME: type = Variant::STRING_NAME; break;
						case OPCODE_TYPE_ADJUST_NODE_PATH: type = Variant::NODE_PATH; break;
						case OPCODE_TYPE_ADJUST_RID: type = Variant::RID; break;
						case OPCODE_TYPE_ADJUST_OBJECT: type = Variant::OBJECT; break;
						case OPCODE_TYPE_ADJUST_CALLABLE: type = Variant::CALLABLE; break;
						case OPCODE_TYPE_ADJUST_SIGNAL: type = Variant::SIGNAL; break;
						case OPCODE_TYPE_ADJUST_DICTIONARY: type = Variant::DICTIONARY; break;
						case OPCODE_TYPE_ADJUST_ARRAY: type = Variant::ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_BYTE_ARRAY: type = Variant::PACKED_BYTE_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_INT32_ARRAY: type = Variant::PACKED_INT32_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_INT64_ARRAY: type = Variant::PACKED_INT64_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_FLOAT32_ARRAY: type = Variant::PACKED_FLOAT32_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_FLOAT64_ARRAY: type = Variant::PACKED_FLOAT64_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_STRING_ARRAY: type = Variant::PACKED_STRING_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_VECTOR2_ARRAY: type = Variant::PACKED_VECTOR2_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_VECTOR3_ARRAY: type = Variant::PACKED_VECTOR3_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_COLOR_ARRAY: type = Variant::PACKED_COLOR_ARRAY; break;
						case OPCODE_TYPE_ADJUST_PACKED_VECTOR4_ARRAY: type = Variant::PACKED_VECTOR4_ARRAY; break;
						default:
							break;
					}
					if (type == Variant::NIL) {
						cursor = _code_size;
						continue;
					}
					build_type_adjust_step(step, cursor, type);
				} break;
			}
			segment.steps.push_back(step);
			cursor += opcode_size_at(cursor);
		}

		segment.end_ip = cursor;
		segments.push_back(segment);
		ip = cursor;
	}

	native_operator_segments = segments;

	const int k_min_native_steps = 10;
	if (!native_operator_segments.is_empty()) {
		Vector<NativeOperatorSegment> filtered;
		filtered.reserve(native_operator_segments.size());
		for (const NativeOperatorSegment &seg : native_operator_segments) {
			if (seg.steps.size() < k_min_native_steps) {
				continue;
			}
			filtered.push_back(seg);
		}
		native_operator_segments = filtered;
	}

	if (_code_size > 0) {
		native_segment_index_by_ip.resize(_code_size);
		native_segment_index_by_ip.fill(-1);
		for (int i = 0; i < native_operator_segments.size(); i++) {
			const NativeOperatorSegment &seg = native_operator_segments[i];
			if (seg.start_ip >= 0 && seg.start_ip < native_segment_index_by_ip.size()) {
				native_segment_index_by_ip.write[seg.start_ip] = i;
			}
		}
	}

	native_segments_ready = true;
}

StringName GDScriptFunction::get_global_name(int p_idx) const {
	ERR_FAIL_INDEX_V(p_idx, global_names.size(), "<errgname>");
	return global_names[p_idx];
}

struct _GDFKC {
	int order = 0;
	List<int> pos;
};

struct _GDFKCS {
	int order = 0;
	StringName id;
	int pos = 0;

	bool operator<(const _GDFKCS &p_r) const {
		return order < p_r.order;
	}
};

void GDScriptFunction::debug_get_stack_member_state(int p_line, List<Pair<StringName, int>> *r_stackvars) const {
	int oc = 0;
	HashMap<StringName, _GDFKC> sdmap;
	for (const StackDebug &sd : stack_debug) {
		if (sd.line >= p_line) {
			break;
		}

		if (sd.added) {
			if (!sdmap.has(sd.identifier)) {
				_GDFKC d;
				d.order = oc++;
				d.pos.push_back(sd.pos);
				sdmap[sd.identifier] = d;

			} else {
				sdmap[sd.identifier].pos.push_back(sd.pos);
			}
		} else {
			ERR_CONTINUE(!sdmap.has(sd.identifier));

			sdmap[sd.identifier].pos.pop_back();
			if (sdmap[sd.identifier].pos.is_empty()) {
				sdmap.erase(sd.identifier);
			}
		}
	}

	List<_GDFKCS> stackpositions;
	for (const KeyValue<StringName, _GDFKC> &E : sdmap) {
		_GDFKCS spp;
		spp.id = E.key;
		spp.order = E.value.order;
		spp.pos = E.value.pos.back()->get();
		stackpositions.push_back(spp);
	}

	stackpositions.sort();

	for (_GDFKCS &E : stackpositions) {
		Pair<StringName, int> p;
		p.first = E.id;
		p.second = E.pos;
		r_stackvars->push_back(p);
	}
}

GDScriptFunction::GDScriptFunction() {
	name = "<anonymous>";
#ifdef DEBUG_ENABLED
	{
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
		GDScriptLanguage::get_singleton()->function_list.add(&function_list);
	}
#endif
}

GDScriptFunction::~GDScriptFunction() {
	get_script()->member_functions.erase(name);

	for (int i = 0; i < lambdas.size(); i++) {
		memdelete(lambdas[i]);
	}

	for (int i = 0; i < argument_types.size(); i++) {
		argument_types.write[i].script_type_ref = Ref<Script>();
	}
	return_type.script_type_ref = Ref<Script>();

#ifdef DEBUG_ENABLED
	MutexLock lock(GDScriptLanguage::get_singleton()->mutex);
	GDScriptLanguage::get_singleton()->function_list.remove(&function_list);
#endif
}

/////////////////////

Variant GDScriptFunctionState::_signal_callback(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	Variant arg;
	r_error.error = Callable::CallError::CALL_OK;

	if (p_argcount == 0) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		return Variant();
	} else if (p_argcount == 1) {
		//noooneee
	} else if (p_argcount == 2) {
		arg = *p_args[0];
	} else {
		Array extra_args;
		for (int i = 0; i < p_argcount - 1; i++) {
			extra_args.push_back(*p_args[i]);
		}
		arg = extra_args;
	}

	Ref<GDScriptFunctionState> self = *p_args[p_argcount - 1];

	if (self.is_null()) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = p_argcount - 1;
		r_error.expected = Variant::OBJECT;
		return Variant();
	}

	return resume(arg);
}

bool GDScriptFunctionState::is_valid(bool p_extended_check) const {
	if (function == nullptr) {
		return false;
	}

	if (p_extended_check) {
		MutexLock lock(GDScriptLanguage::get_singleton()->mutex);

		// Script gone?
		if (!scripts_list.in_list()) {
			return false;
		}
		// Class instance gone? (if not static function)
		if (state.instance && !instances_list.in_list()) {
			return false;
		}
	}

	return true;
}

Variant GDScriptFunctionState::resume(const Variant &p_arg) {
	ERR_FAIL_NULL_V(function, Variant());
	{
		MutexLock lock(GDScriptLanguage::singleton->mutex);

		if (!scripts_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but script is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		if (state.instance && !instances_list.in_list()) {
#ifdef DEBUG_ENABLED
			ERR_FAIL_V_MSG(Variant(), "Resumed function '" + state.function_name + "()' after await, but class instance is gone. At script: " + state.script_path + ":" + itos(state.line));
#else
			return Variant();
#endif
		}
		// Do these now to avoid locking again after the call
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}

	state.result = p_arg;
	Callable::CallError err;
	Variant ret = function->call(nullptr, nullptr, 0, err, &state);

	bool completed = true;

	// If the return value is a GDScriptFunctionState reference,
	// then the function did await again after resuming.
	if (ret.is_ref_counted()) {
		GDScriptFunctionState *gdfs = Object::cast_to<GDScriptFunctionState>(ret);
		if (gdfs && gdfs->function == function) {
			completed = false;
			// Keep the first state alive via reference.
			gdfs->first_state = first_state.is_valid() ? first_state : Ref<GDScriptFunctionState>(this);
		}
	}

	function = nullptr; //cleaned up;
	state.result = Variant();

	if (completed) {
		_clear_stack();
	}

	return ret;
}

void GDScriptFunctionState::_clear_stack() {
	if (state.stack_size) {
		Variant *stack = (Variant *)state.stack.ptr();
		// First `GDScriptFunction::FIXED_ADDRESSES_MAX` stack addresses are special
		// and not copied to the state, so we skip them here.
		for (int i = GDScriptFunction::FIXED_ADDRESSES_MAX; i < state.stack_size; i++) {
			stack[i].~Variant();
		}
		state.stack_size = 0;
	}
}

void GDScriptFunctionState::_clear_connections() {
	List<Object::Connection> conns;
	get_signals_connected_to_this(&conns);

	for (Object::Connection &c : conns) {
		c.signal.disconnect(c.callable);
	}
}

void GDScriptFunctionState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("resume", "arg"), &GDScriptFunctionState::resume, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("is_valid", "extended_check"), &GDScriptFunctionState::is_valid, DEFVAL(false));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_signal_callback", &GDScriptFunctionState::_signal_callback, MethodInfo("_signal_callback"));

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

GDScriptFunctionState::GDScriptFunctionState() :
		scripts_list(this),
		instances_list(this) {
}

GDScriptFunctionState::~GDScriptFunctionState() {
	{
		MutexLock lock(GDScriptLanguage::singleton->mutex);
		scripts_list.remove_from_list();
		instances_list.remove_from_list();
	}
}
