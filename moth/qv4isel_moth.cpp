#include "qv4isel_moth_p.h"
#include "qv4vme_moth_p.h"

using namespace QQmlJS;
using namespace QQmlJS::Moth;

namespace {
QTextStream qout(stderr, QIODevice::WriteOnly);
}

InstructionSelection::InstructionSelection(VM::ExecutionEngine *engine, IR::Module * /*module*/,
                                           uchar *code)
: _engine(engine), _code(code), _ccode(code)
{
}

InstructionSelection::~InstructionSelection()
{
}

void InstructionSelection::operator()(IR::Function *function)
{
    qSwap(_function, function);

    _function->code = VME::exec;
    _function->codeData = _ccode;

    int locals = _function->tempCount - _function->locals.size() + _function->maxNumberOfArguments;
    assert(locals >= 0);

    Instruction::Push push;
    push.value = quint32(locals);
    addInstruction(push);

    foreach (_block, _function->basicBlocks) {
        _addrs.insert(_block, _ccode - _code);

        foreach (IR::Stmt *s, _block->statements)
            s->accept(this);
    }

    for (QHash<IR::BasicBlock *, QVector<ptrdiff_t> >::ConstIterator iter = _patches.begin();
         iter != _patches.end(); ++iter) {

        Q_ASSERT(_addrs.contains(iter.key()));
        ptrdiff_t target = _addrs.value(iter.key());

        const QVector<ptrdiff_t> &patchList = iter.value();
        for (int ii = 0; ii < patchList.count(); ++ii) {
            ptrdiff_t patch = patchList.at(ii);

            *((ptrdiff_t *)(_code + patch)) = target - patch;
        }
    }

    qSwap(_function, function);
}

void InstructionSelection::callActivationProperty(IR::Call *c)
{
    IR::Name *baseName = c->base->asName();
    Q_ASSERT(baseName);

    switch (baseName->builtin) {
    case IR::Name::builtin_invalid: {
        Instruction::LoadName load;
        load.name = _engine->newString(*baseName->id);
        addInstruction(load);

        Instruction::CallValue call;
        prepareCallArgs(c->args, call.argc, call.args);
        addInstruction(call);
    } break;

    case IR::Name::builtin_typeof: {
        IR::Temp *arg = c->args->expr->asTemp();
        assert(arg != 0);

        Instruction::CallBuiltin call;
        call.builtin = Instruction::CallBuiltin::builtin_typeof;
        prepareCallArgs(c->args, call.argc, call.args);
        addInstruction(call);
    } break;

    case IR::Name::builtin_throw: {
        IR::Temp *arg = c->args->expr->asTemp();
        assert(arg != 0);

        Instruction::CallBuiltin call;
        call.builtin = Instruction::CallBuiltin::builtin_throw;
        prepareCallArgs(c->args, call.argc, call.args);
        addInstruction(call);
    } break;

    case IR::Name::builtin_create_exception_handler: {
        Instruction::CallBuiltin call;
        call.builtin = Instruction::CallBuiltin::builtin_create_exception_handler;
        addInstruction(call);
    } break;

    case IR::Name::builtin_delete_exception_handler: {
        Instruction::CallBuiltin call;
        call.builtin = Instruction::CallBuiltin::builtin_delete_exception_handler;
        addInstruction(call);
    } break;

    case IR::Name::builtin_get_exception: {
        Instruction::CallBuiltin call;
        call.builtin = Instruction::CallBuiltin::builtin_get_exception;
        addInstruction(call);
    } break;

    case IR::Name::builtin_delete:
        Q_UNIMPLEMENTED();
        break;

    default:
        Q_UNIMPLEMENTED();
    }
}

void InstructionSelection::callValue(IR::Call *c)
{
    IR::Temp *t = c->base->asTemp();
    Q_ASSERT(t);

    Instruction::LoadTemp load;
    load.tempIndex = t->index;
    addInstruction(load);

    Instruction::CallValue call;
    prepareCallArgs(c->args, call.argc, call.args);
    addInstruction(call);
}

