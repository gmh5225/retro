#include <retro/common.hpp>
#include <retro/arch/x86/sema.hpp>

using namespace retro;
using namespace retro::arch::x86;

// TODO: Special handling:
//  pxor xmm0, xmm0
//  lock or [rsp], 0
//
//

// Declare semantics.
//
DECL_SEMA(NOP) {
	return diag::ok;
}
DECL_SEMA(MOV) {
	// Pattern: [mov reg, reg] <=> [nop]
	if (ins.op[0].type == arch::mop_type::reg && ins.op[1].type == arch::mop_type::reg) {
		if (ins.op[0].r == ins.op[1].r) {
			return diag::ok;
		}
	}

	auto ty = ir::int_type(ins.effective_width);
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVZX) {
	auto t0 = ir::int_type(ins.op[0].get_width());
	auto t1 = ir::int_type(ins.op[1].get_width());
	write(sema_context(), 0, bb->push_cast(t1, read(sema_context(), 1, t1)));
	return diag::ok;
}
DECL_SEMA(MOVSX) {
	auto t0 = ir::int_type(ins.op[0].get_width());
	auto t1 = ir::int_type(ins.op[1].get_width());
	write(sema_context(), 0, bb->push_sign_extend(t1, read(sema_context(), 1, t1)));
	return diag::ok;
}
DECL_SEMA(MOVSXD) {
	auto t0 = ir::int_type(ins.op[0].get_width());
	auto t1 = ir::int_type(ins.op[1].get_width());
	write(sema_context(), 0, bb->push_sign_extend(t1, read(sema_context(), 1, t1)));
	return diag::ok;
}
DECL_SEMA(LEA) {
	// Pattern: [lea reg, [reg]] <=> [nop]
	if (ins.op[0].type == arch::mop_type::reg && !ins.op[1].m.index && ins.op[1].m.disp) {
		if (ins.op[0].r == ins.op[1].m.base) {
			return diag::ok;
		}
	}

	auto [ptr, seg] = agen(sema_context(), ins.op[1].m, false);
	write_reg(sema_context(), ins.op[0].r, std::move(ptr));
	return diag::ok;
}
DECL_SEMA(PUSH) {
	auto rsp	 = reg_sp(mach);
	auto pty		 = mach->ptr_type();
	auto ty		 = ir::int_type(ins.effective_width);	// TODO: Test
	i32  dif		 = ins.effective_width == 2 ? 2 : mach->ptr_width / 8;
	auto prev_sp = read_reg(sema_context(), rsp, pty);

	// Update SP.
	auto value	 = read(sema_context(), 0, ty);
	auto new_sp	 = bb->push_binop(ir::op::sub, prev_sp, ir::constant(pty, dif));
	write_reg(sema_context(), rsp, new_sp);

	// Write the value.
	bb->push_store_mem(ir::NO_SEGMENT, bb->push_cast(ir::type::pointer, new_sp), std::move(value));
	return diag::ok;
}
DECL_SEMA(POP) {
	auto rsp	 = reg_sp(mach);
	auto pty		 = mach->ptr_type();
	auto ty		 = ir::int_type(ins.effective_width);	// TODO: Test
	i32  dif		 = ins.effective_width == 2 ? 2 : mach->ptr_width / 8;
	auto prev_sp = read_reg(sema_context(), rsp, pty);

	// Read the value.
	auto value = bb->push_load_mem(ty, ir::NO_SEGMENT, bb->push_cast(ir::type::pointer, prev_sp));
	
	// Update SP.
	auto new_sp = bb->push_binop(ir::op::add, prev_sp, ir::constant(pty, dif));
	write_reg(sema_context(), rsp, new_sp);

	// Store the operand.
	write(sema_context(), 0, value);
	return diag::ok;
}


DECL_SEMA(MOVUPS) {
	constexpr auto ty = ir::type::f32x4;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVAPS) {
	constexpr auto ty = ir::type::f32x4;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVUPD) {
	constexpr auto ty = ir::type::f64x2;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVAPD) {
	constexpr auto ty = ir::type::f64x2;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVDQU) {
	constexpr auto ty = ir::type::i32x4;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}
