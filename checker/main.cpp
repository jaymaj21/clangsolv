// ClangSOLV's checking engine. See DESIGN.md for the supported semantic model.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace clang;
using Labels = std::set<std::string>;
using State = std::map<const VarDecl*, Labels>;
using MaybeState = std::optional<State>;

static llvm::cl::OptionCategory Category("ClangSOLV options");
static llvm::cl::opt<std::string> ReportPath("report", llvm::cl::desc("Write a JSON check report"), llvm::cl::cat(Category));
static llvm::cl::extrahelp Help(clang::tooling::CommonOptionsParser::HelpMessage);

static Labels intersect(const Labels& left, const Labels& right) {
    Labels result;
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(), std::inserter(result,result.end()));
    return result;
}
static Labels unite(Labels left, const Labels& right) {
    left.insert(right.begin(),right.end());
    return left;
}
static Labels difference(const Labels& left, const Labels& right) {
    Labels result;
    std::set_difference(left.begin(),left.end(),right.begin(),right.end(),std::inserter(result,result.end()));
    return result;
}
static State meet(const State& left, const State& right) {
    State result;
    for (const auto& entry : left) {
        auto found=right.find(entry.first);
        if (found!=right.end()) {
            auto common=intersect(entry.second,found->second);
            if (!common.empty()) result[entry.first]=std::move(common);
        }
    }
    return result;
}
static MaybeState join(const MaybeState& left, const MaybeState& right) {
    if (!left) return right;
    if (!right) return left;
    return meet(*left,*right);
}
static std::string display(const Labels& labels) {
    std::string text;
    for (const auto& label : labels) { if (!text.empty()) text += ", "; text += label; }
    return text.empty() ? "(none)" : text;
}
static llvm::json::Array jsonLabels(const Labels& labels) {
    llvm::json::Array result;
    for (const auto& label : labels) result.push_back(label);
    return result;
}

struct Report {
    unsigned functions=0, cfgBlocks=0, errors=0;
    llvm::json::Array requirements, diagnostics;
};
struct Contract {
    Labels returns;
    std::vector<Labels> parameters;
    unsigned preserves=0;
    bool readonly=false;
    std::string marker;
};
struct Flow {
    MaybeState normal, breaks, continues;
};

class Engine : public RecursiveASTVisitor<Engine> {
    ASTContext& context;
    DiagnosticsEngine& diagnostics;
    SourceManager& sources;
    Report& report;
    bool collectingLabels=true;
    unsigned silent=0;
    Labels vocabulary;
    std::map<const FunctionDecl*,Contract> contracts;
    std::vector<FunctionDecl*> bodies;
    std::set<const FunctionDecl*> seenBodies;
    std::map<const VarDecl*,const VarDecl*> aliases;
    std::set<std::pair<unsigned,std::string>> emitted;