void InstructionSelection::callProperty(IR::Call *c)
{
    IR::Member *m = c->base->asMember();
    Q_ASSERT(m);

    // load the base
    Instruction::LoadTemp load;
    load.tempIndex = m->base->asTemp()->index;
    addInstruction(load);

    // call the property on the loaded base
    Instruction::CallProperty call;
    call.name = _engine->newString(*m->name);
    prepareCallArgs(c->args, call.argc, call.args);
    addInstruction(call);
}

void InstructionSelection::construct(IR::New *ctor)
{
    if (IR::Name *baseName = ctor->base->asName()) {
        Instruction::CreateActivationProperty create;
        create.name = _engine->newString(*baseName->id);
        prepareCallArgs(ctor->args, create.argc, create.args);
        addInstruction(create);
    } else if (IR::Member *member = ctor->base->asMember()) {
        IR::Temp *base = member->base->asTemp();
        assert(base != 0);

        Instruction::CreateProperty create;
        create.base = base->index;
        create.name = _engine->newString(*member->name);
        prepareCallArgs(ctor->args, create.argc, create.args);
        addInstruction(create);
    } else if (IR::Temp *baseTemp = ctor->base->asTemp()) {
        Instruction::CreateValue create;
        create.func = baseTemp->index;
        prepareCallArgs(ctor->args, create.argc, create.args);
        addInstruction(create);
    } else {
        qWarning("  NEW");
    }
}

void InstructionSelection::prepareCallArgs(IR::ExprList *e, quint32 &argc, quint32 &args)
{
    argc = 0;
    args = 0;

    int locals = _function->tempCount - _function->locals.size() + _function->maxNumberOfArguments;

    if (e && e->next == 0 && e->expr->asTemp()->index >= 0 && e->expr->asTemp()->index < locals) {
        // We pass single arguments as references to the stack
        argc = 1;
        args = e->expr->asTemp()->index;
    } else if (e) {
        // We need to move all the temps into the function arg array
        int argLocation = _function->tempCount - _function->locals.size();
        assert(argLocation >= 0);
        args = argLocation;
        while (e) {
            Instruction::MoveTemp move;
            move.fromTempIndex = e->expr->asTemp()->index;
            move.toTempIndex = argLocation;
            addInstruction(move);
            ++argLocation;
            ++argc;
            e = e->next;
        }
    }
}

void InstructionSelection::visitExp(IR::Exp *s)
{
    if (IR::Call *c = s->expr->asCall()) {
        if (c->base->asName()) {
            callActivationProperty(c);
        } else if (c->base->asTemp()) {
            callValue(c);
        } else if (c->base->asMember()) {
            callProperty(c);
        } else {
            Q_UNREACHABLE();
        }

        // TODO: check if we should store the return value ?
    } else {
        Q_UNREACHABLE();
    }
}

void InstructionSelection::visitEnter(IR::Enter *)
{
    qWarning("%s", __PRETTY_FUNCTION__);
    Q_UNREACHABLE();
}

void InstructionSelection::visitLeave(IR::Leave *)
{
    qWarning("%s", __PRETTY_FUNCTION__);
    Q_UNREACHABLE();
}