DECL_SEMA(MOVDQA) {
	constexpr auto ty = ir::type::i32x4;
	write(sema_context(), 0, read(sema_context(), 1, ty));
	return diag::ok;
}


// Arithmetic.
//
DECL_SEMA(ADD) {
	auto ty	= ir::int_type(ins.effective_width);
	auto rhs = read(sema_context(), 1, ty);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs	 = bb->push_atomic_binop(ir::op::add, seg, std::move(ptr), rhs);
		result = bb->push_binop(ir::op::add, lhs, rhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_binop(ir::op::add, lhs, rhs);
		write(sema_context(), 0, result);
	}

	set_af(bb, lhs, rhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "ADD - Overflow flag NYI"));	// TODO
	auto c0 = bb->push_cmp(ir::op::ult, result, lhs);
	auto c1 = bb->push_cmp(ir::op::ult, result, rhs);
	bb->push_write_reg(reg::flag_cf, bb->push_binop(ir::op::bit_or, c0, c1));
	return diag::ok;
}
DECL_SEMA(SUB) {
	auto ty	= ir::int_type(ins.effective_width);
	auto rhs = read(sema_context(), 1, ty);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_binop(ir::op::sub, seg, std::move(ptr), rhs);
		result			 = bb->push_binop(ir::op::sub, lhs, rhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_binop(ir::op::sub, lhs, rhs);
		write(sema_context(), 0, result);
	}

	set_af(bb, lhs, rhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "SUB - Overflow flag NYI"));	// TODO
	bb->push_write_reg(reg::flag_cf, bb->push_cmp(ir::op::ult, lhs, rhs));
	return diag::ok;
}
DECL_SEMA(INC) {
	auto ty	= ir::int_type(ins.effective_width);
	auto rhs = ir::constant(ty, 1);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_binop(ir::op::add, seg, std::move(ptr), rhs);
		result			 = bb->push_binop(ir::op::add, lhs, rhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_binop(ir::op::add, lhs, rhs);
		write(sema_context(), 0, result);
	}

	set_af(bb, lhs, rhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "INC - Overflow flag NYI"));	// TODO
	return diag::ok;
}
DECL_SEMA(DEC) {
	auto ty	= ir::int_type(ins.effective_width);
	auto rhs = ir::constant(ty, 1);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_binop(ir::op::sub, seg, std::move(ptr), rhs);
		result			 = bb->push_binop(ir::op::sub, lhs, rhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_binop(ir::op::sub, lhs, rhs);
		write(sema_context(), 0, result);
	}

	set_af(bb, lhs, rhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "SUB - Overflow flag NYI"));	// TODO
	return diag::ok;
}
DECL_SEMA(NEG) {
	auto ty = ir::int_type(ins.effective_width);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_unop(ty, ir::op::neg, seg, std::move(ptr));
		result			 = bb->push_unop(ir::op::neg, lhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_unop(ir::op::neg, lhs);
		write(sema_context(), 0, result);
	}

	set_af(bb, lhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_cf, bb->push_cmp(ir::op::ne, lhs, ir::constant(ty, 0)));
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "SUB/NEG - Overflow flag NYI"));	 // TODO
	return diag::ok;
}

