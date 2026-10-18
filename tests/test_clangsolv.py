#!/usr/bin/env python3
"""End-to-end LibTooling tests. No textual checker or mocked AST is used."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--checker',required=True)
OPTIONS,REST=parser.parse_known_args()
spec=importlib.util.spec_from_file_location('generator',ROOT/'generator/generate_labels.py')
generator=importlib.util.module_from_spec(spec); spec.loader.exec_module(generator)

PRELUDE='''#include <clangsolv_labels.hpp>
using namespace test::tags;
struct Item { int value; };
returns(P) Item persist(Item x);
readonly preserves(1) returns(V) Item validate(const Item& x);
readonly void consume(needs(P,V) const Item& x);
readonly void needP(needs(P) const Item& x);
readonly void needV(needs(V) const Item& x);
'''

SMART=PRELUDE+'\n#include <memory>\nreturns(P) std::shared_ptr<Item> shared();\nreturns(P) std::unique_ptr<Item> owned();\n'

class CheckerTests(unittest.TestCase):
    def check(self,body,accepted=True,diagnostic=None,prefix=PRELUDE):
        with tempfile.TemporaryDirectory(prefix='clangsolv-test-') as directory:
            directory=Path(directory)
            source=directory/'test.cpp'; report=directory/'report.json'
            (directory/'clangsolv_labels.hpp').write_text(generator.render('namespace=test::tags\nlabels=P,V,A,Approved'),encoding='utf-8')
            source.write_text(prefix+body,encoding='utf-8')
            result=subprocess.run([OPTIONS.checker,'--report',str(report),str(source),'--','-std=c++17',
                '-I'+str(ROOT/'library/include'),'-I'+str(directory)],capture_output=True,text=True,errors='replace')
            self.assertEqual(result.returncode==0,accepted,result.stdout+result.stderr)
            data=json.loads(report.read_text(encoding='utf-8'))
            self.assertEqual(data['accepted'],accepted)
            if diagnostic: self.assertIn(diagnostic,result.stderr)
            return data

    def test_shared_copy(self):
        self.check('void f(){auto p=shared(); auto q=p; needP(*q); clangsolv::require<P>(q); needP(*p);}',True,prefix=SMART)

    def test_shared_mutation(self):
        self.check('void f(){auto p=shared(); auto q=p; q->value=42; needP(*p);}',False,prefix=SMART)

    def test_shared_reset(self):
        self.check('void f(){auto p=shared(); auto q=p; q.reset(); needP(*p);}',False,prefix=SMART)

    def test_shared_escape(self):
        self.check('void f(){auto p=shared(); auto raw=p.get(); needP(*p);}',False,prefix=SMART)

    def test_shared_forget(self):
        self.check('void f(){auto p=shared(); auto q=p; clangsolv::forget<P>(q); needP(*p);}',False,prefix=SMART)

    def test_unique_move(self):
        self.check('void f(){auto p=owned(); auto q=std::move(p); needP(*q);}',True,prefix=SMART)

    def test_unique_moved_source(self):
        self.check('void f(){auto p=owned(); auto q=std::move(p); needP(*p);}',False,prefix=SMART)

    def test_smart_null(self):
        self.check('void f(){std::shared_ptr<Item> p; needP(*p);}',False,prefix=SMART)

    def test_smart_borrow_reset(self):
        self.check('void f(){auto p=shared(); const Item& borrowed=*p; p.reset(); needP(borrowed);}',False,prefix=SMART)

    def test_smart_establish(self):
        self.check('void f(){auto p=std::make_shared<Item>(); clangsolv::establish<P>(p); needP(*p);}',True,prefix=SMART)

    def test_unique_cast_move_invalidates_source(self):
        self.check('void f(){auto p=owned(); auto q=static_cast<std::unique_ptr<Item>&&>(p); needP(*q);}',prefix=SMART)
        self.check('void f(){auto p=owned(); auto q=static_cast<std::unique_ptr<Item>&&>(p); needP(*p);}',False,'missing required labels',prefix=SMART)

    def test_smart_branch_join(self):
        self.check('void f(bool b){auto p=shared(); if(b) p.reset(); needP(*p);}',False,'missing required labels',prefix=SMART)

    def test_smart_custom_deleter_rejected(self):
        self.check('struct D{void operator()(Item*) const;}; returns(P) std::unique_ptr<Item,D> custom();',False,'std::default_delete',prefix=SMART)

    def test_smart_late_establishment_is_conservative(self):
        self.check('void f(){auto p=std::make_shared<Item>(); auto q=p; clangsolv::establish<P>(p); needP(*q);}',False,'missing required labels',prefix=SMART)

    def test_smart_parameter(self):
        self.check('void f(needs(P) const std::shared_ptr<Item>& p){needP(*p);}',prefix=SMART)

    def test_smart_preservation(self):
        self.check('readonly preserves() returns(V) std::shared_ptr<Item> validatePtr(const std::shared_ptr<Item>& p); void f(){auto p=shared(); auto q=validatePtr(p); consume(*q);}',prefix=SMART)
        self.check('readonly preserves() std::shared_ptr<int> wrong(const std::shared_ptr<Item>& p);',False,'same domain type',prefix=SMART)

    def test_smart_array_rejected(self):
        self.check('returns(P) std::unique_ptr<Item[]> array();',False,'arrays and nested pointers',prefix=SMART)

    def test_smart_adoption_rejected(self):
        self.check('void f(Item* raw){std::shared_ptr<Item> p(raw);}',False,'adoption/conversion',prefix=SMART)

    def test_pipeline(self):
        report=self.check('void f(){auto p=persist(Item{}); auto v=validate(p); consume(v);}')
        self.assertEqual(len(report['requirements']),1)
        self.assertEqual(report['requirements'][0]['known'],['test::tags::P','test::tags::V'])

    def test_missing_label(self):
        self.check('void f(){auto p=persist(Item{}); consume(p);}',False,'missing required labels')

    def test_assignment_replaces_evidence(self):
        self.check('void f(){auto p=persist(Item{}); p=validate(p); consume(p); p=Item{}; consume(p);}',False,'missing required labels')

    def test_copy(self):
        self.check('void f(){auto p=persist(Item{}); auto v=validate(p); auto copy=v; consume(copy);}')

    def test_if_join(self):
        self.check('void f(bool b){auto p=persist(Item{}); auto v=validate(p); Item x; if(b) x=v; else x=p; needP(x);}')

    def test_if_join_loses_noncommon_label(self):
        self.check('void f(bool b){auto p=persist(Item{}); auto v=validate(p); Item x; if(b) x=v; else x=p; needV(x);}',False,'missing required labels')

    def test_early_return_path(self):
        self.check('void f(bool b){auto p=persist(Item{}); if(b) return; else p=validate(p); consume(p);}')

    def test_conditional_expression(self):
        self.check('void f(bool b){auto p=persist(Item{}); auto v=validate(p); auto x=b?v:p; needP(x);}')

    def test_conditional_loses_label(self):
        self.check('void f(bool b){auto p=persist(Item{}); auto v=validate(p); auto x=b?v:p; needV(x);}',False,'missing required labels')

    def test_mutation(self):
        self.check('void f(){auto p=persist(Item{}); auto v=validate(p); v.value=42; consume(v);}',False,'missing required labels')

    def test_reference_alias_invalidation(self):
        self.check('void f(){auto p=persist(Item{}); auto v=validate(p); Item& alias=v; alias.value=42; consume(v);}',False,'missing required labels')

    def test_reference_alias_evidence(self):
        self.check('void f(){auto p=persist(Item{}); auto v=validate(p); const Item& alias=v; consume(alias);}')

    def test_unknown_call_invalidation(self):
        self.check('void unknown(); void f(){auto p=persist(Item{}); auto v=validate(p); unknown(); consume(v);}',False,'missing required labels')

    def test_readonly_call(self):
        self.check('readonly void inspect(); void f(){auto p=persist(Item{}); auto v=validate(p); inspect(); consume(v);}')

    def test_loop_zero_iterations(self):
        self.check('void f(bool b){auto p=persist(Item{}); while(b) p=validate(p); consume(p);}',False,'missing required labels')

    def test_loop_later_iteration(self):
        self.check('void f(bool b){auto p=persist(Item{}); while(b){needP(p); p.value=1;}}',False,'missing required labels')

    def test_loop_retention(self):
        self.check('void f(bool b){auto p=persist(Item{}); while(b){needP(p); if(b) break; else continue;} needP(p);}')

    def test_do_while_establishment(self):
        self.check('void f(bool b){Item p{}; do {p=persist(p);} while(b); needP(p);}')

    def test_for_increment_mutation(self):
        self.check('void f(bool b){auto p=persist(Item{}); for(;b;p.value++){needP(p);}}',False,'missing required labels')

    def test_short_circuit(self):
        self.check('bool mutate(Item&); void f(bool b){auto p=persist(Item{}); bool x=b&&mutate(p); (void)x; needP(p);}',False,'missing required labels')

    def test_explicit_establish_require_forget(self):
        self.check('void f(){Item p{}; clangsolv::establish<P,V,Approved>(p); consume(p); clangsolv::require<Approved>(p); clangsolv::forget<V>(p); needP(p); consume(p);}',False,'missing required labels')

    def test_forget_all(self):
        self.check('void f(){Item p{}; clangsolv::establish<P,V>(p); clangsolv::forget(p); consume(p);}',False,'missing required labels')

    def test_new_label_without_checker_change(self):
        self.check('void f(){Item p{}; clangsolv::establish<Approved>(p); clangsolv::require<Approved>(p);}')

    def test_unknown_label(self):
        self.check('returns(Unknown) Item other();',False,'unknown label')

    def test_default_preservation(self):
        self.check('readonly preserves() Item identity(const Item& x); void f(){auto p=persist(Item{}); auto copy=identity(p); needP(copy);}')

    def test_second_argument_preservation(self):
        self.check('readonly preserves(2) Item second(const Item& x,const Item& y); void f(){Item raw{}; auto p=persist(Item{}); auto copy=second(raw,p); needP(copy);}')
        self.check('readonly preserves(2) Item second(const Item& x,const Item& y); void f(){Item raw{}; auto p=persist(Item{}); auto copy=second(p,raw); needP(copy);}',False,'missing required labels')

    def test_default_preservation_without_parameter(self):
        self.check('preserves() Item other();',False,'one-based parameter index')

    def test_invalid_preservation_index(self):
        self.check('preserves(0) Item other(Item);',False,'one-based parameter index')

    def test_preservation_domain_type(self):
        self.check('preserves(1) int other(Item);',False,'same domain type')

    def test_parameter_contract_body(self):
        self.check('void f(needs(P,V) const Item& x){consume(x);}')

    def test_header_redeclaration_contract(self):
        self.check('void consume(const Item& x) { clangsolv::require<P,V>(x); }')

    def test_unsupported_switch(self):
        self.check('void f(int n){switch(n){case 1: break; default: break;}}',False,'unsupported control flow')

    def test_indirect_call(self):
        self.check('void f(void(*fn)(Item),Item x){fn(x);}',False,'indirect calls are unsupported')

    def test_pointer_requirement(self):
        self.check('void f(needs(P) Item* x);',False,'labelled pointer parameters')

    def test_global_establishment(self):
        self.check('Item global; void f(){clangsolv::establish<P>(global);}',False,'named local value')

    def test_unregistered_marker(self):
        self.check('struct Fake{}; void f(){Item p{}; clangsolv::establish<Fake>(p);}',False,'unregistered label')

    def test_cast_cannot_carry_unrelated_labels(self):
        self.check('void f(){int x=42; clangsolv::establish<P>(x); double y=x; clangsolv::require<P>(y);}',False,'missing required labels')

    def test_multiargument_side_effects(self):
        self.check('readonly void two(needs(P) Item,Item); void f(){auto p=persist(Item{}); two(p,persist(Item{}));}',False,'bind arguments first')

    def test_syntax_error_report(self):
        data=self.check('void f(){not_cpp !!!}',False)
        self.assertNotEqual(data['compiler_exit'],0)

    def test_reassigned_pipeline(self):
        self.check('void f(){auto p=persist(Item{}); p=validate(p); consume(p);}')

    def test_member_receiver_effects(self):
        self.check('struct Observer{readonly void inspect();}; Observer sideeffect(); void f(){auto p=persist(Item{}); sideeffect().inspect(); needP(p);}',False,'missing required labels')

    def test_default_constructor_effects(self):
        self.check('int sideeffect(); struct Other{int n=sideeffect();}; void f(){auto p=persist(Item{}); Other other; needP(p);}',False,'missing required labels')

    def test_custom_destructor_rejected(self):
        self.check('struct Other{~Other();}; void f(){Other other;}',False,'user-defined destructors')

    def test_aggregate_destructor_rejected(self):
        self.check('struct Other{~Other();}; void f(){Other other{};}',False,'user-defined destructors')

    def test_virtual_dispatch_rejected(self):
        self.check('struct Other{virtual void inspect();}; void f(Other& other){other.inspect();}',False,'virtual dispatch')

    def test_trailing_empty_contract_label(self):
        self.check('returns(P,) Item other();',False,'unknown label')

    def test_const_cast_rejected(self):
        self.check('void f(const Item& other){auto& alias=const_cast<Item&>(other);}',False,'const_cast')

    def test_lambda_rejected(self):
        self.check('void f(){auto callback=[](){}; callback();}',False,'lambdas are unsupported')

    def test_variable_annotation_rejected(self):
        self.check('void f(){needs(P) Item x;}',False,'contracts do not belong')

    def test_static_local_establishment_rejected(self):
        self.check('void f(){static Item x; clangsolv::establish<P>(x);}',False,'named local value')

    def test_defaulted_move_destination(self):
        self.check('void f(){auto p=persist(Item{}); auto moved=static_cast<Item&&>(p); needP(moved);}')

    def test_defaulted_move_source_invalidated(self):
        self.check('void f(){auto p=persist(Item{}); auto moved=static_cast<Item&&>(p); needP(p);}',False,'missing required labels')

    def test_template_preservation_instantiation(self):
        self.check('template<class T> readonly preserves(1) T identity(const T& x){return x;} void f(){auto p=persist(Item{}); auto copy=identity(p); needP(copy);}')

    def test_ambiguous_label(self):
        self.check('namespace other{struct CLANGSOLV_ANNOTATE("clangsolv.label") P{};} returns(P) Item makeOther();',False,'ambiguous label')

    def test_qualified_label_disambiguates(self):
        prefix=PRELUDE.replace('returns(P)','returns(test::tags::P)').replace('needs(P,V)','needs(test::tags::P,V)').replace('needs(P)','needs(test::tags::P)')
        self.check('namespace other{struct CLANGSOLV_ANNOTATE("clangsolv.label") P{};} readonly returns(test::tags::P) Item makeOther(); void f(){auto x=makeOther(); needP(x);}',prefix=prefix)

    def test_binary_evaluation_order_rejected(self):
        self.check('readonly int read(needs(P) const Item&); int mutate(Item&); void f(){auto p=persist(Item{}); int n=read(p)+mutate(p);}',False,'unsequenced side-effecting operands')

    def test_nested_argument_evaluation_order_rejected(self):
        self.check('readonly int read(needs(P) const Item&); int mutate(Item&); void outer(int,int); void f(){auto p=persist(Item{}); outer(read(p),mutate(p));}',False,'evaluation ordering')

    def test_member_operator_requirement(self):
        self.check('struct Consumer{readonly int operator()(needs(P) const Item&) const;}; void f(){Consumer c; auto p=persist(Item{}); c(p);}')

    def test_member_operator_checks_explicit_argument(self):
        self.check('struct Consumer{readonly int operator()(needs(P) const Item&) const;}; void f(){Consumer c; Item p{}; clangsolv::establish<P>(c); c(p);}',False,'missing required labels')

    def test_member_operator_preservation(self):
        self.check('struct Factory{readonly preserves(1) Item operator()(const Item&) const;}; void f(){Factory factory; auto p=persist(Item{}); auto copy=factory(p); needP(copy);}')

    def test_compilation_database(self):
        with tempfile.TemporaryDirectory() as directory:
            directory=Path(directory)
            source=directory/'test.cpp'; report=directory/'report.json'
            (directory/'clangsolv_labels.hpp').write_text(generator.render('namespace=test::tags\nlabels=P,V,A'),encoding='utf-8')
            source.write_text(PRELUDE+'void f(){auto p=persist(Item{}); needP(p);}',encoding='utf-8')
            database=[{'directory':str(directory),'file':str(source),'arguments':['clang++','-std=c++17',
                '-I'+str(ROOT/'library/include'),'-I'+str(directory),'-c',str(source),'-o','test.o']}]
            (directory/'compile_commands.json').write_text(json.dumps(database),encoding='utf-8')
            result=subprocess.run([OPTIONS.checker,'-p',str(directory),'--report',str(report),str(source)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertTrue(json.loads(report.read_text())['accepted'])

class GeneratorTests(unittest.TestCase):
    def test_invalid_configs(self):
        for config in ['namespace=std\nlabels=X','namespace=app\nlabels=int','namespace=app\nlabels=X,X',
            'namespace=app\nlabels=X,','namespace=app\nlabels=_X','namespace=app\nlabels=X__Y',
            'namespace=app\nlabels=X\nextra=Y','labels=X','namespace=app\nnamespace=other\nlabels=X']:
            with self.subTest(config=config),self.assertRaises(ValueError): generator.render(config)

    def test_cli_stability_and_removal(self):
        with tempfile.TemporaryDirectory() as directory:
            config=Path(directory)/'labels.properties'; output=Path(directory)/'labels.hpp'
            config.write_text('namespace=app\nlabels=X,Y',encoding='utf-8')
            command=[sys.executable,str(ROOT/'generator/generate_labels.py'),'--config',str(config),'--output',str(output)]
            self.assertEqual(subprocess.run(command,capture_output=True).returncode,0)
            timestamp=output.stat().st_mtime_ns
            self.assertEqual(subprocess.run(command,capture_output=True).returncode,0)
            self.assertEqual(timestamp,output.stat().st_mtime_ns)
            config.write_text('namespace=app\nlabels=X',encoding='utf-8')
            self.assertEqual(subprocess.run(command,capture_output=True).returncode,0)
            self.assertNotIn(' Y ',output.read_text())

if __name__=='__main__': unittest.main(argv=[sys.argv[0],*REST],verbosity=2)
