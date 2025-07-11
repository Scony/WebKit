/*
 * Copyright (C) 2013-2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#if ENABLE(DFG_JIT)

#include "DFGGraph.h"

namespace JSC { namespace DFG {

// This phase is used to determine if a node can safely run at a new location.
// It is important to note that returning false does not mean it's definitely 
// wrong to run the node at the new location. In other words, returning false 
// does not imply moving the node would be invalid only that this phase could
// not prove it is valid. Thus, it is always ok to return false.

template<typename AbstractStateType>
class SafeToExecuteEdge {
public:
    SafeToExecuteEdge(AbstractStateType& state)
        : m_state(state)
    {
    }
    
    void operator()(Node*, Edge edge)
    {
        m_maySeeEmptyChild |= !!(m_state.forNode(edge).m_type & SpecEmpty);

        switch (edge.useKind()) {
        case UntypedUse:
        case Int32Use:
        case DoubleRepUse:
        case DoubleRepRealUse:
        case Int52RepUse:
        case NumberUse:
        case RealNumberUse:
        case BooleanUse:
        case CellUse:
        case CellOrOtherUse:
        case ObjectUse:
        case ArrayUse:
        case FunctionUse:
        case FinalObjectUse:
        case RegExpObjectUse:
        case PromiseObjectUse:
        case ProxyObjectUse:
        case GlobalProxyUse:
        case DerivedArrayUse:
        case DateObjectUse:
        case MapObjectUse:
        case SetObjectUse:
        case MapIteratorObjectUse:
        case SetIteratorObjectUse:
        case WeakMapObjectUse:
        case WeakSetObjectUse:
        case DataViewObjectUse:
        case ObjectOrOtherUse:
        case StringIdentUse:
        case StringUse:
        case StringOrOtherUse:
        case SymbolUse:
        case AnyBigIntUse:
        case HeapBigIntUse:
        case BigInt32Use:
        case StringObjectUse:
        case StringOrStringObjectUse:
        case NotStringVarUse:
        case NotSymbolUse:
        case NotCellUse:
        case NotCellNorBigIntUse:
        case OtherUse:
        case MiscUse:
        case AnyIntUse:
        case DoubleRepAnyIntUse:
        case NotDoubleUse:
        case NeitherDoubleNorHeapBigIntNorStringUse:
        case NeitherDoubleNorHeapBigIntUse:
            return;
            
        case KnownInt32Use:
            if (m_state.forNode(edge).m_type & ~SpecInt32Only)
                m_result = false;
            return;

        case KnownBooleanUse:
            if (m_state.forNode(edge).m_type & ~SpecBoolean)
                m_result = false;
            return;
            
        case KnownCellUse:
            if (m_state.forNode(edge).m_type & ~SpecCell)
                m_result = false;
            return;
            
        case KnownStringUse:
            if (m_state.forNode(edge).m_type & ~SpecString)
                m_result = false;
            return;

        case KnownPrimitiveUse:
            if (m_state.forNode(edge).m_type & ~(SpecHeapTop & ~SpecObject))
                m_result = false;
            return;

        case KnownOtherUse:
            if (m_state.forNode(edge).m_type & ~SpecOther)
                m_result = false;
            return;
            
        case LastUseKind:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }
    
    bool result() const { return m_result; }
    bool maySeeEmptyChild() const { return m_maySeeEmptyChild; }
private:
    AbstractStateType& m_state;
    bool m_result { true };
    bool m_maySeeEmptyChild { false };
};

// Determines if it's safe to execute a node within the given abstract state. This may
// return false conservatively. If it returns true, then you can hoist the given node
// up to the given point and expect that it will not crash. It also guarantees that the
// node will not produce a malformed JSValue or object pointer when executed in the
// given state. But this doesn't guarantee that the node will produce the result you
// wanted. For example, you may have a GetByOffset from a prototype that only makes
// semantic sense if you've also checked that some nearer prototype doesn't also have
// a property of the same name. This could still return true even if that check hadn't
// been performed in the given abstract state. That's fine though: the load can still
// safely execute before that check, so long as that check continues to guard any
// user-observable things done to the loaded value.
template<typename AbstractStateType>
bool safeToExecute(AbstractStateType& state, Graph& graph, Node* node, bool ignoreEmptyChildren = false)
{
    SafeToExecuteEdge<AbstractStateType> safeToExecuteEdge(state);
    DFG_NODE_DO_TO_CHILDREN(graph, node, safeToExecuteEdge);
    if (!safeToExecuteEdge.result())
        return false;

    if (!ignoreEmptyChildren && safeToExecuteEdge.maySeeEmptyChild()) {
        // We conservatively assume if the empty value flows into a node,
        // it might not be able to handle it (e.g, crash). In general, the bytecode generator
        // emits code in such a way that most node types don't need to worry about the empty value
        // because they will never see it. However, code motion has to consider the empty
        // value so it does not insert/move nodes to a place where they will crash. E.g, the
        // type check hoisting phase needs to insert CheckStructureOrEmpty instead of CheckStructure
        // for hoisted structure checks because it can not guarantee that a particular local is not
        // the empty value.
        switch (node->op()) {
        case CheckNotEmpty:
        case CheckStructureOrEmpty:
        case CheckArrayOrEmpty:
            break;
        default:
            return false;
        }
    }

    // NOTE: This can lie when it comes to effectful nodes, because it knows that they aren't going to
    // get hoisted anyway. Sometimes this is convenient so we can avoid branching on some internal
    // state of the node (like what some child's UseKind might be). However, nodes that are obviously
    // always effectful, we return false for, to make auditing the "return true" cases easier.

    switch (node->op()) {
    case JSConstant:
    case DoubleConstant:
    case Int52Constant:
    case LazyJSConstant:
    case Identity:
    case IdentityWithProfile:
    case GetCallee:
    case GetArgumentCountIncludingThis:
    case GetRestLength:
    case GetLocal:
    case GetStack:
    case ExitOK:
    case Phantom:
    case ArithBitNot:
    case ArithBitAnd:
    case ArithBitOr:
    case ArithBitXor:
    case ArithBitLShift:
    case ArithBitRShift:
    case ArithBitURShift:
    case ValueToInt32:
    case UInt32ToNumber:
    case DoubleAsInt32:
    case ArithAdd:
    case ArithClz32:
    case ArithSub:
    case ArithNegate:
    case ArithMul:
    case ArithDiv:
    case ArithMod:
    case ArithAbs:
    case ArithMin:
    case ArithMax:
    case ArithPow:
    case ArithSqrt:
    case ArithFRound:
    case ArithF16Round:
    case ArithRound:
    case ArithFloor:
    case ArithCeil:
    case ArithTrunc:
    case ArithUnary:
    case CheckStructure:
    case CheckStructureOrEmpty:
    case GetExecutable:
    case CheckJSCast:
    case CheckNotJSCast:
    case CheckArray:
    case CheckArrayOrEmpty:
    case GetScope:
    case GetEvalScope:
    case SkipScope:
    case GetGlobalObject:
    case GetGlobalThis:
    case UnwrapGlobalProxy:
    case GetClosureVar:
    case GetGlobalVar:
    case GetGlobalLexicalVariable:
    case CheckIsConstant:
    case CheckNotEmpty:
    case AssertNotEmpty:
    case CheckIdent:
    case CompareLess:
    case CompareLessEq:
    case CompareGreater:
    case CompareGreaterEq:
    case CompareBelow:
    case CompareBelowEq:
    case CompareEq:
    case CompareStrictEq:
    case CompareEqPtr:
    case SameValue:
    case CheckTypeInfoFlags:
    case ParseInt:
    case ToIntegerOrInfinity:
    case ToLength:
    case OverridesHasInstance:
    case IsEmpty:
    case IsEmptyStorage:
    case TypeOfIsUndefined:
    case TypeOfIsObject:
    case TypeOfIsFunction:
    case IsUndefinedOrNull:
    case IsBoolean:
    case IsNumber:
    case IsBigInt:
    case NumberIsInteger:
    case IsObject:
    case IsCallable:
    case IsConstructor:
    case IsCellWithType:
    case IsTypedArrayView:
    case HasStructureWithFlags:
    case TypeOf:
    case ToBoolean:
    case LogicalNot:
    case ToString:
    case FunctionToString:
    case NumberToStringWithValidRadixConstant:
    case StrCat:
    case CallStringConstructor:
    case MakeRope:
    case MakeAtomString:
    case GetFromArguments:
    case GetArgument:
    case StringFromCharCode:
    case ExtractOSREntryLocal:
    case ExtractCatchLocal:
    case AssertInBounds:
    case CheckInBounds:
    case CheckInBoundsInt52:
    case ConstantStoragePointer:
    case Check:
    case CheckVarargs:
    case ValueRep:
    case DoubleRep:
    case PurifyNaN:
    case Int52Rep:
    case BooleanToNumber:
    case FiatInt52:
    case HasIndexedProperty:
    case CheckStructureImmediate:
    case GetMyArgumentByVal:
    case GetMyArgumentByValOutOfBounds:
    case GetPrototypeOf:
    case GetRegExpObjectLastIndex:
    case MapHash:
    case NormalizeMapKey:
    case StringSlice:
    case StringSubstring:
    case ToLowerCase:
    case MapGet:
    case LoadMapValue:
    case MapStorage:
    case MapStorageOrSentinel:
    case MapIterationNext:
    case MapIterationEntry:
    case MapIterationEntryKey:
    case MapIterationEntryValue:
    case MapIteratorNext:
    case MapIteratorKey:
    case MapIteratorValue:
    case ExtractValueFromWeakMapGet:
    case WeakMapGet:
    case AtomicsIsLockFree:
    case MatchStructure:
    case DateGetInt32OrNaN:
    case DateGetTime:
    case DataViewGetInt:
    case DataViewGetFloat:
    case ResolveRope:
    case NumberIsNaN:
    case NumberIsFinite:
    case NumberIsSafeInteger:
    case StringIndexOf:
        return true;

    case GlobalIsFinite:
    case GlobalIsNaN:
        return false;

    case GetButterfly:
        return state.forNode(node->child1()).isType(SpecObject);

    case ArraySlice:
    case ArrayIncludes:
    case ArrayIndexOf: {
        // You could plausibly move this code around as long as you proved the
        // incoming array base structure is an original array at the hoisted location.
        // Instead of doing that extra work, we just conservatively return false.
        return false;
    }

    case GetGetter:
    case GetSetter: {
        if (!state.forNode(node->child1()).isType(SpecCell))
            return false;
        StructureAbstractValue& value = state.forNode(node->child1()).m_structure;
        if (value.isInfinite() || value.size() != 1)
            return false;

        return value[0].get() == graph.m_vm.getterSetterStructure.get();
    }

    case BottomValue:
        // If in doubt, assume that this isn't safe to execute, just because we have no way of
        // compiling this node.
        return false;

    case StoreBarrier:
    case FencedStoreBarrier:
    case PutStructure:
    case NukeStructureAndSetButterfly:
        // We conservatively assume that these cannot be put anywhere, which forces the compiler to
        // keep them exactly where they were. This is sort of overkill since the clobberize effects
        // already force these things to be ordered precisely. I'm just not confident enough in my
        // effect based memory model to rely solely on that right now.
        return false;
        
    case FilterCallLinkStatus:
    case FilterGetByStatus:
    case FilterPutByStatus:
    case FilterInByStatus:
    case FilterDeleteByStatus:
    case FilterCheckPrivateBrandStatus:
    case FilterSetPrivateBrandStatus:
    case EnumeratorPutByVal:
        // We don't want these to be moved anywhere other than where we put them, since we want them
        // to capture "profiling" at the point in control flow here the user put them.
        return false;

    case EnumeratorGetByVal:
    case GetByVal:
    case GetByValMegamorphic:
    case GetIndexedPropertyStorage:
    case GetArrayLength:
    case GetUndetachedTypeArrayLength:
    case GetTypedArrayLengthAsInt52:
    case GetVectorLength:
    case ArrayPop:
    case StringAt:
    case StringCharAt:
    case StringCharCodeAt:
    case StringCodePointAt:
        return node->arrayMode().alreadyChecked(graph, node, state.forNode(graph.child(node, 0)));

    // We can make them non conservative by checking the condition safely.
    case MultiGetByVal:
    case MultiPutByVal:
        return false;

    case ArrayPush:
        return node->arrayMode().alreadyChecked(graph, node, state.forNode(graph.varArgChild(node, 1)));

    case DataViewGetByteLength:
    case DataViewGetByteLengthAsInt52:
        return !(state.forNode(node->child1()).m_type & ~(SpecDataViewObject));

    case CheckDetached:
        return !(state.forNode(node->child1()).m_type & ~(SpecTypedArrayView | SpecDataViewObject));

    case GetTypedArrayByteOffset:
    case GetTypedArrayByteOffsetAsInt52:
        return !(state.forNode(node->child1()).m_type & ~(SpecTypedArrayView));

    case PutByValDirect:
    case PutByVal:
    case PutByValAlias:
    case PutByValMegamorphic:
        return node->arrayMode().modeForPut().alreadyChecked(
            graph, node, state.forNode(graph.varArgChild(node, 0)));

    case AllocatePropertyStorage:
    case ReallocatePropertyStorage:
        return state.forNode(node->child1()).m_structure.isSubsetOf(
            RegisteredStructureSet(node->transition()->previous));
        
    case GetGetterSetterByOffset: {
        // If it's an inline property, we need to make sure it's a cell before trusting what the structure set tells us.
        if (node->child1().node() == node->child2().node() && !state.forNode(node->child2()).isType(SpecCell))
            return false;

        StorageAccessData& data = node->storageAccessData();
        auto* uid = graph.identifiers()[data.identifierNumber];
        PropertyOffset desiredOffset = data.offset;
        StructureAbstractValue& value = state.forNode(node->child2()).m_structure;
        if (value.isInfinite())
            return false;
        for (unsigned i = value.size(); i--;) {
            Structure* thisStructure = value[i].get();
            if (thisStructure->isUncacheableDictionary())
                return false;
            unsigned attributes = 0;
            PropertyOffset checkOffset = thisStructure->getConcurrently(uid, attributes);
            if (checkOffset != desiredOffset || !(attributes & PropertyAttribute::Accessor))
                return false;
        }
        return true;
    }

    case GetWebAssemblyInstanceExports: {
        if (!state.forNode(node->child1()).isType(SpecCell))
            return false;

        StructureAbstractValue& value = state.forNode(node->child1()).m_structure;
        if (value.isInfinite())
            return false;
        for (unsigned i = value.size(); i--;) {
            Structure* structure = value[i].get();
            if (structure->typeInfo().type() != WebAssemblyInstanceType)
                return false;
        }

        return true;
    }

    case GetByOffset:
    case PutByOffset: {
        // If it's an inline property, we need to make sure it's a cell before trusting what the structure set tells us.
        if (node->child1().node() == node->child2().node() && !state.forNode(node->child2()).isType(SpecCell))
            return false;

        StorageAccessData& data = node->storageAccessData();
        PropertyOffset offset = data.offset;
        // Graph::isSafeToLoad() is all about proofs derived from PropertyConditions. Those don't
        // know anything about inferred types. But if we have a proof derived from watching a
        // structure that has a type proof, then the next case below will deal with it.
        if (state.structureClobberState() == StructuresAreWatched) {
            if (JSObject* knownBase = node->child2()->dynamicCastConstant<JSObject*>()) {
                if (graph.isSafeToLoad(knownBase, offset))
                    return true;
            }
        }
        
        StructureAbstractValue& value = state.forNode(node->child2()).m_structure;
        if (value.isInfinite())
            return false;
        for (unsigned i = value.size(); i--;) {
            Structure* thisStructure = value[i].get();
            if (thisStructure->isUncacheableDictionary())
                return false;
            if (!thisStructure->isValidOffset(offset))
                return false;
        }
        return true;
    }
        
    case MultiGetByOffset: {
        // We can't always guarantee that the MultiGetByOffset is safe to execute if it
        // contains loads from prototypes. If the load requires a check in IR, which is rare, then
        // we currently claim that we don't know if it's safe to execute because finding that
        // check in the abstract state would be hard. If the load requires watchpoints, we just
        // check if we're not in a clobbered state (i.e. in between a side effect and an
        // invalidation point).
        for (const MultiGetByOffsetCase& getCase : node->multiGetByOffsetData().cases) {
            GetByOffsetMethod method = getCase.method();
            switch (method.kind()) {
            case GetByOffsetMethod::Invalid:
                RELEASE_ASSERT_NOT_REACHED();
                break;
            case GetByOffsetMethod::Constant: // OK because constants are always safe to execute.
            case GetByOffsetMethod::Load: // OK because the MultiGetByOffset has its own checks for loading from self.
                break;
            case GetByOffsetMethod::LoadFromPrototype:
                // Only OK if the state isn't clobbered. That's almost always the case.
                if (state.structureClobberState() != StructuresAreWatched)
                    return false;
                if (!graph.isSafeToLoad(method.prototype()->cast<JSObject*>(), method.offset()))
                    return false;
                break;
            }
        }
        return true;
    }

    case CallDOMGetter:
    case CallDOM: {
        Node* thisNode = node->child1().node();
        StructureAbstractValue& structures = state.forNode(thisNode).m_structure;
        if (!structures.isFinite())
            return false;
        bool isSafe = true;
        const ClassInfo* classInfo = node->requiredDOMJITClassInfo();
        structures.forEach([&] (RegisteredStructure structure) {
            isSafe &= structure->classInfoForCells()->isSubClassOf(classInfo);
        });
        return isSafe;
    }

    case EnumeratorNextUpdateIndexAndMode:
    case ExtractFromTuple:
    case EnumeratorNextUpdatePropertyName:
    case ToThis:
    case CreateThis:
    case CreatePromise:
    case CreateGenerator:
    case CreateAsyncGenerator:
    case ObjectAssign:
    case ObjectCreate:
    case ObjectKeys:
    case ObjectGetOwnPropertyNames:
    case ObjectGetOwnPropertySymbols:
    case ObjectToString:
    case ReflectOwnKeys:
    case SetLocal:
    case SetCallee:
    case PutStack:
    case KillStack:
    case MovHint:
    case ZombieHint:
    case Upsilon:
    case Phi:
    case Flush:
    case SetArgumentDefinitely:
    case SetArgumentMaybe:
    case SetArgumentCountIncludingThis:
    case PhantomLocal:
    case DeleteById:
    case DeleteByVal:
    case GetById:
    case GetByIdMegamorphic:
    case GetByIdWithThis:
    case GetByIdWithThisMegamorphic:
    case GetByValWithThis:
    case GetByValWithThisMegamorphic:
    case GetByIdFlush:
    case GetByIdDirect:
    case GetByIdDirectFlush:
    case PutById:
    case PutByIdFlush:
    case PutByIdMegamorphic:
    case PutByIdWithThis:
    case PutByValWithThis:
    case PutByIdDirect:
    case PutGetterById:
    case PutSetterById:
    case PutGetterSetterById:
    case PutGetterByVal:
    case PutSetterByVal:
    case PutPrivateName:
    case PutPrivateNameById:
    case GetPrivateName:
    case GetPrivateNameById:
    case CheckPrivateBrand:
    case SetPrivateBrand:
    case DefineDataProperty:
    case DefineAccessorProperty:
    case Arrayify:
    case ArrayifyToStructure:
    case PutClosureVar:
    case PutGlobalVariable:
    case CheckBadValue:
    case RegExpExec:
    case RegExpExecNonGlobalOrSticky:
    case RegExpTest:
    case RegExpTestInline:
    case RegExpMatchFast:
    case RegExpMatchFastGlobal:
    case RegExpSearch:
    case Call:
    case DirectCall:
    case TailCallInlinedCaller:
    case DirectTailCallInlinedCaller:
    case Construct:
    case DirectConstruct:
    case CallVarargs:
    case CallDirectEval:
    case TailCallVarargsInlinedCaller:
    case TailCallForwardVarargsInlinedCaller:
    case ConstructVarargs:
    case CallWasm:
    case CallCustomAccessorGetter:
    case CallCustomAccessorSetter:
    case VarargsLength:
    case LoadVarargs:
    case CallForwardVarargs:
    case ConstructForwardVarargs:
    case NewObject:
    case NewGenerator:
    case NewAsyncGenerator:
    case NewArray:
    case NewArrayWithSize:
    case NewArrayWithConstantSize:
    case NewArrayWithSpecies:
    case NewArrayWithSizeAndStructure:
    case NewArrayBuffer:
    case NewArrayWithSpread:
    case NewInternalFieldObject:
    case Spread:
    case NewRegExp:
    case NewMap:
    case NewSet:
    case NewSymbol:
    case ProfileType:
    case ProfileControlFlow:
    case InstanceOf:
    case InstanceOfMegamorphic:
    case InstanceOfCustom:
    case CallObjectConstructor:
    case ToPrimitive:
    case ToPropertyKey:
    case ToPropertyKeyOrNumber:
    case ToNumber:
    case ToNumeric:
    case ToObject:
    case CallNumberConstructor:
    case NumberToStringWithRadix:
    case SetFunctionName:
    case NewStringObject:
    case NewRegExpUntyped:
    case InByVal:
    case InByValMegamorphic:
    case InById:
    case InByIdMegamorphic:
    case EnumeratorInByVal:
    case EnumeratorHasOwnProperty:
    case HasPrivateName:
    case HasPrivateBrand:
    case HasOwnProperty:
    case PushWithScope:
    case CreateActivation:
    case CreateDirectArguments:
    case CreateScopedArguments:
    case CreateClonedArguments:
    case PutToArguments:
    case NewFunction:
    case NewGeneratorFunction:
    case NewAsyncGeneratorFunction:
    case NewAsyncFunction:
    case NewBoundFunction:
    case Jump:
    case Branch:
    case Switch:
    case EntrySwitch:
    case Return:
    case TailCall:
    case DirectTailCall:
    case TailCallVarargs:
    case TailCallForwardVarargs:
    case Throw:
    case ThrowStaticError:
    case CountExecution:
    case SuperSamplerBegin:
    case SuperSamplerEnd:
    case ForceOSRExit:
    case CPUIntrinsic:
    case CheckTraps:
    case LogShadowChickenPrologue:
    case LogShadowChickenTail:
    case NewTypedArray:
    case NewTypedArrayBuffer:
    case Unreachable:
    case ClearCatchLocals:
    case CheckTierUpInLoop:
    case CheckTierUpAtReturn:
    case CheckTierUpAndOSREnter:
    case LoopHint:
    case InvalidationPoint:
    case NotifyWrite:
    case MultiPutByOffset:
    case MultiDeleteByOffset:
    case GetPropertyEnumerator:
    case PhantomNewObject:
    case PhantomNewArrayWithConstantSize:
    case PhantomNewFunction:
    case PhantomNewGeneratorFunction:
    case PhantomNewAsyncGeneratorFunction:
    case PhantomNewAsyncFunction:
    case PhantomNewInternalFieldObject:
    case PhantomCreateActivation:
    case PhantomNewRegExp:
    case PutHint:
    case MaterializeNewObject:
    case MaterializeNewArrayWithConstantSize:
    case MaterializeCreateActivation:
    case MaterializeNewInternalFieldObject:
    case PhantomDirectArguments:
    case PhantomCreateRest:
    case PhantomSpread:
    case PhantomNewArrayWithSpread:
    case PhantomNewArrayBuffer:
    case PhantomClonedArguments:
    case ForwardVarargs:
    case CreateRest:
    case SetRegExpObjectLastIndex:
    case RecordRegExpCachedResult:
    case GetDynamicVar:
    case PutDynamicVar:
    case ResolveScopeForHoistingFuncDeclInEval:
    case ResolveScope:
    case StringValueOf:
    case WeakSetAdd:
    case WeakMapSet:
    case AtomicsAdd:
    case AtomicsAnd:
    case AtomicsCompareExchange:
    case AtomicsExchange:
    case AtomicsLoad:
    case AtomicsOr:
    case AtomicsStore:
    case AtomicsSub:
    case AtomicsXor:
    case InitializeEntrypointArguments:
    case ValueNegate:
    case GetInternalField:
    case PutInternalField:
    case DataViewSet:
    case SetAdd:
    case MapSet:
    case MapOrSetDelete:
    case StringReplace:
    case StringReplaceAll:
    case StringReplaceRegExp:
    case ArithRandom:
    case ArithIMul:
    case TryGetById:
    case StringLocaleCompare:
    case FunctionBind:
    case DateSetTime:
    case ArraySplice:
        return false;

    case StringReplaceString:
        return node->child3().useKind() == StringUse;

    case Inc:
    case Dec:
        return node->child1().useKind() != UntypedUse;

    case ValueBitAnd:
    case ValueBitXor:
    case ValueBitOr:
    case ValueBitLShift:
    case ValueBitRShift:
    case ValueBitURShift:
    case ValueAdd:
    case ValueSub:
    case ValueMul:
    case ValueDiv:
    case ValueMod:
    case ValuePow:
        return node->isBinaryUseKind(AnyBigIntUse) || node->isBinaryUseKind(BigInt32Use) || node->isBinaryUseKind(HeapBigIntUse);

    case ValueBitNot:
        return node->child1().useKind() == AnyBigIntUse || node->child1().useKind() == BigInt32Use || node->child1().useKind() == HeapBigIntUse;

    case LastNodeType:
        RELEASE_ASSERT_NOT_REACHED();
        return false;
    }
    
    RELEASE_ASSERT_NOT_REACHED();
    return false;
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)