// Logical.
//
template<auto Operation>
static diag::lazy logical(SemaContext) {
	auto ty	= ir::int_type(ins.effective_width);
	auto rhs = read(sema_context(), 1, ty);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_binop(Operation, seg, std::move(ptr), rhs);
		result			 = bb->push_binop(Operation, lhs, rhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_binop(Operation, lhs, rhs);
		write(sema_context(), 0, result);
	}

	set_flags_logical(bb, result);
	return diag::ok;
}
DECL_SEMA(OR) { return logical<ir::op::bit_or>(sema_context()); }
DECL_SEMA(AND) { return logical<ir::op::bit_and>(sema_context()); }
DECL_SEMA(XOR) {
	// Pattern: [xor reg, reg] <=> [mov reg, 0] + flags update
	if (ins.op[0].type == arch::mop_type::reg && ins.op[1].type == arch::mop_type::reg) {
		if (ins.op[0].r == ins.op[1].r) {
			bb->push_write_reg(reg::flag_sf, false);
			bb->push_write_reg(reg::flag_zf, true);
			bb->push_write_reg(reg::flag_pf, true);
			bb->push_write_reg(reg::flag_of, false);
			bb->push_write_reg(reg::flag_cf, false);
			bb->push_write_reg(reg::flag_af, false);
			write_reg(sema_context(), ins.op[0].r, ir::constant(ir::int_type(ins.effective_width), 0));
			return diag::ok;
		}
	}
	return logical<ir::op::bit_xor>(sema_context());
}
DECL_SEMA(NOT) {
	auto ty	= ir::int_type(ins.effective_width);

	ir::insn*	result;
	ir::variant lhs;
	if (ins.modifiers & ZYDIS_ATTRIB_HAS_LOCK) {
		auto [ptr, seg] = agen(sema_context(), ins.op[0].m, true);
		lhs				 = bb->push_atomic_unop(ty, ir::op::bit_not, seg, std::move(ptr));
		result			 = bb->push_unop(ir::op::bit_not, lhs);
	} else {
		lhs	 = read(sema_context(), 0, ty);
		result = bb->push_unop(ir::op::bit_not, lhs);
		write(sema_context(), 0, result);
	}
	return diag::ok;
}


template<auto Operation>
static diag::lazy shift(SemaContext) {
	auto ty		= ir::int_type(ins.effective_width);
	auto rhs		= read(sema_context(), 1, ty);
	auto lhs		= read(sema_context(), 0, ty);
	auto result = bb->push_binop(Operation, lhs, rhs);
	write(sema_context(), 0, result);

	/*
	The CF flag contains the value of the last bit shifted out of the destination operand; it is undefined for SHL and SHR instructions where the count is greater than or equal to
	the size (in bits) of the destination operand.
	The OF flag is affected only for 1-bit shifts (see “Description” above); otherwise, it is undefined.
	*/
	/*
	The SF, ZF, and PF flags are
	set according to the result. If the count is 0, the flags are not affected. For a non-zero count, the AF flag is undefined.
	*/
	bb->push_write_reg(reg::flag_cf, bb->push_poison(ir::type::i1, "Shift - Carry flag NYI"));	  // TODO
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "Shift - Overflow flag NYI"));  // TODO
	bb->push_write_reg(reg::flag_sf, bb->push_poison(ir::type::i1, "Shift - Sign flag NYI"));		  // TODO
	bb->push_write_reg(reg::flag_zf, bb->push_poison(ir::type::i1, "Shift - Zero flag NYI"));		  // TODO
	bb->push_write_reg(reg::flag_pf, bb->push_poison(ir::type::i1, "Shift - Parity flag NYI"));	  // TODO
	return diag::ok;
}
template<auto Operation>
static diag::lazy rot(SemaContext) {
	auto ty		= ir::int_type(ins.effective_width);
	auto rhs		= read(sema_context(), 1, ty);
	auto lhs		= read(sema_context(), 0, ty);
	auto result = bb->push_binop(Operation, lhs, rhs);
	write(sema_context(), 0, result);

	/*
	If the masked count is 0, the flags are not affected. If the masked count is 1, then the OF flag is affected, otherwise (masked count is greater than 1) the OF flag is undefined.
	The CF flag is affected when the masked count is nonzero. The SF, ZF, AF, and PF flags are always unaffected.
	*/
	bb->push_write_reg(reg::flag_cf, bb->push_poison(ir::type::i1, "Rotate - Carry flag NYI"));	  // TODO
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "Rotate - Overflow flag NYI"));  // TODO
	return diag::ok;
}
DECL_SEMA(SHR) { return shift<ir::op::bit_shr>(sema_context()); }
DECL_SEMA(SHL) { return shift<ir::op::bit_shl>(sema_context()); }
DECL_SEMA(SAR) { return shift<ir::op::bit_sar>(sema_context()); }
DECL_SEMA(ROR) { return shift<ir::op::bit_ror>(sema_context()); }
DECL_SEMA(ROL) { return shift<ir::op::bit_rol>(sema_context()); }
// TODO: RCL, RCR, SHRD, SHLD.
//       SHLX, SHRX, SARX, RORX.