    llvm::json::Object location(SourceLocation loc) {
        auto presumed=sources.getPresumedLoc(sources.getExpansionLoc(loc));
        if (presumed.isInvalid()) return llvm::json::Object{};
        return llvm::json::Object{{"file",presumed.getFilename()}, {"line",static_cast<int64_t>(presumed.getLine())},
                {"column",static_cast<int64_t>(presumed.getColumn())}};
    }
    void error(SourceLocation loc, const std::string& message) {
        if (silent) return;
        loc=sources.getExpansionLoc(loc);
        if (!emitted.insert({loc.getRawEncoding(),message}).second) return;
        auto id=diagnostics.getCustomDiagID(DiagnosticsEngine::Error,"ClangSOLV: %0");
        diagnostics.Report(loc,id) << message;
        auto item=location(loc); item["message"]=message;
        report.diagnostics.push_back(std::move(item)); ++report.errors;
    }
    Labels resolve(llvm::StringRef text, SourceLocation loc) {
        Labels result;
        llvm::SmallVector<llvm::StringRef,8> words;
        text.split(words,',',-1,true);
        if (words.empty()) { error(loc,"empty label list"); return result; }
        for (auto word : words) {
            word=word.trim();
            if (word.starts_with("::")) word=word.drop_front(2);
            Labels candidates;
            for (const auto& name : vocabulary) {
                auto shortName=llvm::StringRef(name).rsplit("::").second;
                if (word==name || (word.find("::")==llvm::StringRef::npos && word==shortName)) candidates.insert(name);
            }
            if (candidates.size()!=1) error(loc,(candidates.empty() ? "unknown label: " : "ambiguous label; qualify its namespace: ")+word.str());
            else result.insert(*candidates.begin());
        }
        return result;
    }
    bool userLocation(SourceLocation loc) const {
        return loc.isValid() && !sources.isInSystemHeader(sources.getExpansionLoc(loc));
    }
    bool hasAnnotation(const Decl* decl, llvm::StringRef value) const {
        for (auto* attr : decl->specific_attrs<AnnotateAttr>()) if (attr->getAnnotation()==value) return true;
        return false;
    }
    // Recognise semantic standard-library specialisations, including aliases.
    std::string smartKind(QualType type) const {
        auto* record=type.getNonReferenceType()->getAsCXXRecordDecl();
        auto* specialised=dyn_cast_or_null<ClassTemplateSpecializationDecl>(record);
        if (!specialised || !specialised->isInStdNamespace()) return {};
        auto name=specialised->getSpecializedTemplate()->getNameAsString();
        return name=="shared_ptr" || name=="unique_ptr" ? name : std::string();
    }
    QualType domain(QualType type) const {
        type=type.getNonReferenceType().getUnqualifiedType();
        if (smartKind(type).empty()) return type;
        auto* record=cast<ClassTemplateSpecializationDecl>(type->getAsCXXRecordDecl());
        return record->getTemplateArgs().get(0).getAsType().getUnqualifiedType();
    }
    void checkSmartType(QualType type,SourceLocation loc) {
        auto kind=smartKind(type);
        if (kind.empty()) return;
        auto* record=cast<ClassTemplateSpecializationDecl>(type.getNonReferenceType()->getAsCXXRecordDecl());
        auto pointee=domain(type);
        if (pointee->isArrayType() || pointee->isPointerType() || !smartKind(pointee).empty())
            error(loc,"smart-pointer labels require a single ordinary pointee; arrays and nested pointers are unsupported");
        if (kind=="unique_ptr") {
            auto* deleter=record->getTemplateArgs().get(1).getAsType()->getAsCXXRecordDecl();
            if (!deleter || !deleter->isInStdNamespace() || deleter->getNameAsString()!="default_delete")
                error(loc,"labelled unique_ptr requires std::default_delete");
        }
        checkDestruction(pointee,loc);
    }
    Contract readContract(const FunctionDecl* function) {
        Contract result;
        result.parameters.resize(function->getNumParams());
        std::vector<const FunctionDecl*> declarations;
        for (auto* decl : function->redecls()) declarations.push_back(decl);
        if (auto* primary=function->getPrimaryTemplate()) declarations.push_back(primary->getTemplatedDecl());
        for (auto* decl : declarations) {
            for (auto* attr : decl->specific_attrs<AnnotateAttr>()) {
                auto text=attr->getAnnotation();
                if (!text.starts_with("clangsolv.")) continue;
                if (text.consume_front("clangsolv.returns:")) result.returns=unite(result.returns,resolve(text,attr->getLocation()));
                else if (text.consume_front("clangsolv.preserves:")) {
                    unsigned index=1;
                    text=text.trim();
                    if ((!text.empty() && text.getAsInteger(10,index)) || index==0 || index>function->getNumParams())
                        error(attr->getLocation(),"preserves must name a valid one-based parameter index");
                    else if (result.preserves && result.preserves!=index) error(attr->getLocation(),"conflicting preservation indices");
                    else result.preserves=index;
                } else if (text=="clangsolv.readonly") result.readonly=true;
                else if (text=="clangsolv.establish" || text=="clangsolv.forget" || text=="clangsolv.require") {
                    result.marker=text.drop_front(10).str(); result.readonly=true;
                } else error(attr->getLocation(),"unsupported function annotation: "+text.str());
            }
            for (unsigned i=0;i<decl->getNumParams();++i) {
                auto* parameter=decl->getParamDecl(i);
                for (auto* attr : parameter->specific_attrs<AnnotateAttr>()) {
                    auto text=attr->getAnnotation();
                    if (!text.starts_with("clangsolv.")) continue;
                    if (text.consume_front("clangsolv.requires:")) result.parameters[i]=unite(result.parameters[i],resolve(text,attr->getLocation()));
                    else error(attr->getLocation(),"only needs is supported on parameters");
                }
            }
        }
        if (result.preserves) {
            auto source=domain(function->getParamDecl(result.preserves-1)->getType());
            auto target=domain(function->getReturnType());
            if (!source->isDependentType() && !target->isDependentType() && !context.hasSameType(source,target))
                error(function->getLocation(),"preservation requires the same domain type for source and result");
        }
        if ((!result.returns.empty() || result.preserves) && function->getReturnType().getNonReferenceType()->isPointerType())
            error(function->getLocation(),"labelled pointer results are unsupported; use a value or reference");
        for (unsigned i=0;i<result.parameters.size();++i)
            if (!result.parameters[i].empty() && function->getParamDecl(i)->getType().getNonReferenceType()->isPointerType())
                error(function->getParamDecl(i)->getLocation(),"labelled pointer parameters are unsupported; use a value or reference");
        if (!result.returns.empty() || result.preserves) checkSmartType(function->getReturnType(),function->getLocation());
        for (unsigned i=0;i<result.parameters.size();++i)
            if (!result.parameters[i].empty()) checkSmartType(function->getParamDecl(i)->getType(),function->getLocation());
        return result;
    }
    const Contract& contract(const FunctionDecl* function) {
        auto* canonical=function->getCanonicalDecl();
        auto found=contracts.find(canonical);
        if (found==contracts.end()) found=contracts.emplace(canonical,readContract(function)).first;
        return found->second;
    }
    const VarDecl* canonical(const VarDecl* variable) const {
        variable=variable->getCanonicalDecl();
        std::set<const VarDecl*> visited;
        while (aliases.count(variable) && visited.insert(variable).second) variable=aliases.at(variable);
        return variable;
    }
    const VarDecl* wholeVariable(const Expr* expression) const {
        if (!expression) return nullptr;
        expression=expression->IgnoreParenImpCasts();
        if (auto* reference=dyn_cast<DeclRefExpr>(expression))
            if (auto* variable=dyn_cast<VarDecl>(reference->getDecl())) return canonical(variable);
        return nullptr;
    }
    Labels known(const State& state,const VarDecl* variable) const {
        if (!variable) return {};
        auto found=state.find(canonical(variable));
        return found==state.end() ? Labels{} : found->second;
    }
    void set(State& state,const VarDecl* variable,Labels labels) {
        if (!variable) return;
        variable=canonical(variable);
        if (!isa<ParmVarDecl>(variable) && (!variable->isLocalVarDecl() || variable->hasGlobalStorage())) return;
        if (labels.empty()) state.erase(variable); else state[variable]=std::move(labels);
    }
    void requirement(SourceLocation loc,llvm::StringRef consumer,const Labels& needed,const Labels& available) {
        if (needed.empty() || silent) return;
        auto missing=difference(needed,available);
        auto item=location(loc);
        item["consumer"]=consumer.str(); item["required"]=jsonLabels(needed);
        item["known"]=jsonLabels(available); item["missing"]=jsonLabels(missing);
        item["accepted"]=missing.empty(); report.requirements.push_back(std::move(item));
        if (!missing.empty()) error(loc,"missing required labels for "+consumer.str()+": "+display(missing)+"; known: "+display(available));
    }
    void checkDestruction(QualType type,SourceLocation loc) {
        if (type->isReferenceType()) return;
        auto* record=context.getBaseElementType(type)->getAsCXXRecordDecl();
        auto* destructor=record && record->hasDefinition() ? record->getDefinition()->getDestructor() : nullptr;
        if (destructor && !destructor->isDefaulted() && userLocation(destructor->getLocation()))
            error(loc,"user-defined destructors are unsupported in checked value construction");
    }
    Labels markerLabels(const FunctionDecl* function,SourceLocation loc) {
        Labels labels;
        auto* arguments=function->getTemplateSpecializationArgs();
        if (!arguments || arguments->size()==0 || arguments->get(0).getKind()!=TemplateArgument::Pack) {
            error(loc,"proof marker requires explicit label template arguments"); return labels;
        }
        for (const auto& argument : arguments->get(0).pack_elements()) {
            auto* record=argument.getKind()==TemplateArgument::Type ? argument.getAsType()->getAsCXXRecordDecl() : nullptr;
            auto name=record ? record->getQualifiedNameAsString() : std::string();
            if (!vocabulary.count(name)) error(loc,"proof marker uses an unregistered label type: "+name);
            else labels.insert(name);
        }
        return labels;
    }
    Labels evaluateCall(const CallExpr* call,State& state) {
        // Observing the managed object transfers pointee evidence, never wrapper facts.
        const Expr* receiver=nullptr;
        if (auto* member=dyn_cast<CXXMemberCallExpr>(call)) receiver=member->getImplicitObjectArgument();
        if (auto* operation=dyn_cast<CXXOperatorCallExpr>(call); operation && operation->getNumArgs()) receiver=operation->getArg(0);
        auto* direct=call->getDirectCallee();
        if (receiver && !smartKind(receiver->getType()).empty() && direct) {
            checkSmartType(receiver->getType(),call->getExprLoc());
            auto labels=evaluate(receiver,state);
            auto name=direct->getNameAsString();
            if (name=="operator*" || name=="operator->") return labels;
            if (name=="operator bool" || name=="use_count" || name=="unique") return {};
            // get/release escape; reset/swap/assignment can replace or dispose objects.
            // All aliases lose evidence, including previously borrowed references.
            for (unsigned i=isa<CXXOperatorCallExpr>(call) ? 1 : 0;i<call->getNumArgs();++i) evaluate(call->getArg(i),state);
            state.clear(); return {};
        }
        if (auto* member=dyn_cast<CXXMemberCallExpr>(call)) evaluate(member->getImplicitObjectArgument(),state);
        auto* function=call->getDirectCallee();
        unsigned sideEffects=0;
        for (const auto* argument : call->arguments()) sideEffects+=argument->HasSideEffects(context);
        if (sideEffects>1) error(call->getExprLoc(),"multiple side-effecting arguments have unsupported evaluation ordering; bind arguments first");
        std::vector<Labels> arguments;
        for (const auto* argument : call->arguments()) arguments.push_back(evaluate(argument,state));
        if (!function) {
            error(call->getExprLoc(),"indirect calls are unsupported; use a direct function with an explicit contract");
            state.clear(); return {};
        }
        if (auto* method=dyn_cast<CXXMethodDecl>(function); method && method->isVirtual()) {
            error(call->getExprLoc(),"virtual dispatch is unsupported; use a non-virtual contracted boundary");
            state.clear(); return {};
        }
        const auto& rules=contract(function);
        // Operator-call ASTs include the implicit object as argument zero.
        // Contract indices always count explicit parameters.
        const unsigned offset=isa<CXXOperatorCallExpr>(call) && isa<CXXMethodDecl>(function) ? 1 : 0;
        if (!rules.marker.empty()) {
            auto labels=markerLabels(function,call->getExprLoc());
            const auto* variable=call->getNumArgs() ? wholeVariable(call->getArg(0)) : nullptr;
            if (!variable || (!isa<ParmVarDecl>(variable) && (!variable->isLocalVarDecl() || variable->hasGlobalStorage()))) {
                error(call->getExprLoc(),"proof markers require a named local value or reference parameter");
                return {};
            }
            if (variable->getType().getNonReferenceType()->isPointerType()) {
                error(call->getExprLoc(),"proof markers cannot label pointers"); return {};
            }
            checkSmartType(variable->getType(),call->getExprLoc());
            if (rules.marker=="establish") set(state,variable,unite(known(state,variable),labels));
            else if (rules.marker=="forget") {
                auto remaining=labels.empty() ? Labels{} : difference(known(state,variable),labels);
                // Revocation may concern a pointee shared by other tracked handles.
                state.clear(); set(state,variable,remaining);
            }
            else requirement(call->getExprLoc(),"clangsolv::require",labels,known(state,variable));
            return {};
        }
        bool hasRequirements=false;
        for (const auto& needed : rules.parameters) hasRequirements |= !needed.empty();
        if (call->getNumArgs()>1 && (hasRequirements || rules.preserves)) {
            for (const auto* argument : call->arguments()) if (argument->HasSideEffects(context)) {
                error(call->getExprLoc(),"side-effecting multi-argument contract calls are unsupported; bind arguments first");
                state.clear(); return {};
            }
        }
        for (unsigned i=0;i<rules.parameters.size() && i+offset<arguments.size();++i) {
            auto available=arguments[i+offset];
            // Reference arguments denote current storage, not an earlier value snapshot.
            if (function->getParamDecl(i)->getType()->isReferenceType())
                if (auto* variable=wholeVariable(call->getArg(i+offset))) available=known(state,variable);
            requirement(call->getArg(i+offset)->getExprLoc(),function->getQualifiedNameAsString(),rules.parameters[i],available);
        }
        Labels result=rules.returns;
        if (rules.preserves && rules.preserves+offset<=arguments.size()) result=unite(result,arguments[rules.preserves-1+offset]);
        if (function->isInStdNamespace() && function->getNameAsString()=="move") {
            state.clear(); return arguments.empty() ? Labels{} : arguments.front();
        }
        if (auto* operation=dyn_cast<CXXOperatorCallExpr>(call); operation && operation->getOperator()==OO_Equal) {
            state.clear();
            auto* method=dyn_cast<CXXMethodDecl>(function);
            // Defaulted value transfer retains evidence on the destination,
            // while the conservative write kill also invalidates the source.
            if (method && (method->isCopyAssignmentOperator() || method->isMoveAssignmentOperator()) && method->isDefaulted() && arguments.size()==2) {
                result=arguments[1];
                set(state,wholeVariable(call->getArg(0)),result);
            }
            return result;
        }
        if (!rules.readonly) state.clear();
        return result;
    }
    Labels evaluate(const Expr* expression,State& state) {
        if (!expression) return {};
        if (auto* e=dyn_cast<ExprWithCleanups>(expression)) return evaluate(e->getSubExpr(),state);
        if (auto* e=dyn_cast<MaterializeTemporaryExpr>(expression)) return evaluate(e->getSubExpr(),state);
        if (auto* e=dyn_cast<CXXBindTemporaryExpr>(expression)) return evaluate(e->getSubExpr(),state);
        if (auto* e=dyn_cast<ParenExpr>(expression)) return evaluate(e->getSubExpr(),state);
        if (auto* e=dyn_cast<CXXDefaultArgExpr>(expression)) return evaluate(e->getExpr(),state);
        if (auto* e=dyn_cast<DeclRefExpr>(expression)) {
            if (auto* variable=dyn_cast<VarDecl>(e->getDecl())) return known(state,variable);
            return {};
        }
        if (auto* e=dyn_cast<CastExpr>(expression)) {
            auto result=evaluate(e->getSubExpr(),state);
            if (isa<CXXConstCastExpr>(e) || isa<CXXReinterpretCastExpr>(e)) {
                error(e->getExprLoc(),"const_cast and reinterpret_cast are unsupported in checked functions");
                state.clear(); return {};
            }
            return context.hasSameUnqualifiedType(e->getType(),e->getSubExpr()->getType()) ? result : Labels{};
        }
        if (auto* e=dyn_cast<ConditionalOperator>(expression)) {
            evaluate(e->getCond(),state);
            auto left=state, right=state;
            auto leftLabels=evaluate(e->getTrueExpr(),left), rightLabels=evaluate(e->getFalseExpr(),right);
            state=meet(left,right); return intersect(leftLabels,rightLabels);
        }
        if (auto* e=dyn_cast<BinaryOperator>(expression)) {
            if (e->isAssignmentOp()) {
                auto result=evaluate(e->getRHS(),state);
                evaluate(e->getLHS(),state); state.clear();
                if (e->getOpcode()==BO_Assign) set(state,wholeVariable(e->getLHS()),result);
                return e->getOpcode()==BO_Assign ? result : Labels{};
            }
            if (!e->isLogicalOp() && e->getOpcode()!=BO_Comma &&
                e->getLHS()->HasSideEffects(context) && e->getRHS()->HasSideEffects(context))
                error(e->getExprLoc(),"unsequenced side-effecting operands are unsupported; bind operands first");
            evaluate(e->getLHS(),state);
            if (e->isLogicalOp()) {
                auto skipped=state, executed=state;
                evaluate(e->getRHS(),executed); state=meet(skipped,executed); return {};
            }
            auto right=evaluate(e->getRHS(),state);
            return e->getOpcode()==BO_Comma ? right : Labels{};
        }
        if (auto* e=dyn_cast<UnaryOperator>(expression)) {
            evaluate(e->getSubExpr(),state);
            if (e->isIncrementDecrementOp() || e->getOpcode()==UO_AddrOf) state.clear();
            return {};
        }
        if (auto* e=dyn_cast<CallExpr>(expression)) return evaluateCall(e,state);
        if (auto* e=dyn_cast<CXXConstructExpr>(expression)) {
            unsigned sideEffects=0;
            for (auto* argument : e->arguments()) sideEffects+=argument->HasSideEffects(context);
            if (sideEffects>1) error(e->getExprLoc(),"multiple side-effecting constructor arguments are unsupported; bind arguments first");
            std::vector<Labels> values;
            for (auto* argument : e->arguments()) values.push_back(evaluate(argument,state));
            auto* constructor=e->getConstructor();
            checkDestruction(e->getType(),e->getExprLoc());
            if (!smartKind(e->getType()).empty()) {
                checkSmartType(e->getType(),e->getExprLoc());
                if (!values.empty() && e->getNumArgs()==1 &&
                    context.hasSameUnqualifiedType(e->getType(),e->getArg(0)->getType().getNonReferenceType())) {
                    if (smartKind(e->getType())=="unique_ptr" || e->getArg(0)->isXValue()) state.clear();
                    return values.front();
                }
                if (constructor->isDefaultConstructor()) return {};
                error(e->getExprLoc(),"smart-pointer adoption/conversion constructors are unsupported; use make_shared/make_unique or a contracted factory");
                state.clear(); return {};
            }
            if (constructor->isCopyConstructor() && constructor->isDefaulted() && !values.empty()) {
                if (!constructor->isTrivial()) state.clear();
                return values.front();
            }
            if (constructor->isMoveConstructor() && constructor->isDefaulted() && !values.empty()) {
                state.clear(); return values.front();
            }
            if (!constructor->isTrivial()) state.clear();
            return {};
        }
        if (auto* e=dyn_cast<InitListExpr>(expression)) {
            checkDestruction(e->getType(),e->getExprLoc());
            for (auto* init : e->inits()) evaluate(init,state);
            return {};
        }
        if (auto* e=dyn_cast<MemberExpr>(expression)) { evaluate(e->getBase(),state); return {}; }
        if (auto* e=dyn_cast<ArraySubscriptExpr>(expression)) { evaluate(e->getBase(),state); evaluate(e->getIdx(),state); return {}; }
        if (isa<IntegerLiteral>(expression) || isa<FloatingLiteral>(expression) || isa<StringLiteral>(expression) ||
            isa<CharacterLiteral>(expression) || isa<CXXBoolLiteralExpr>(expression) || isa<CXXNullPtrLiteralExpr>(expression) ||
            isa<CXXThisExpr>(expression) || isa<ImplicitValueInitExpr>(expression) || isa<UnaryExprOrTypeTraitExpr>(expression)) return {};
        if (isa<LambdaExpr>(expression)) {
            error(expression->getExprLoc(),"lambdas are unsupported; use a named contracted function");
            state.clear(); return {};
        }
        error(expression->getExprLoc(),"unsupported expression: "+std::string(expression->getStmtClassName()));
        state.clear(); return {};
    }
    Flow loop(const Stmt* body,const Expr* condition,const Expr* increment,State entry,bool doFirst) {
        auto head=entry;
        bool stable=false;
        ++silent;
        for (unsigned iteration=0;iteration<1024;++iteration) {
            auto start=head;
            if (!doFirst) evaluate(condition,start);
            auto flow=walk(body,start);
            auto back=join(flow.normal,flow.continues);
            if (back) { evaluate(increment,*back); if (doFirst) evaluate(condition,*back); }
            auto next=back ? meet(entry,*back) : entry;
            if (next==head) { stable=true; break; }
            head=std::move(next);
        }
        --silent;
        if (!stable) error(body->getBeginLoc(),"loop analysis did not converge within its safety limit");
        auto start=head;
        if (!doFirst) evaluate(condition,start);
        auto flow=walk(body,start);
        auto back=join(flow.normal,flow.continues);
        if (back) { evaluate(increment,*back); if (doFirst) evaluate(condition,*back); }
        auto exit=doFirst ? back : MaybeState(start);
        exit=join(exit,flow.breaks);
        return {exit,std::nullopt,std::nullopt};
    }
    Flow walk(const Stmt* statement,State state) {
        if (!statement) return {state,std::nullopt,std::nullopt};
        if (auto* cleaned=dyn_cast<ExprWithCleanups>(statement)) return walk(cleaned->getSubExpr(),state);
        if (auto* block=dyn_cast<CompoundStmt>(statement)) {
            Flow result{state,std::nullopt,std::nullopt};
            for (auto* child : block->body()) {
                if (!result.normal) break;
                auto next=walk(child,*result.normal);
                result.normal=std::move(next.normal);
                result.breaks=join(result.breaks,next.breaks);
                result.continues=join(result.continues,next.continues);
            }
            return result;
        }
        if (auto* declarations=dyn_cast<DeclStmt>(statement)) {
            for (auto* decl : declarations->decls()) if (auto* variable=dyn_cast<VarDecl>(decl)) {
                checkDestruction(variable->getType(),variable->getLocation());
                checkSmartType(variable->getType(),variable->getLocation());
                auto labels=evaluate(variable->getInit(),state);
                if (variable->getType()->isReferenceType()) {
                    if (auto* target=wholeVariable(variable->getInit())) aliases[variable->getCanonicalDecl()]=target;
                }
                set(state,variable,std::move(labels));
            }
            return {state,std::nullopt,std::nullopt};
        }
        if (auto* branch=dyn_cast<IfStmt>(statement)) {
            auto initialized=walk(branch->getInit(),state);
            if (!initialized.normal) return initialized;
            state=*initialized.normal;
            if (branch->getConditionVariableDeclStmt()) state=*walk(branch->getConditionVariableDeclStmt(),state).normal;
            evaluate(branch->getCond(),state);
            auto left=walk(branch->getThen(),state), right=walk(branch->getElse(),state);
            return {join(left.normal,right.normal),join(left.breaks,right.breaks),join(left.continues,right.continues)};
        }
        if (auto* whileLoop=dyn_cast<WhileStmt>(statement)) {
            if (whileLoop->getConditionVariable()) {
                error(statement->getBeginLoc(),"loop condition declarations are unsupported; bind the condition separately"); state.clear();
            }
            return loop(whileLoop->getBody(),whileLoop->getCond(),nullptr,state,false);
        }
        if (auto* forLoop=dyn_cast<ForStmt>(statement)) {
            auto initialized=walk(forLoop->getInit(),state);
            if (!initialized.normal) return initialized;
            if (forLoop->getConditionVariable()) error(statement->getBeginLoc(),"loop condition declarations are unsupported");
            return loop(forLoop->getBody(),forLoop->getCond(),forLoop->getInc(),*initialized.normal,false);
        }
        if (auto* doLoop=dyn_cast<DoStmt>(statement)) return loop(doLoop->getBody(),doLoop->getCond(),nullptr,state,true);
        if (isa<BreakStmt>(statement)) return {std::nullopt,state,std::nullopt};
        if (isa<ContinueStmt>(statement)) return {std::nullopt,std::nullopt,state};
        if (auto* returned=dyn_cast<ReturnStmt>(statement)) { evaluate(returned->getRetValue(),state); return {}; }
        if (auto* thrown=dyn_cast<CXXThrowExpr>(statement)) { evaluate(thrown->getSubExpr(),state); return {}; }
        if (auto* expression=dyn_cast<Expr>(statement)) { evaluate(expression,state); return {state,std::nullopt,std::nullopt}; }
        if (isa<NullStmt>(statement)) return {state,std::nullopt,std::nullopt};
        error(statement->getBeginLoc(),"unsupported control flow: "+std::string(statement->getStmtClassName()));
        state.clear(); return {state,std::nullopt,std::nullopt};
    }
public:
    Engine(ASTContext& context,Report& report) : context(context),diagnostics(context.getDiagnostics()),sources(context.getSourceManager()),report(report) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitCXXRecordDecl(CXXRecordDecl* record) {
        if (collectingLabels && hasAnnotation(record,"clangsolv.label")) vocabulary.insert(record->getQualifiedNameAsString());
        if (!collectingLabels && userLocation(record->getLocation()))
            for (auto* attr : record->specific_attrs<AnnotateAttr>())
                if (attr->getAnnotation().starts_with("clangsolv.") && attr->getAnnotation()!="clangsolv.label")
                    error(attr->getLocation(),"only generated label metadata belongs on a record declaration");
        return true;
    }
    bool VisitVarDecl(VarDecl* variable) {
        if (!collectingLabels && !isa<ParmVarDecl>(variable) && userLocation(variable->getLocation()))
            for (auto* attr : variable->specific_attrs<AnnotateAttr>())
                if (attr->getAnnotation().starts_with("clangsolv."))
                    error(attr->getLocation(),"contracts do not belong on local or global variables; use proof markers");
        return true;
    }
    bool VisitFunctionDecl(FunctionDecl* function) {
        if (collectingLabels || function->isImplicit() || !userLocation(function->getLocation())) return true;
        const auto& rules=contract(function);
        if (function->doesThisDeclarationHaveABody() && !function->isDependentContext() && rules.marker.empty() &&
            seenBodies.insert(function->getCanonicalDecl()).second) bodies.push_back(function);
        return true;
    }
    void run(TranslationUnitDecl* unit) {
        TraverseDecl(unit); collectingLabels=false; TraverseDecl(unit);
        for (auto* function : bodies) {
            aliases.clear();
            State entry;
            const auto& rules=contract(function);
            for (unsigned i=0;i<function->getNumParams();++i) set(entry,function->getParamDecl(i),rules.parameters[i]);
            CFG::BuildOptions options;
            auto cfg=CFG::buildCFG(function,function->getBody(),&context,options);
            if (!cfg) { error(function->getLocation(),"Clang could not construct a control-flow graph"); continue; }
            ++report.functions; report.cfgBlocks+=cfg->size();
            // Structured AST transfer implements a bounded subset of this CFG.
            // Unsupported statements reject the translation unit, rather than
            // pretending that an unmodelled edge preserves evidence.
            walk(function->getBody(),entry);
        }
    }
};

