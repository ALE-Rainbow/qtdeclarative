/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the V4VM module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/
#include <qmljs_engine.h>
#include <qmljs_objects.h>
#include <qv4ecmaobjects_p.h>
#include "qv4mm.h"

namespace QQmlJS {
namespace VM {

struct StringPool
{
    QHash<QString, String*> strings;

    ~StringPool()
    { qDeleteAll(strings.values()); }

    String *newString(const QString &s)
    {
        QHash<QString, String*>::const_iterator it = strings.find(s);
        if (it != strings.end())
            return it.value();
        String *str = new String(s);
        strings.insert(s, str);
        return str;
    }
};

ExecutionEngine::ExecutionEngine(MemoryManager *memoryManager, EvalISelFactory *factory)
    : memoryManager(memoryManager)
    , iselFactory(factory)
    , debugger(0)
    , globalObject(Value::nullValue())
    , exception(Value::nullValue())
{
    MemoryManager::GCBlocker gcBlocker(memoryManager);

    stringPool.reset(new StringPool);
    memoryManager->setStringPool(stringPool.data());
    memoryManager->setExecutionEngine(this);

    rootContext = newContext();
    rootContext->init(this);
    current = rootContext;

    id_length = identifier(QStringLiteral("length"));
    id_prototype = identifier(QStringLiteral("prototype"));
    id_constructor = identifier(QStringLiteral("constructor"));
    id_arguments = identifier(QStringLiteral("arguments"));
    id_this = identifier(QStringLiteral("this"));
    id___proto__ = identifier(QStringLiteral("__proto__"));
    id_enumerable = identifier(QStringLiteral("enumerable"));
    id_configurable = identifier(QStringLiteral("configurable"));
    id_writable = identifier(QStringLiteral("writable"));
    id_value = identifier(QStringLiteral("value"));
    id_get = identifier(QStringLiteral("get"));
    id_set = identifier(QStringLiteral("set"));

    objectPrototype = new (memoryManager) ObjectPrototype();
    stringPrototype = new (memoryManager) StringPrototype(rootContext);
    numberPrototype = new (memoryManager) NumberPrototype();
    booleanPrototype = new (memoryManager) BooleanPrototype();
    arrayPrototype = new (memoryManager) ArrayPrototype();
    datePrototype = new (memoryManager) DatePrototype();
    functionPrototype = new (memoryManager) FunctionPrototype(rootContext);
    regExpPrototype = new (memoryManager) RegExpPrototype();
    errorPrototype = new (memoryManager) ErrorPrototype();
    evalErrorPrototype = new (memoryManager) EvalErrorPrototype(rootContext);
    rangeErrorPrototype = new (memoryManager) RangeErrorPrototype(rootContext);
    referenceErrorPrototype = new (memoryManager) ReferenceErrorPrototype(rootContext);
    syntaxErrorPrototype = new (memoryManager) SyntaxErrorPrototype(rootContext);
    typeErrorPrototype = new (memoryManager) TypeErrorPrototype(rootContext);
    uRIErrorPrototype = new (memoryManager) URIErrorPrototype(rootContext);

    stringPrototype->prototype = objectPrototype;
    numberPrototype->prototype = objectPrototype;
    booleanPrototype->prototype = objectPrototype;
    arrayPrototype->prototype = objectPrototype;
    datePrototype->prototype = objectPrototype;
    functionPrototype->prototype = objectPrototype;
    regExpPrototype->prototype = objectPrototype;
    errorPrototype->prototype = objectPrototype;
    evalErrorPrototype->prototype = errorPrototype;
    rangeErrorPrototype->prototype = errorPrototype;
    referenceErrorPrototype->prototype = errorPrototype;
    syntaxErrorPrototype->prototype = errorPrototype;
    typeErrorPrototype->prototype = errorPrototype;
    uRIErrorPrototype->prototype = errorPrototype;

    objectCtor = Value::fromObject(new (memoryManager) ObjectCtor(rootContext));
    stringCtor = Value::fromObject(new (memoryManager) StringCtor(rootContext));
    numberCtor = Value::fromObject(new (memoryManager) NumberCtor(rootContext));
    booleanCtor = Value::fromObject(new (memoryManager) BooleanCtor(rootContext));
    arrayCtor = Value::fromObject(new (memoryManager) ArrayCtor(rootContext));
    functionCtor = Value::fromObject(new (memoryManager) FunctionCtor(rootContext));
    dateCtor = Value::fromObject(new (memoryManager) DateCtor(rootContext));
    regExpCtor = Value::fromObject(new (memoryManager) RegExpCtor(rootContext));
    errorCtor = Value::fromObject(new (memoryManager) ErrorCtor(rootContext));
    evalErrorCtor = Value::fromObject(new (memoryManager) EvalErrorCtor(rootContext));
    rangeErrorCtor = Value::fromObject(new (memoryManager) RangeErrorCtor(rootContext));
    referenceErrorCtor = Value::fromObject(new (memoryManager) ReferenceErrorCtor(rootContext));
    syntaxErrorCtor = Value::fromObject(new (memoryManager) SyntaxErrorCtor(rootContext));
    typeErrorCtor = Value::fromObject(new (memoryManager) TypeErrorCtor(rootContext));
    uRIErrorCtor = Value::fromObject(new (memoryManager) URIErrorCtor(rootContext));

    stringCtor.objectValue()->prototype = functionPrototype;
    numberCtor.objectValue()->prototype = functionPrototype;
    booleanCtor.objectValue()->prototype = functionPrototype;
    arrayCtor.objectValue()->prototype = functionPrototype;
    functionCtor.objectValue()->prototype = functionPrototype;
    dateCtor.objectValue()->prototype = functionPrototype;
    regExpCtor.objectValue()->prototype = functionPrototype;
    errorCtor.objectValue()->prototype = functionPrototype;
    evalErrorCtor.objectValue()->prototype = errorPrototype;
    rangeErrorCtor.objectValue()->prototype = errorPrototype;
    referenceErrorCtor.objectValue()->prototype = errorPrototype;
    syntaxErrorCtor.objectValue()->prototype = errorPrototype;
    typeErrorCtor.objectValue()->prototype = errorPrototype;
    uRIErrorCtor.objectValue()->prototype = errorPrototype;

    objectPrototype->init(rootContext, objectCtor);
    stringPrototype->init(rootContext, stringCtor);
    numberPrototype->init(rootContext, numberCtor);
    booleanPrototype->init(rootContext, booleanCtor);
    arrayPrototype->init(rootContext, arrayCtor);
    datePrototype->init(rootContext, dateCtor);
    functionPrototype->init(rootContext, functionCtor);
    regExpPrototype->init(rootContext, regExpCtor);
    errorPrototype->init(rootContext, errorCtor);
    evalErrorPrototype->init(rootContext, evalErrorCtor);
    rangeErrorPrototype->init(rootContext, rangeErrorCtor);
    referenceErrorPrototype->init(rootContext, referenceErrorCtor);
    syntaxErrorPrototype->init(rootContext, syntaxErrorCtor);
    typeErrorPrototype->init(rootContext, typeErrorCtor);
    uRIErrorPrototype->init(rootContext, uRIErrorCtor);

    //
    // set up the global object
    //
    VM::Object *glo = newObject(/*rootContext*/);
    globalObject = Value::fromObject(glo);
    rootContext->activation = glo;
    rootContext->thisObject = Value::fromObject(glo);

    glo->defineDefaultProperty(rootContext, QStringLiteral("Object"), objectCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("String"), stringCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Number"), numberCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Boolean"), booleanCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Array"), arrayCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Function"), functionCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Date"), dateCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("RegExp"), regExpCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Error"), errorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("EvalError"), evalErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("RangeError"), rangeErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("ReferenceError"), referenceErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("SyntaxError"), syntaxErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("TypeError"), typeErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("URIError"), uRIErrorCtor);
    glo->defineDefaultProperty(rootContext, QStringLiteral("Math"), Value::fromObject(newMathObject(rootContext)));

    glo->defineReadonlyProperty(this, QStringLiteral("undefined"), Value::undefinedValue());
    glo->defineReadonlyProperty(this, QStringLiteral("NaN"), Value::fromDouble(nan("")));
    glo->defineReadonlyProperty(this, QStringLiteral("Infinity"), Value::fromDouble(INFINITY));

    glo->defineDefaultProperty(rootContext, QStringLiteral("eval"), Value::fromObject(new (memoryManager) EvalFunction(rootContext)));

    // TODO: parseInt [15.1.2.2]
    // TODO: parseFloat [15.1.2.3]
    glo->defineDefaultProperty(rootContext, QStringLiteral("isNaN"), Value::fromObject(new (memoryManager) IsNaNFunction(rootContext)));
    glo->defineDefaultProperty(rootContext, QStringLiteral("isFinite"), Value::fromObject(new (memoryManager) IsFiniteFunction(rootContext)));
}