// Comparisons.
//
DECL_SEMA(CMP) {
	auto ty		= ir::int_type(ins.effective_width);
	auto lhs		= read(sema_context(), 0, ty);
	auto rhs		= read(sema_context(), 1, ty);
	auto result = bb->push_binop(ir::op::sub, lhs, rhs);
	set_af(bb, lhs, rhs, result);
	set_sf(bb, result);
	set_zf(bb, result);
	set_pf(bb, result);
	bb->push_write_reg(reg::flag_of, bb->push_poison(ir::type::i1, "SUB - Overflow flag NYI"));	// TODO
	bb->push_write_reg(reg::flag_cf, bb->push_cmp(ir::op::ult, lhs, rhs));
	return diag::ok;
}
DECL_SEMA(TEST) {
	auto ty		= ir::int_type(ins.effective_width);
	auto lhs		= read(sema_context(), 0, ty);
	auto rhs		= read(sema_context(), 1, ty);
	auto result = bb->push_binop(ir::op::bit_and, lhs, rhs);
	set_flags_logical(bb, result);
	return diag::ok;
}

// CALL / JMP / RET.
//
DECL_SEMA(CALL) {
	bb->push_xcall(read(sema_context(), 0, ir::type::pointer));
	return diag::ok;
}
DECL_SEMA(JMP) {
	bb->push_xjmp(read(sema_context(), 0, ir::type::pointer));
	return diag::ok;
}
DECL_SEMA(RET) {
	// RET imm16:
	if (ins.operand_count) {
		// SP = SP + Imm.
		auto rsp	 = reg_sp(mach);
		auto pty = mach->ptr_type();
		auto prev_sp = read_reg(sema_context(), rsp, pty);
		auto new_sp	 = bb->push_binop(ir::op::add, prev_sp, ir::constant(pty, ins.op[0].i.s));
		write_reg(sema_context(), rsp, new_sp);
	}
	bb->push_ret(std::nullopt);
	return diag::ok;
}

// INT1 / INT3 / UD2
//
DECL_SEMA(UD2) {
	bb->push_trap("ud2");
	return diag::ok;
}
DECL_SEMA(INT3) {
	bb->push_trap("int3");
	return diag::ok;
}
DECL_SEMA(INT1) {
	bb->push_trap("int1");
	return diag::ok;
}
// TODO: INT / INTO

/*
pushfq
popfq

xchg -> 4 0.018925%
movsd -> 85 0.402157%
xorps -> 24 0.113550%
cmpxchg -> 1 0.004731%

xadd -> 46 0.217638%
bts -> 28 0.132475%
bt -> 15 0.070969%
mul -> 10 0.047313%
imul -> 19 0.089894%
rol -> 13 0.061506%
ror -> 1 0.004731%
sbb -> 5 0.023656%
adc -> 3 0.014194%
div -> 1 0.004731%
cdqe -> 13 0.061506%
clc -> 10 0.047313%
cqo -> 8 0.037850%

movq -> 14 0.066238%
movd -> 5 0.023656%
movss -> 9 0.042581%
divss -> 1 0.004731%
cvtdq2pd -> 2 0.009463%
cvtdq2ps -> 2 0.009463%
cvtps2pd -> 2 0.009463%
cvtsi2ss -> 6 0.028388%
cvtsi2sd -> 4 0.018925%
addsd -> 1 0.004731%
addss -> 1 0.004731%
psrldq -> 6 0.028388%

scasd -> 1 0.004731%
stosw -> 3 0.014194%
lodsb -> 1 0.004731%
stosd -> 1 0.004731%

xgetbv -> 1 0.004731%
cpuid -> 3 0.014194%
*/