class Consumer : public ASTConsumer {
    Report& report;
public:
    explicit Consumer(Report& report) : report(report) {}
    void HandleTranslationUnit(ASTContext& context) override {
        if (!context.getDiagnostics().hasErrorOccurred()) Engine(context,report).run(context.getTranslationUnitDecl());
    }
};
class Action : public ASTFrontendAction {
    Report& report;
public:
    explicit Action(Report& report) : report(report) {}
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&,llvm::StringRef) override { return std::make_unique<Consumer>(report); }
};
class Factory : public tooling::FrontendActionFactory {
    Report& report;
public:
    explicit Factory(Report& report) : report(report) {}
    std::unique_ptr<FrontendAction> create() override { return std::make_unique<Action>(report); }
};

int main(int argc,const char** argv) {
    auto parser=tooling::CommonOptionsParser::create(argc,argv,Category);
    if (!parser) { llvm::errs()<<parser.takeError(); return 2; }
    Report report;
    tooling::ClangTool tool(parser->getCompilations(),parser->getSourcePathList());
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-DCLANGSOLV_CHECKING=1",tooling::ArgumentInsertPosition::BEGIN));
#ifdef CLANGSOLV_RESOURCE_DIR
    tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster("-resource-dir=" CLANGSOLV_RESOURCE_DIR,tooling::ArgumentInsertPosition::BEGIN));
#endif
    Factory factory(report);
    auto code=tool.run(&factory);
    const bool accepted=code==0 && report.errors==0;
    if (!ReportPath.empty()) {
        std::error_code error;
        llvm::raw_fd_ostream output(ReportPath,error);
        if (error) { llvm::errs()<<"ClangSOLV: cannot write report: "<<error.message()<<'\n'; return 2; }
        llvm::json::Array files;
        for (const auto& file : parser->getSourcePathList()) files.push_back(file);
        output<<llvm::formatv("{0:2}\n",llvm::json::Value(llvm::json::Object{
            {"schema",1},{"tool","clangsolv"},{"accepted",accepted},{"compiler_exit",code},
            {"functions_checked",report.functions},{"cfg_blocks",report.cfgBlocks},{"files",std::move(files)},
            {"requirements",std::move(report.requirements)},{"diagnostics",std::move(report.diagnostics)}}));
    }
    llvm::outs()<<"ClangSOLV: "<<(accepted ? "accepted" : "rejected")<<"; "<<report.functions<<" function bodies checked\n";
    return accepted ? 0 : 1;
}