ExecutionEngine::~ExecutionEngine()
{
    delete globalObject.asObject();
    delete rootContext;
    qDeleteAll(functions);
}

ExecutionContext *ExecutionEngine::newContext()
{
    return new ExecutionContext();
}

String *ExecutionEngine::identifier(const QString &s)
{
    String *&id = identifiers[s];
    if (! id)
        id = newString(s);
    return id;
}

Function *ExecutionEngine::newFunction(const QString &name)
{
    VM::Function *f = new VM::Function(name);
    functions.append(f);
    return f;
}

FunctionObject *ExecutionEngine::newNativeFunction(ExecutionContext *scope, String *name, Value (*code)(ExecutionContext *))
{
    NativeFunction *f = new (memoryManager) NativeFunction(scope, name, code);
    f->prototype = scope->engine->functionPrototype;
    return f;
}

FunctionObject *ExecutionEngine::newScriptFunction(ExecutionContext *scope, VM::Function *function)
{
    assert(function);

    MemoryManager::GCBlocker gcBlocker(memoryManager);

    ScriptFunction *f = new (memoryManager) ScriptFunction(scope, function);
    Object *proto = scope->engine->newObject();
    proto->__put__(scope, scope->engine->id_constructor, Value::fromObject(f));
    f->__put__(scope, scope->engine->id_prototype, Value::fromObject(proto));
    f->prototype = scope->engine->functionPrototype;
    return f;
}