typedef VM::Value (*ALUFunction)(const VM::Value, const VM::Value, VM::Context*);
static inline ALUFunction aluOpFunction(IR::AluOp op)
{
    switch (op) {
    case IR::OpInvalid:
        return 0;
    case IR::OpIfTrue:
        return 0;
    case IR::OpNot:
        return 0;
    case IR::OpUMinus:
        return 0;
    case IR::OpUPlus:
        return 0;
    case IR::OpCompl:
        return 0;
    case IR::OpBitAnd:
        return VM::__qmljs_bit_and;
    case IR::OpBitOr:
        return VM::__qmljs_bit_or;
    case IR::OpBitXor:
        return VM::__qmljs_bit_xor;
    case IR::OpAdd:
        return VM::__qmljs_add;
    case IR::OpSub:
        return VM::__qmljs_sub;
    case IR::OpMul:
        return VM::__qmljs_mul;
    case IR::OpDiv:
        return VM::__qmljs_div;
    case IR::OpMod:
        return VM::__qmljs_mod;
    case IR::OpLShift:
        return VM::__qmljs_shl;
    case IR::OpRShift:
        return VM::__qmljs_shr;
    case IR::OpURShift:
        return VM::__qmljs_ushr;
    case IR::OpGt:
        return VM::__qmljs_gt;
    case IR::OpLt:
        return VM::__qmljs_lt;
    case IR::OpGe:
        return VM::__qmljs_ge;
    case IR::OpLe:
        return VM::__qmljs_le;
    case IR::OpEqual:
        return VM::__qmljs_eq;
    case IR::OpNotEqual:
        return VM::__qmljs_ne;
    case IR::OpStrictEqual:
        return VM::__qmljs_se;
    case IR::OpStrictNotEqual:
        return VM::__qmljs_sne;
    case IR::OpInstanceof:
        return VM::__qmljs_instanceof;
    case IR::OpIn:
        return VM::__qmljs_in;
    case IR::OpAnd:
        return 0;
    case IR::OpOr:
        return 0;
    default:
        assert(!"Unknown AluOp");
        return 0;
    }
};

