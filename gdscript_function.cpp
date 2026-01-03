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

	if (native_operator_hints.is_empty() || !_code_ptr || _operator_funcs_count == 0) {
		native_segments_ready = true;
		return;
	}

	NativeOperatorSegment current_segment;
	for (const NativeOperatorHint &hint : native_operator_hints) {
		if (!is_math_operator(hint.op)) {
			continue;
		}

		if (_code_ptr[hint.ip] != OPCODE_OPERATOR_VALIDATED) {
			continue;
		}

		int operator_func_index = _code_ptr[hint.ip + 4];
		if (operator_func_index < 0 || operator_func_index >= _operator_funcs_count) {
			continue;
		}

		NativeOperatorStep step;
		int a_address = _code_ptr[hint.ip + 1];
		int b_address = _code_ptr[hint.ip + 2];
		int dst_address = _code_ptr[hint.ip + 3];
		step.a_type = (uint8_t)((a_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		step.b_type = (uint8_t)((b_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		step.dst_type = (uint8_t)((dst_address & ADDR_TYPE_MASK) >> ADDR_BITS);
		step.a_index = (uint32_t)(a_address & ADDR_MASK);
		step.b_index = (uint32_t)(b_address & ADDR_MASK);
		step.dst_index = (uint32_t)(dst_address & ADDR_MASK);
		step.evaluator = _operator_funcs_ptr[operator_func_index];
		step.unary = hint.unary;

		// Group consecutive validated math operators into a single native segment.
		if (current_segment.steps.is_empty()) {
			current_segment.start_ip = hint.ip;
			current_segment.end_ip = hint.ip + 5;
			current_segment.steps.push_back(step);
			continue;
		}

		if (hint.ip == current_segment.end_ip) {
			current_segment.steps.push_back(step);
			current_segment.end_ip = hint.ip + 5;
		} else {
			native_segment_lookup.insert(current_segment.start_ip, native_operator_segments.size());
			native_operator_segments.push_back(current_segment);
			current_segment = NativeOperatorSegment();
			current_segment.start_ip = hint.ip;
			current_segment.end_ip = hint.ip + 5;
			current_segment.steps.push_back(step);
		}
	}

	if (!current_segment.steps.is_empty()) {
		native_segment_lookup.insert(current_segment.start_ip, native_operator_segments.size());
		native_operator_segments.push_back(current_segment);
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