Object *ExecutionEngine::newObject()
{
    Object *object = new (memoryManager) Object();
    object->prototype = objectPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newObjectCtor(ExecutionContext *ctx)
{
    return new (memoryManager) ObjectCtor(ctx);
}

String *ExecutionEngine::newString(const QString &s)
{
    return stringPool->newString(s);
}

Object *ExecutionEngine::newStringObject(const Value &value)
{
    StringObject *object = new (memoryManager) StringObject(value);
    object->prototype = stringPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newStringCtor(ExecutionContext *ctx)
{
    return new (memoryManager) StringCtor(ctx);
}

Object *ExecutionEngine::newNumberObject(const Value &value)
{
    NumberObject *object = new (memoryManager) NumberObject(value);
    object->prototype = numberPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newNumberCtor(ExecutionContext *ctx)
{
    return new (memoryManager) NumberCtor(ctx);
}

Object *ExecutionEngine::newBooleanObject(const Value &value)
{
    Object *object = new (memoryManager) BooleanObject(value);
    object->prototype = booleanPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newBooleanCtor(ExecutionContext *ctx)
{
    return new (memoryManager) BooleanCtor(ctx);
}

Object *ExecutionEngine::newFunctionObject(ExecutionContext *ctx)
{
    Object *object = new (memoryManager) FunctionObject(ctx);
    object->prototype = functionPrototype;
    return object;
}

ArrayObject *ExecutionEngine::newArrayObject()
{
    ArrayObject *object = new (memoryManager) ArrayObject();
    object->prototype = arrayPrototype;
    return object;
}

ArrayObject *ExecutionEngine::newArrayObject(const Array &value)
{
    ArrayObject *object = new (memoryManager) ArrayObject(value);
    object->prototype = arrayPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newArrayCtor(ExecutionContext *ctx)
{
    return new (memoryManager) ArrayCtor(ctx);
}

Object *ExecutionEngine::newDateObject(const Value &value)
{
    Object *object = new (memoryManager) DateObject(value);
    object->prototype = datePrototype;
    return object;
}

FunctionObject *ExecutionEngine::newDateCtor(ExecutionContext *ctx)
{
    return new (memoryManager) DateCtor(ctx);
}

RegExpObject *ExecutionEngine::newRegExpObject(const QString &pattern, int flags)
{
    bool global = (flags & IR::RegExp::RegExp_Global);
    QRegularExpression::PatternOptions options = 0;
    if (flags & IR::RegExp::RegExp_IgnoreCase)
        options |= QRegularExpression::CaseInsensitiveOption;
    if (flags & IR::RegExp::RegExp_Multiline)
        options |= QRegularExpression::MultilineOption;

    QRegularExpression re(pattern, options);
    return newRegExpObject(re, global);
}

RegExpObject *ExecutionEngine::newRegExpObject(const QRegularExpression &re, bool global)
{
    RegExpObject *object = new (memoryManager) RegExpObject(re, global);
    object->prototype = regExpPrototype;
    return object;
}

FunctionObject *ExecutionEngine::newRegExpCtor(ExecutionContext *ctx)
{
    return new (memoryManager) RegExpCtor(ctx);
}

Object *ExecutionEngine::newErrorObject(const Value &value)
{
    ErrorObject *object = new (memoryManager) ErrorObject(value);
    object->prototype = errorPrototype;
    return object;
}

Object *ExecutionEngine::newSyntaxErrorObject(ExecutionContext *ctx, DiagnosticMessage *message)
{
    SyntaxErrorObject *object = new (memoryManager) SyntaxErrorObject(ctx, message);
    object->prototype = syntaxErrorPrototype;
    return object;
}

Object *ExecutionEngine::newReferenceErrorObject(ExecutionContext *ctx, const QString &message)
{
    ReferenceErrorObject *object = new (memoryManager) ReferenceErrorObject(ctx, message);
    object->prototype = referenceErrorPrototype;
    return object;
}

Object *ExecutionEngine::newTypeErrorObject(ExecutionContext *ctx, const QString &message)
{
    TypeErrorObject *object = new (memoryManager) TypeErrorObject(ctx, message);
    object->prototype = typeErrorPrototype;
    return object;
}

Object *ExecutionEngine::newMathObject(ExecutionContext *ctx)
{
    MathObject *object = new (memoryManager) MathObject(ctx);
    object->prototype = objectPrototype;
    return object;
}

Object *ExecutionEngine::newActivationObject()
{
    return new (memoryManager) Object();
}

Object *ExecutionEngine::newForEachIteratorObject(Object *o)
{
    return new (memoryManager) ForEachIteratorObject(o);
}

} // namespace VM
} // namespace QQmlJS