void InstructionSelection::visitMove(IR::Move *s)
{
    if (IR::Name *n = s->target->asName()) {
        Q_UNUSED(n);
        // set activation property
        if (IR::Temp *t = s->source->asTemp()) {
            // TODO: fold the next 2 instructions.
            Instruction::LoadTemp load;
            load.tempIndex = t->index;
            addInstruction(load);

            Instruction::StoreName store;
            store.name = _engine->newString(*n->id);
            addInstruction(store);
            return;
        } else {
            Q_UNREACHABLE();
        }
    } else if (IR::Temp *t = s->target->asTemp()) {
        // Check what kind of load it is, and generate the instruction for that.
        // The store to the temp (the target) is done afterwards.
        if (IR::Name *n = s->source->asName()) {
            Q_UNUSED(n);
            if (*n->id == QStringLiteral("this")) { // ### `this' should be a builtin.
                addInstruction(Instruction::LoadThis());
            } else {
                Instruction::LoadName load;
                load.name = _engine->newString(*n->id);
                addInstruction(load);
            }
        } else if (IR::Const *c = s->source->asConst()) {
            switch (c->type) {
            case IR::UndefinedType:
                addInstruction(Instruction::LoadUndefined());
                break;
            case IR::NullType:
                addInstruction(Instruction::LoadNull());
                break;
            case IR::BoolType:
                if (c->value) addInstruction(Instruction::LoadTrue());
                else addInstruction(Instruction::LoadFalse());
                break;
            case IR::NumberType: {
                Instruction::LoadNumber load;
                load.value = c->value;
                addInstruction(load);
                } break;
            default:
                Q_UNREACHABLE();
                break;
            }
        } else if (IR::Temp *t2 = s->source->asTemp()) {
            Instruction::LoadTemp load;
            load.tempIndex = t2->index;
            addInstruction(load);
        } else if (IR::String *str = s->source->asString()) {
            Instruction::LoadString load;
            load.value = _engine->newString(*str->value);
            addInstruction(load);
        } else if (IR::Closure *clos = s->source->asClosure()) {
            Instruction::LoadClosure load;
            load.value = clos->value;
            addInstruction(load);
        } else if (IR::New *ctor = s->source->asNew()) {
            construct(ctor);
        } else if (IR::Member *m = s->source->asMember()) {
            if (IR::Temp *base = m->base->asTemp()) {
                Instruction::LoadProperty load;
                load.baseTemp = base->index;
                load.name = _engine->newString(*m->name);
                addInstruction(load);
            } else {
                qWarning("  MEMBER");
            }
        } else if (IR::Subscript *ss = s->source->asSubscript()) {
            Instruction::LoadElement load;
            load.base = ss->base->asTemp()->index;
            load.index = ss->index->asTemp()->index;
            addInstruction(load);
        } else if (IR::Unop *u = s->source->asUnop()) {
            if (IR::Temp *e = u->expr->asTemp()) {
                VM::Value (*op)(const VM::Value value, VM::Context *ctx) = 0;
                switch (u->op) {
                case IR::OpIfTrue: assert(!"unreachable"); break;
                case IR::OpNot: op = VM::__qmljs_not; break;
                case IR::OpUMinus: op = VM::__qmljs_uminus; break;
                case IR::OpUPlus: op = VM::__qmljs_uplus; break;
                case IR::OpCompl: op = VM::__qmljs_compl; break;
                default: assert(!"unreachable"); break;
                } // switch

                if (op) {
                    Instruction::Unop unop;
                    unop.alu = op;
                    unop.e = e->index;
                    addInstruction(unop);
                }
            } else {
                qWarning("  UNOP");
                s->dump(qout, IR::Stmt::MIR);
                qout << endl;
            }
        } else if (IR::Binop *b = s->source->asBinop()) {
            Instruction::Binop binop;
            binop.alu = aluOpFunction(b->op);
            binop.lhsTempIndex = b->left->index;
            binop.rhsTempIndex = b->right->index;
            addInstruction(binop);
        } else if (IR::Call *c = s->source->asCall()) {
            if (c->base->asName()) {
                callActivationProperty(c);
            } else if (c->base->asMember()) {
                callProperty(c);
            } else if (c->base->asTemp()) {
                callValue(c);
            } else {
                Q_UNREACHABLE();
            }
        }

        Instruction::StoreTemp st;
        st.tempIndex = t->index;
        addInstruction(st);
        return;
    } else if (IR::Subscript *ss = s->target->asSubscript()) {
        if (IR::Temp *t = s->source->asTemp()) {
            void (*op)(VM::Value base, VM::Value index, VM::Value value, VM::Context *ctx) = 0;
            switch (s->op) {
            case IR::OpBitAnd: op = VM::__qmljs_inplace_bit_and_element; break;
            case IR::OpBitOr: op = VM::__qmljs_inplace_bit_or_element; break;
            case IR::OpBitXor: op = VM::__qmljs_inplace_bit_xor_element; break;
            case IR::OpAdd: op = VM::__qmljs_inplace_add_element; break;
            case IR::OpSub: op = VM::__qmljs_inplace_sub_element; break;
            case IR::OpMul: op = VM::__qmljs_inplace_mul_element; break;
            case IR::OpDiv: op = VM::__qmljs_inplace_div_element; break;
            case IR::OpMod: op = VM::__qmljs_inplace_mod_element; break;
            case IR::OpLShift: op = VM::__qmljs_inplace_shl_element; break;
            case IR::OpRShift: op = VM::__qmljs_inplace_shr_element; break;
            case IR::OpURShift: op = VM::__qmljs_inplace_ushr_element; break;
            default: break;
            }

            if (op) {
                Instruction::InplaceElementOp ieo;
                ieo.alu = op;
                ieo.targetBase = ss->base->asTemp()->index;
                ieo.targetIndex = ss->index->asTemp()->index;
                ieo.source = t->index;
                addInstruction(ieo);
                return;
            } else if (s->op == IR::OpInvalid) {
                if (IR::Temp *t2 = s->source->asTemp()) {
                    Instruction::LoadTemp load;
                    load.tempIndex = t2->index;
                    addInstruction(load);

                    Instruction::StoreElement store;
                    store.base = ss->base->asTemp()->index;
                    store.index = ss->index->asTemp()->index;
                    addInstruction(store);
                    return;
                }
            }
        }
        qWarning("SUBSCRIPT");
    } else if (IR::Member *m = s->target->asMember()) {
        if (IR::Temp *t = s->source->asTemp()) {
            void (*op)(VM::Value value, VM::Value base, VM::String *name, VM::Context *ctx) = 0;
            switch (s->op) {
            case IR::OpBitAnd: op = VM::__qmljs_inplace_bit_and_member; break;
            case IR::OpBitOr: op = VM::__qmljs_inplace_bit_or_member; break;
            case IR::OpBitXor: op = VM::__qmljs_inplace_bit_xor_member; break;
            case IR::OpAdd: op = VM::__qmljs_inplace_add_member; break;
            case IR::OpSub: op = VM::__qmljs_inplace_sub_member; break;
            case IR::OpMul: op = VM::__qmljs_inplace_mul_member; break;
            case IR::OpDiv: op = VM::__qmljs_inplace_div_member; break;
            case IR::OpMod: op = VM::__qmljs_inplace_mod_member; break;
            case IR::OpLShift: op = VM::__qmljs_inplace_shl_member; break;
            case IR::OpRShift: op = VM::__qmljs_inplace_shr_member; break;
            case IR::OpURShift: op = VM::__qmljs_inplace_ushr_member; break;
            default: break;
            }

            if (op) {
                Instruction::InplaceMemberOp imo;
                imo.alu = op;
                imo.targetBase = m->base->asTemp()->index;
                imo.targetMember = _engine->newString(*m->name);
                imo.source = t->index;
                addInstruction(imo);
                return;
            } else if (s->op == IR::OpInvalid) {
                Instruction::LoadTemp load;
                load.tempIndex = t->index;
                addInstruction(load);

                Instruction::StoreProperty store;
                store.baseTemp = m->base->asTemp()->index;
                store.name = _engine->newString(*m->name);
                addInstruction(store);
                return;
            }
        }
        qWarning("MEMBER");
    }

    Q_UNIMPLEMENTED();
    s->dump(qout, IR::Stmt::MIR);
    qout << endl;
    Q_UNREACHABLE();
}

void InstructionSelection::visitJump(IR::Jump *s)
{
    Instruction::Jump jump;
    jump.offset = 0;
    ptrdiff_t loc = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));

    _patches[s->target].append(loc);
}

void InstructionSelection::visitCJump(IR::CJump *s)
{
    if (IR::Temp *t = s->cond->asTemp()) {
        Instruction::LoadTemp load;
        load.tempIndex = t->index;
        addInstruction(load);
    } else if (IR::Binop *b = s->cond->asBinop()) {
        Instruction::Binop binop;
        binop.alu = aluOpFunction(b->op);
        binop.lhsTempIndex = b->left->index;
        binop.rhsTempIndex = b->right->index;
        addInstruction(binop);
    } else {
        Q_UNREACHABLE();
    }

    Instruction::CJump jump;
    jump.offset = 0;
    ptrdiff_t tl = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));
    _patches[s->iftrue].append(tl);

    if (_block->index + 1 != s->iffalse->index) {
        Instruction::Jump jump;
        jump.offset = 0;
        ptrdiff_t fl = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));
        _patches[s->iffalse].append(fl);
    }
}

void InstructionSelection::visitRet(IR::Ret *s)
{
    Instruction::Ret ret;
    ret.tempIndex = s->expr->index;
    addInstruction(ret);
}

ptrdiff_t InstructionSelection::addInstructionHelper(Instr::Type type, Instr &instr)
{
#ifdef MOTH_THREADED_INTERPRETER
    instr.common.code = VME::instructionJumpTable()[static_cast<int>(type)];
#else
    instr.common.instructionType = type;
#endif

    ptrdiff_t ptrOffset = _ccode - _code;
    int size = Instr::size(type);

    ::memcpy(_ccode, reinterpret_cast<const char *>(&instr), size);
    _ccode += size;

    return ptrOffset;
}

