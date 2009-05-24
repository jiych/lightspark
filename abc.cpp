/**************************************************************************
    Lightspark, a free flash player implementation

    Copyright (C) 2009  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

//#define __STDC_LIMIT_MACROS
#include <llvm/Module.h>
#include <llvm/DerivedTypes.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Constants.h> 
#include <llvm/Support/IRBuilder.h> 
#include <llvm/Target/TargetData.h>
#include "abc.h"
#include "logger.h"
#include "swftypes.h"
#include <sstream>


llvm::ExecutionEngine* ABCVm::ex;

extern __thread SystemState* sys;

using namespace std;
long timeDiff(timespec& s, timespec& d);

void ignore(istream& i, int count);

DoABCTag::DoABCTag(RECORDHEADER h, std::istream& in):DisplayListTag(h,in)
{
	int dest=in.tellg();
	dest+=getSize();
	in >> Flags >> Name;
	LOG(CALLS,"DoABCTag Name: " << Name);

	vm=new ABCVm(in);
	sys->currentVm=vm;

	if(dest!=in.tellg())
		LOG(ERROR,"Corrupted ABC data: missing " << dest-in.tellg());
}

int DoABCTag::getDepth() const
{
	return 0x20001;
}

void DoABCTag::Render()
{
	LOG(CALLS,"ABC Exec " << Name);
	vm->Run();
}

SymbolClassTag::SymbolClassTag(RECORDHEADER h, istream& in):DisplayListTag(h,in)
{
	LOG(TRACE,"SymbolClassTag");
	in >> NumSymbols;

	Tags.resize(NumSymbols);
	Names.resize(NumSymbols);

	for(int i=0;i<NumSymbols;i++)
		in >> Tags[i] >> Names[i];
}

int SymbolClassTag::getDepth() const
{
	//After DoABCTag execution
	return 0x30000;
}

void SymbolClassTag::Render()
{
	return;
	LOG(NOT_IMPLEMENTED,"SymbolClassTag Render");
	cout << "NumSymbols " << NumSymbols << endl;

	for(int i=0;i<NumSymbols;i++)
	{
		cout << Tags[i] << ' ' << Names[i] << endl;
		if(Tags[i]==0)
			sys->currentVm->buildNamedClass(sys,Names[i]);
		else
			sys->currentVm->buildNamedClass(new ASObject,Names[i]);
	}

}

//Be careful, arguments nubering starts from 1
ISWFObject* ABCVm::argumentDumper(arguments* arg, uint32_t n)
{
	return arg->args[n-1].getData();
}

void ABCVm::registerFunctions()
{
	vector<const llvm::Type*> sig;
	const llvm::Type* ptr_type=ex->getTargetData()->getIntPtrType();

	sig.push_back(llvm::PointerType::getUnqual(ptr_type));
	sig.push_back(llvm::IntegerType::get(32));
	llvm::FunctionType* FT=llvm::FunctionType::get(llvm::PointerType::getUnqual(ptr_type), sig, false);
	llvm::Function* F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"argumentDumper",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::argumentDumper);
	sig.clear();

	// (ABCVm*)
	sig.push_back(llvm::PointerType::getUnqual(ptr_type));
	FT=llvm::FunctionType::get(llvm::Type::VoidTy, sig, false);
	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"pushScope",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::pushScope);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"convert_i",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::convert_i);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"coerce_s",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::coerce_s);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"dup",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::dup);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"add",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::add);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"swap",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::swap);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"pushNull",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::pushNull);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"pushFalse",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::pushFalse);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"asTypelate",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::asTypelate);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"popScope",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::popScope);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"newActivation",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::newActivation);

	// (ABCVm*,int)
	sig.push_back(llvm::IntegerType::get(32));
	FT=llvm::FunctionType::get(llvm::Type::VoidTy, sig, false);
	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"ifLT",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::ifLT);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"ifStrictNE",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::ifStrictNE);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"ifEq",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::ifEq);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"newCatch",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::newCatch);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"newObject",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::newObject);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"ifFalse",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::ifFalse);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"jump",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::jump);

/*	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getLocal",&module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getLocal);*/

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getSlot",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getSlot);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"setSlot",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::setSlot);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getLocal",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getLocal);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"setLocal",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::setLocal);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"coerce",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::coerce);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getLex",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getLex);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"findPropStrict",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::findPropStrict);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"pushByte",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::pushByte);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"incLocal_i",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::incLocal_i);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getProperty",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getProperty);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"setProperty",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::setProperty);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"findProperty",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::findProperty);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"constructSuper",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::constructSuper);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"newArray",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::newArray);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"newClass",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::newClass);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"pushString",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::pushString);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"initProperty",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::initProperty);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"kill",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::kill);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"getScopeObject",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::getScopeObject);

	// (ABCVm*,int,int)
	sig.push_back(llvm::IntegerType::get(32));
	FT=llvm::FunctionType::get(llvm::Type::VoidTy, sig, false);
	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"callPropVoid",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::callPropVoid);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"callProperty",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::callProperty);

	F=llvm::Function::Create(FT,llvm::Function::ExternalLinkage,"constructProp",module);
	ex->addGlobalMapping(F,(void*)&ABCVm::constructProp);
}

void ABCVm::registerClasses()
{
	//Register predefined types, ASObject are enough for not implemented classes
	
	Global.setVariableByName(".Object",new ASObject);
	valid_classes[".Object"]=-1;
	Global.setVariableByName(".int",new ASObject);
	valid_classes[".int"]=-1;
	Global.setVariableByName(".Boolean",new ASObject);
	valid_classes[".Boolean"]=-1;

	Global.setVariableByName(".Error",new ASObject);

	Global.setVariableByName("flash.events.EventDispatcher",new ASObject);
	Global.setVariableByName("flash.display.DisplayObject",new ASObject);
	Global.setVariableByName("flash.display.InteractiveObject",new ASObject);
	Global.setVariableByName("flash.display.DisplayObjectContainer",new ASObject);
	Global.setVariableByName("flash.display.Sprite",new ASObject);
	Global.setVariableByName("flash.events.Event",new ASObject);
	Global.setVariableByName("flash.net.LocalConnection",new ASObject);
	Global.setVariableByName("flash.utils.Proxy",new ASObject);
	Global.setVariableByName("flash.events.ProgressEvent",new ASObject);
}

string ABCVm::getMultinameString(unsigned int mi, method_info* th) const
{
	const multiname_info* m=&constant_pool.multinames[mi];
	string ret;
	switch(m->kind)
	{
		case 0x07:
		{
			const namespace_info* n=&constant_pool.namespaces[m->ns];
			if(n->name)
			{
				ret=getString(n->name);
				if(ret=="")
					ret=getString(m->name);
				else
					ret+="."+getString(m->name);
			}
			else
				ret=getString(m->name);
			break;
		}
		case 0x09:
		{
			const ns_set_info* s=&constant_pool.ns_sets[m->ns_set];
			//printNamespaceSet(s);
			if(s->count!=1)
			{
				LOG(ERROR,"Multiname on namespace set not really supported yet");
				ret="<Unsupported>";
			}
			else
			{
				const namespace_info* n=&constant_pool.namespaces[s->ns[0]];
				ret=getString(n->name)+'.'+ getString(m->name);
			}
			break;
		}
		case 0x1b:
		{
			string name;
			if(th!=NULL)
			{
				ISWFObject* n=th->runtime_stack_pop();
				if(n->getObjectType()!=T_STRING)
				{
					LOG(ERROR,"Name on the stack should be a string");
					name="<Invalid>";
				}
				else
					name=n->toString();
			}
			else
				name="<Invalid>";
			//We currently assume that a null namespace is good
			const ns_set_info* s=&constant_pool.ns_sets[m->ns_set];
			printNamespaceSet(s);
			return "."+name;
			break;
		}
/*		case 0x0d:
			LOG(CALLS, "QNameA");
			break;
		case 0x0f:
			LOG(CALLS, "RTQName");
			break;
		case 0x10:
			LOG(CALLS, "RTQNameA");
			break;
		case 0x11:
			LOG(CALLS, "RTQNameL");
			break;
		case 0x12:
			LOG(CALLS, "RTQNameLA");
			break;
		case 0x0e:
			LOG(CALLS, "MultinameA");
			break;
		case 0x1c:
			LOG(CALLS, "MultinameLA");
			break;*/
		default:
			LOG(ERROR,"Multiname to String not yet implemented for this kind " << hex << m->kind);
			break;
	}
	return ret;
}

ABCVm::ABCVm(istream& in)
{
	in >> minor >> major;
	LOG(CALLS,"ABCVm version " << major << '.' << minor);
	in >> constant_pool;

	in >> method_count;
	methods.resize(method_count);
	for(int i=0;i<method_count;i++)
		in >> methods[i];

	in >> metadata_count;
	metadata.resize(metadata_count);
	for(int i=0;i<metadata_count;i++)
		in >> metadata[i];

	in >> class_count;
	instances.resize(class_count);
	for(int i=0;i<class_count;i++)
	{
		in >> instances[i];
		//Link instance names with classes
		valid_classes[getMultinameString(instances[i].name)]=i;

		cout << getMultinameString(instances[i].name) << endl;
	}
	classes.resize(class_count);
	for(int i=0;i<class_count;i++)
		in >> classes[i];

	in >> script_count;
	scripts.resize(script_count);
	for(int i=0;i<script_count;i++)
		in >> scripts[i];

	in >> method_body_count;
	method_body.resize(method_body_count);
	for(int i=0;i<method_body_count;i++)
	{
		in >> method_body[i];
		//Link method body with method signature
		if(methods[method_body[i].method].body!=NULL)
			LOG(ERROR,"Duplicate body assigment")
		else
			methods[method_body[i].method].body=&method_body[i];
	}

}

SWFObject ABCVm::buildNamedClass(ISWFObject* base, const string& s)
{
	map<string,int>::iterator it=valid_classes.find(s);
	if(it!=valid_classes.end())
		LOG(CALLS,"Class " << s << " found")
	else
		LOG(CALLS,"Class " << s << " not found")
	//TODO: Really build class
	int index=it->second;
	if(index==-1)
	{
		bool found;
		ISWFObject* r=Global.getVariableByName(it->first,found);
		if(!found)
		{
			LOG(ERROR,"Class " << it->first << " not found");
			abort();
		}
		return r->clone();
	}
	else
	{
		method_info* m=&methods[classes[index].cinit];
		synt_method(m);
		LOG(CALLS,"Calling Class init");
		if(m->f)
		{
			Function::as_function FP=(Function::as_function)ex->getPointerToFunction(m->f);
			FP(&Global,NULL);
		}
		m=&methods[instances[index].init];
		synt_method(m);
		LOG(CALLS,"Building instance traits");
		for(int i=0;i<instances[index].trait_count;i++)
			buildTrait(base,&instances[index].traits[i]);

		LOG(CALLS,"Calling Instance init");
		//module.dump();
		if(m->f)
		{
			arguments args;
			args.args.push_back(new Null);
			Function::as_function FP=(Function::as_function)ex->getPointerToFunction(m->f);
			FP(base,&args);
		}
		return base;
	}
}

inline method_info* ABCVm::get_method(unsigned int m)
{
	if(m<method_count)
		return &methods[m];
	else
	{
		LOG(ERROR,"Requested invalid method");
		return NULL;
	}
}

void ABCVm::add(method_info* th)
{
	cout << "add" << endl;
}

void ABCVm::asTypelate(method_info* th)
{
	cout << "asTypelate" << endl;
}

void ABCVm::swap(method_info* th)
{
	cout << "swap" << endl;
}

void ABCVm::newActivation(method_info* th)
{
	cout << "newActivation" << endl;
	//TODO: Should create a real activation object
	//TODO: Should method traits be added to the activation context?
	ASObject* act=new ASObject;
	for(int i=0;i<th->body->trait_count;i++)
		th->vm->buildTrait(act,&th->body->traits[i]);

	th->runtime_stack_push(act);
}

void ABCVm::popScope(method_info* th)
{
	cout << "popScope" << endl;
}

void ABCVm::constructProp(method_info* th, int n, int m)
{
	string name=th->vm->getMultinameString(n);
	cout << "constructProp " << name << ' ' << m << endl;
}

void ABCVm::callProperty(method_info* th, int n, int m)
{
	//Should be called after arguments are popped
	string name=th->vm->getMultinameString(n);
	cout << "callProperty " << name << ' ' << m << endl;
	arguments args;
	args.args.resize(m);
	for(int i=0;i<m;i++)
		args.args[m-i-1]=th->runtime_stack_pop();
	ISWFObject* obj=th->runtime_stack_pop();
	bool found;
	SWFObject o=obj->getVariableByName(name,found);
	//If o is already a function call it, otherwise find the Call method
	if(o->getObjectType()==T_FUNCTION)
	{
		IFunction* f=dynamic_cast<IFunction*>(o.getData());
		ISWFObject* ret=f->call(obj,&args);
		th->runtime_stack_push(ret);
	}
	else if(o->getObjectType()==T_UNDEFINED)
	{
		LOG(NOT_IMPLEMENTED,"We got a Undefined function");
		th->runtime_stack_push(new Undefined);
	}
	else
	{
		IFunction* f=dynamic_cast<IFunction*>(o->getVariableByName(".Call",found));
		ISWFObject* ret=f->call(obj,&args);
		th->runtime_stack_push(ret);
	}
}

void ABCVm::callPropVoid(method_info* th, int n, int m)
{
	string name=th->vm->getMultinameString(n); 
	cout << "callPropVoid " << name << ' ' << m << endl;
	arguments args;
	args.args.resize(m);
	for(int i=0;i<m;i++)
		args.args[m-i-1]=th->runtime_stack_pop();
	ISWFObject* obj=th->runtime_stack_pop();
	bool found;
	SWFObject o=obj->getVariableByName(name,found);
	//If o is already a function call it, otherwise find the Call method
	if(o->getObjectType()==T_FUNCTION)
	{
		IFunction* f=dynamic_cast<IFunction*>(o.getData());
		f->call(obj,&args);
	}
	else
	{
		IFunction* f=dynamic_cast<IFunction*>(o->getVariableByName(".Call",found));
		f->call(obj,&args);
	}
}

void ABCVm::jump(method_info* th, int offset)
{
	cout << "jump " << offset << endl;
}

void ABCVm::ifFalse(method_info* th, int offset)
{
	cout << "ifFalse " << offset << endl;

	ISWFObject* obj1=th->runtime_stack_pop();
	th->runtime_stack_push((ISWFObject*)new uintptr_t(!Boolean_concrete(obj1)));
}

//We follow the Boolean() algorithm, but return a concrete result, not a Boolean object
bool Boolean_concrete(ISWFObject* obj)
{
	if(obj->getObjectType()==T_STRING)
	{
		cout << "string to bool" << endl;
		string s=obj->toString();
		if(s.empty())
			return false;
		else
			return true;
	}
	else if(obj->getObjectType()==T_OBJECT)
	{
		cout << "object to bool" << endl;
		return true;
	}
	else
		return false;
}

void ABCVm::ifStrictNE(method_info* th, int offset)
{
	cout << "ifStrictNE " << offset << endl;
}

void ABCVm::ifLT(method_info* th, int offset)
{
	cout << "ifLT " << offset << endl;
}

void ABCVm::ifEq(method_info* th, int offset)
{
	cout << "ifEq " << offset << endl;

	ISWFObject* obj1=th->runtime_stack_pop();
	ISWFObject* obj2=th->runtime_stack_pop();

	//Real comparision demanded to object
	if(obj1->isEqual(obj2))
		th->runtime_stack_push((ISWFObject*)new uintptr_t(1));
	else
		th->runtime_stack_push((ISWFObject*)new uintptr_t(0));
}

void ABCVm::coerce(method_info* th, int n)
{
	cout << "coerce " << n << endl;
}

void ABCVm::newCatch(method_info* th, int n)
{
	cout << "newCatch " << n << endl;
}

void ABCVm::newObject(method_info* th, int n)
{
	cout << "newObject " << n << endl;
}

void ABCVm::setSlot(method_info* th, int n)
{
	cout << "setSlot DONE: " << n << endl;
	ISWFObject* value=th->runtime_stack_pop();
	ISWFObject* obj=th->runtime_stack_pop();
	
	obj->setSlot(n,value);
}

void ABCVm::getSlot(method_info* th, int n)
{
	cout << "getSlot DONE: " << n << endl;
	ISWFObject* obj=th->runtime_stack_pop();
	th->runtime_stack_push(obj->getSlot(n));
}

void ABCVm::getLocal(method_info* th, int n)
{
	cout << "getLocal: DONE " << n << endl;
}

void ABCVm::setLocal(method_info* th, int n)
{
	cout << "setLocal: DONE " << n << endl;
}

void ABCVm::convert_i(method_info* th)
{
	cout << "convert_i" << endl;
}

void ABCVm::coerce_s(method_info* th)
{
	cout << "coerce_s" << endl;
}

void ABCVm::dup(method_info* th)
{
	cout << "dup: DONE" << endl;
}

void ABCVm::pushFalse(method_info* th)
{
	cout << "pushFalse" << endl;
	th->runtime_stack_push(new Boolean(false));
}

void ABCVm::pushNull(method_info* th)
{
	cout << "pushNull DONE" << endl;
	th->runtime_stack_push(new Null);
}

void ABCVm::pushScope(method_info* th)
{
	ISWFObject* t=th->runtime_stack_pop();
	cout << "pushScope: DONE " << t << endl;
	th->scope_stack.push_back(t);
}

void ABCVm::pushByte(method_info* th, int n)
{
	cout << "pushByte " << n << endl;
}

void ABCVm::incLocal_i(method_info* th, int n)
{
	cout << "incLocal_i " << n << endl;
}

void ABCVm::constructSuper(method_info* th, int n)
{
	cout << "constructSuper " << n << endl;
}

void ABCVm::setProperty(method_info* th, int n)
{
	ISWFObject* value=th->runtime_stack_pop();
	string name=th->vm->getMultinameString(n,th);
	cout << "setProperty " << name << endl;

	ISWFObject* obj=th->runtime_stack_pop();
	//DEBUG
	ASObject* o=dynamic_cast<ASObject*>(obj);
	printf("Object ID 0x%lx\n",o->debug_id);

	ISWFObject* ret=obj->setVariableByName(name,value);
}

void ABCVm::getProperty(method_info* th, int n)
{
	string name=th->vm->getMultinameString(n,th);
	cout << "getProperty " << name << endl;

	ISWFObject* obj=th->runtime_stack_pop();
	//DEBUG
	ASObject* o=dynamic_cast<ASObject*>(obj);
	if(o)
		printf("Object ID 0x%lx\n",o->debug_id);


	bool found;
	ISWFObject* ret=obj->getVariableByName(name,found);
	if(!found)
	{
		LOG(ERROR,"Property not found");
		th->runtime_stack_push(ret);
	}
	else
	{
		//DEBUG
		ASObject* r=dynamic_cast<ASObject*>(ret);
		printf("0x%lx\n",r->debug_id);

		th->runtime_stack_push(ret);
	}
}

void ABCVm::findProperty(method_info* th, int n)
{
	string name=th->vm->getMultinameString(n);
	cout << "findProperty " << name << endl;

	vector<ISWFObject*>::reverse_iterator it=th->scope_stack.rbegin();
	bool found=false;
	for(it;it!=th->scope_stack.rend();it++)
	{
		(*it)->getVariableByName(name,found);
		if(found)
		{
			//We have to return the object, not the property
			th->runtime_stack_push(*it);
			break;
		}
	}
	if(!found)
	{
		cout << "NOT found, pushing global" << endl;
		th->runtime_stack_push(&th->vm->Global);
	}
}

void ABCVm::findPropStrict(method_info* th, int n)
{
	string name=th->vm->getMultinameString(n);
	cout << "findPropStrict " << name << endl;

	vector<ISWFObject*>::reverse_iterator it=th->scope_stack.rbegin();
	bool found=false;
	for(it;it!=th->scope_stack.rend();it++)
	{
		(*it)->getVariableByName(name,found);
		if(found)
		{
			//We have to return the object, not the property
			th->runtime_stack_push(*it);
			break;
		}
	}
	if(!found)
	{
		cout << "NOT found, pushing Undefined" << endl;
		th->runtime_stack_push(new Undefined);
	}
}

void ABCVm::initProperty(method_info* th, int n)
{
	string name=th->vm->getMultinameString(n);
	cout << "initProperty " << name << endl;
	ISWFObject* value=th->runtime_stack_pop();
	//DEBUG
	ASObject* r=dynamic_cast<ASObject*>(value);
	if(r!=NULL)
		printf("Value ID 0x%lx\n",r->debug_id);

	ISWFObject* obj=th->runtime_stack_pop();

	//TODO: Should we make a copy or pass the reference
	obj->setVariableByName(name,value);
}

void ABCVm::newArray(method_info* th, int n)
{
	cout << "newArray " << n << endl;
//	th->printClass(n);
}

void ABCVm::newClass(method_info* th, int n)
{
	cout << "newClass " << n << endl;
	th->runtime_stack_push(new Undefined);
//	th->printClass(n);
}

void ABCVm::getScopeObject(method_info* th, int n)
{
	th->runtime_stack_push(th->scope_stack[n]);
	cout << "getScopeObject: DONE " << th->scope_stack[n] << endl;
}

void ABCVm::debug(int p)
{
	cout << "debug " << p << endl;
}

void ABCVm::getLex(method_info* th, int n)
{
	string name=th->vm->getMultinameString(n);
	cout << "getLex DONE: " << name << endl;
	vector<ISWFObject*>::reverse_iterator it=th->scope_stack.rbegin();
	bool found=false;
	for(it;it!=th->scope_stack.rend();it++)
	{
		SWFObject o=(*it)->getVariableByName(name,found);
		if(found)
		{
			th->runtime_stack_push(o.getData());
			//DEBUG
			ASObject* r=dynamic_cast<ASObject*>(o.getData());
			if(r!=NULL)
				printf("Found ID 0x%lx\n",r->debug_id);
			break;
		}
	}
	if(!found)
	{
		cout << "NOT found, pushing Undefined" << endl;
		th->runtime_stack_push(new Undefined);
	}

}

void ABCVm::pushString(method_info* th, int n)
{
	string s=th->vm->getString(n); 
	cout << "pushString " << s << endl;
	th->runtime_stack_push(new ASString(s));
}

void ABCVm::kill(method_info* th, int n)
{
	cout << "kill " << n << endl;
}

void method_info::runtime_stack_push(ISWFObject* s)
{
	stack[stack_index++]=s;
	cout << "Runtime stack index " << stack_index << endl;
}

void method_info::setStackLength(const llvm::ExecutionEngine* ex, int l)
{
	const llvm::Type* ptr_type=ex->getTargetData()->getIntPtrType();
	//TODO: We add this huge safety margin because not implemented instruction do not clean the stack as they should
	stack=new ISWFObject*[ l*10 ];
	llvm::Constant* constant = llvm::ConstantInt::get(ptr_type, (uintptr_t)stack);
	//TODO: Now the stack is considered an array of pointer to int64
	dynamic_stack = llvm::ConstantExpr::getIntToPtr(constant, llvm::PointerType::getUnqual(llvm::PointerType::getUnqual(ptr_type)));
	constant = llvm::ConstantInt::get(ptr_type, (uintptr_t)&stack_index);
	dynamic_stack_index = llvm::ConstantExpr::getIntToPtr(constant, llvm::PointerType::getUnqual(llvm::IntegerType::get(32)));

}

llvm::Value* method_info::llvm_stack_pop(llvm::IRBuilder<>& builder) const 
{
	//decrement stack index
	llvm::Value* index=builder.CreateLoad(dynamic_stack_index);
	llvm::Constant* constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), 1);
	llvm::Value* index2=builder.CreateSub(index,constant);
	builder.CreateStore(index2,dynamic_stack_index);

	llvm::Value* dest=builder.CreateGEP(dynamic_stack,index2);
	return builder.CreateLoad(dest);
}

llvm::Value* method_info::llvm_stack_peek(llvm::IRBuilder<>& builder) const
{
	llvm::Value* index=builder.CreateLoad(dynamic_stack_index);
	llvm::Constant* constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), 1);
	llvm::Value* index2=builder.CreateSub(index,constant);
	llvm::Value* dest=builder.CreateGEP(dynamic_stack,index2);
	return builder.CreateLoad(dest);
}

void method_info::llvm_stack_push(llvm::IRBuilder<>& builder, llvm::Value* val)
{
	llvm::Value* index=builder.CreateLoad(dynamic_stack_index);
	llvm::Value* dest=builder.CreateGEP(dynamic_stack,index);
	builder.CreateStore(val,dest);

	//increment stack index
	llvm::Constant* constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), 1);
	llvm::Value* index2=builder.CreateAdd(index,constant);
	builder.CreateStore(index2,dynamic_stack_index);
}

ISWFObject* method_info::runtime_stack_pop()
{
	if(stack_index==0)
	{
		LOG(ERROR,"Empty stack");
		return NULL;
	}
	cout << "Runtime stack index " << stack_index << endl;
	return stack[--stack_index];
}

ISWFObject* method_info::runtime_stack_peek()
{
	if(stack_index==0)
	{
		LOG(ERROR,"Empty stack");
		return NULL;
	}
	cout << "Runtime stack index " << stack_index << endl;
	return stack[stack_index-1];
}

inline ABCVm::stack_entry ABCVm::static_stack_pop(llvm::IRBuilder<>& builder, vector<ABCVm::stack_entry>& static_stack, const method_info* m) 
{
	//try to get the tail value from the static stack
	if(!static_stack.empty())
	{
		stack_entry ret=static_stack.back();
		static_stack.pop_back();
		return ret;
	}
	//try to pop the tail value of the dynamic stack
	cout << "Popping dynamic stack" << endl;
	return stack_entry(m->llvm_stack_pop(builder),STACK_OBJECT);
}

inline ABCVm::stack_entry ABCVm::static_stack_peek(llvm::IRBuilder<>& builder, vector<ABCVm::stack_entry>& static_stack, const method_info* m) 
{
	//try to get the tail value from the static stack
	if(!static_stack.empty())
		return static_stack.back();
	//try to load the tail value of the dynamic stack
	cout << "Peeking dynamic stack" << endl;
	return stack_entry(m->llvm_stack_peek(builder),STACK_OBJECT);
}

inline void ABCVm::static_stack_push(vector<ABCVm::stack_entry>& static_stack, const ABCVm::stack_entry& e)
{
	static_stack.push_back(e);
}

inline void ABCVm::syncStacks(llvm::IRBuilder<>& builder, bool jitted,std::vector<stack_entry>& static_stack,method_info* m)
{
	if(jitted)
	{
		for(int i=0;i<static_stack.size();i++)
		{
			if(static_stack[i].second!=STACK_OBJECT)
				LOG(ERROR,"Conversion not yet implemented");
			m->llvm_stack_push(builder,static_stack[i].first);
		}
		static_stack.clear();
	}
}

llvm::FunctionType* ABCVm::synt_method_prototype()
{
	//The pointer size compatible int type will be useful
	const llvm::Type* ptr_type=ex->getTargetData()->getIntPtrType();

	//Initialize LLVM representation of method
	vector<const llvm::Type*> sig;
	sig.push_back(llvm::PointerType::getUnqual(ptr_type));
	sig.push_back(llvm::PointerType::getUnqual(ptr_type));

	return llvm::FunctionType::get(llvm::Type::VoidTy, sig, false);
}

llvm::Function* ABCVm::synt_method(method_info* m)
{
	if(m->f)
		return m->f;

	if(!m->body)
	{
		string n=getString(m->name);
		LOG(CALLS,"Method " << n << " should be intrinsic");
		return NULL;
	}
	stringstream code(m->body->code);
	m->vm=this;
	llvm::FunctionType* method_type=synt_method_prototype();
	m->f=llvm::Function::Create(method_type,llvm::Function::ExternalLinkage,"method",module);

	//The pointer size compatible int type will be useful
	const llvm::Type* ptr_type=ex->getTargetData()->getIntPtrType();
	
	llvm::BasicBlock *BB = llvm::BasicBlock::Create("entry", m->f);
	llvm::IRBuilder<> Builder;

	//We define a couple of variables that will be used a lot
	llvm::Constant* constant;
	llvm::Constant* constant2;
	llvm::Value* value;
	//let's give access to method data to llvm
	constant = llvm::ConstantInt::get(ptr_type, (uintptr_t)m);
	llvm::Value* th = llvm::ConstantExpr::getIntToPtr(constant, llvm::PointerType::getUnqual(ptr_type));

	//let's give access to local data storage
	m->locals=new ISWFObject*[m->body->local_count];
	constant = llvm::ConstantInt::get(ptr_type, (uintptr_t)m->locals);
	llvm::Value* locals = llvm::ConstantExpr::getIntToPtr(constant, 
			llvm::PointerType::getUnqual(llvm::PointerType::getUnqual(ptr_type)));

	//the stack is statically handled as much as possible to allow llvm optimizations
	//on branch and on interpreted/jitted code transition it is synchronized with the dynamic one
	vector<stack_entry> static_stack;
	static_stack.reserve(m->body->max_stack);
	m->setStackLength(ex,m->body->max_stack);

	//the scope stack is not accessible to llvm code
	
	//Creating a mapping between blocks and starting address
	map<int,llvm::BasicBlock*> blocks;
	blocks.insert(pair<int,llvm::BasicBlock*>(0,BB));

	bool jitted=false;

	//This is initialized to true so that on first iteration the entry block is used
	bool last_is_branch=true;

	Builder.SetInsertPoint(BB);
	//We fill locals with function arguments
	//First argument is the 'this'
	constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), 0);
	llvm::Value* t=Builder.CreateGEP(locals,constant);
	llvm::Function::ArgumentListType::iterator it=m->f->getArgumentList().begin();
	llvm::Value* arg=it;
	Builder.CreateStore(arg,t);
	//Second argument is the arguments pointer
	it++;
	for(int i=0;i<m->param_count;i++)
	{
		constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), i+1);
		t=Builder.CreateGEP(locals,constant);
		arg=Builder.CreateCall2(ex->FindFunctionNamed("argumentDumper"), it, constant);
		Builder.CreateStore(arg,t);
	}

	//Each case block builds the correct parameters for the interpreter function and call it
	u8 opcode;
	while(1)
	{
		//Check if we are expecting a new block start
		map<int,llvm::BasicBlock*>::iterator it=blocks.find(code.tellg());
		if(it!=blocks.end())
		{
			//A new block starts, the last instruction should have been a branch?
			if(!last_is_branch)
			{
				cout << "Last instruction before a new block was not a branch. Opcode " << hex << opcode<< endl;
				Builder.CreateBr(it->second);
			}
			Builder.SetInsertPoint(it->second);
		}
		
		last_is_branch=false;

		code >> opcode;
		if(code.eof())
			break;
		switch(opcode)
		{
			case 0x08:
			{
				//kill
				cout << "synt kill" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("kill"), th, constant);
				break;
			}
			case 0x09:
			{
				//label
				syncStacks(Builder,jitted,static_stack,m);
				//Create a new block and insert it in the mapping
				llvm::BasicBlock* A;
				map<int,llvm::BasicBlock*>::iterator it=blocks.find(code.tellg());
				if(it!=blocks.end())
					A=it->second;
				else
				{
					A=llvm::BasicBlock::Create("fall", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(code.tellg(),A));
				}
				Builder.CreateBr(A);
				Builder.SetInsertPoint(A);
				break;
			}
			case 0x10:
			{
				//jump
				cout << "synt jump" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				last_is_branch=true;

				s24 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("jump"), th, constant);

				//Create a block for the fallthrough code and insert it in the mapping
				llvm::BasicBlock* A;
				map<int,llvm::BasicBlock*>::iterator it=blocks.find(code.tellg());
				if(it!=blocks.end())
					A=it->second;
				else
				{
					A=llvm::BasicBlock::Create("fall", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(code.tellg(),A));
				}
				//Create a block for the landing code and insert it in the mapping
				llvm::BasicBlock* B;
				it=blocks.find(int(code.tellg())+t);
				if(it!=blocks.end())
					B=it->second;
				else
				{
					B=llvm::BasicBlock::Create("jump_land", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(int(code.tellg())+t,B));
				}

				Builder.CreateBr(B);
				Builder.SetInsertPoint(A);
				break;
			}
			case 0x12:
			{
				//iffalse
				cout << "synt iffalse" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;

				last_is_branch=true;
				s24 t;
				code >> t;

				//Create a block for the fallthrough code and insert in the mapping
				llvm::BasicBlock* A;
				map<int,llvm::BasicBlock*>::iterator it=blocks.find(code.tellg());
				if(it!=blocks.end())
					A=it->second;
				else
				{
					A=llvm::BasicBlock::Create("fall", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(code.tellg(),A));
				}

				//And for the branch destination, if they are not in the blocks mapping
				llvm::BasicBlock* B;
				it=blocks.find(int(code.tellg())+t);
				if(it!=blocks.end())
					B=it->second;
				else
				{
					B=llvm::BasicBlock::Create("then", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(int(code.tellg())+t,B));
				}
			
				//Make comparision
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("ifFalse"), th, constant);

				//Pop the stack, we are surely going to pop from the dynamic one
				//ifFalse pushed a pointer to integer
				llvm::Value* cond_ptr=static_stack_pop(Builder,static_stack,m).first;
				llvm::Value* cond=Builder.CreateLoad(cond_ptr);
				llvm::Value* cond1=Builder.CreateTrunc(cond,llvm::IntegerType::get(1));
				Builder.CreateCondBr(cond1,B,A);
				//Now start populating the fallthrough block
				Builder.SetInsertPoint(A);
				break;
			}
			case 0x13:
			{
				//ifeq
				cout << "synt ifeq" << endl;
				//TODO: implement common data comparison
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;

				last_is_branch=true;
				s24 t;
				code >> t;
				//Create a block for the fallthrough code and insert in the mapping
				llvm::BasicBlock* A;
				map<int,llvm::BasicBlock*>::iterator it=blocks.find(code.tellg());
				if(it!=blocks.end())
					A=it->second;
				else
				{
					A=llvm::BasicBlock::Create("fall", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(code.tellg(),A));
				}

				//And for the branch destination, if they are not in the blocks mapping
				llvm::BasicBlock* B;
				it=blocks.find(int(code.tellg())+t);
				if(it!=blocks.end())
					B=it->second;
				else
				{
					B=llvm::BasicBlock::Create("then", m->f);
					blocks.insert(pair<int,llvm::BasicBlock*>(int(code.tellg())+t,B));
				}
			
				//Make comparision
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("ifEq"), th, constant);

				//Pop the stack, we are surely going to pop from the dynamic one
				//ifEq pushed a pointer to integer
				llvm::Value* cond_ptr=static_stack_pop(Builder,static_stack,m).first;
				llvm::Value* cond=Builder.CreateLoad(cond_ptr);
				llvm::Value* cond1=Builder.CreateTrunc(cond,llvm::IntegerType::get(1));
				Builder.CreateCondBr(cond1,B,A);
				//Now start populating the fallthrough block
				Builder.SetInsertPoint(A);
				break;
			}
			case 0x15:
			{
				//iflt
				cout << "synt iflt" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				s24 t;
				code >> t;
				//Make comparision
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("ifLT"), th, constant);
				break;
			}
			case 0x1a:
			{
				//ifstrictne
				cout << "synt ifstrictne" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				s24 t;
				code >> t;
				//Make comparision
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("ifStrictNE"), th, constant);
				break;
			}
			case 0x1b:
			{
				//lookupswitch
				cout << "synt lookupswitch" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				s24 t;
				code >> t;
				printf("default %i\n",int(t));
				u30 count;
				code >> count;
				printf("count %i\n",int(count));
				for(int i=0;i<count+1;i++)
					code >> t;
				break;
			}
			case 0x1d:
			{
				//popscope
				cout << "synt popscope" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("popScope"), th);
				break;
			}
			case 0x20:
			{
				//pushnull
				cout << "synt pushnull" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("pushNull"), th);
				break;
			}
			case 0x24:
			{
				//pushbyte
				cout << "synt pushbyte" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				uint8_t t;
				code.read((char*)&t,1);
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("pushByte"), th, constant);
				break;
			}
			case 0x27:
			{
				//pushfalse
				cout << "synt pushfalse" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("pushFalse"), th);
				break;
			}
			case 0x2a:
			{
				//dup
				cout << "synt dup" << endl;
				jitted=true;
				Builder.CreateCall(ex->FindFunctionNamed("dup"), th);
				stack_entry e=static_stack_peek(Builder,static_stack,m);
				static_stack_push(static_stack,e);
				break;
			}
			case 0x2b:
			{
				//swap
				cout << "synt swap" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("swap"), th);
				break;
			}
			case 0x2c:
			{
				//pushstring
				cout << "synt pushstring" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("pushString"), th, constant);
				break;
			}
			case 0x30:
			{
				//pushscope
				cout << "synt pushscope" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("pushScope"), th);
				break;
			}
			case 0x46:
			{
				//callproperty
				//TODO: Implement static resolution where possible
				cout << "synt callproperty" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				code >> t;
				constant2 = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);

/*				//Pop the stack arguments
				vector<llvm::Value*> args(t+1);
				for(int i=0;i<t;i++)
					args[t-i]=static_stack_pop(Builder,static_stack,m).first;*/
				//Call the function resolver, static case could be resolved at this time (TODO)
				Builder.CreateCall3(ex->FindFunctionNamed("callProperty"), th, constant, constant2);
/*				//Pop the function object, and then the object itself
				llvm::Value* fun=static_stack_pop(Builder,static_stack,m).first;

				llvm::Value* fun2=Builder.CreateBitCast(fun,synt_method_prototype(t));
				args[0]=static_stack_pop(Builder,static_stack,m).first;
				Builder.CreateCall(fun2,args.begin(),args.end());*/

				break;
			}
			case 0x47:
			{
				//returnvoid
				cout << "synt returnvoid" << endl;
				Builder.CreateRetVoid();
				break;
			}
			case 0x48:
			{
				//returnvalue
				//TODO: Should coerce the return type to the expected one
				cout << "synt returnvalue" << endl;
				stack_entry e=static_stack_pop(Builder,static_stack,m);
				Builder.CreateRet(e.first);
				break;
			}
			case 0x49:
			{
				//constructsuper
				cout << "synt constructsuper" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("constructSuper"), th, constant);
				break;
			}
			case 0x4a:
			{
				//constructprop
				cout << "synt constructprop" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				code >> t;
				constant2 = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall3(ex->FindFunctionNamed("constructProp"), th, constant, constant2);
				break;
			}
			case 0x4f:
			{
				//callpropvoid
				cout << "synt callpropvoid" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				code >> t;
				constant2 = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall3(ex->FindFunctionNamed("callPropVoid"), th, constant, constant2);
				break;
			}
			case 0x55:
			{
				//newobject
				cout << "synt newobject" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("newObject"), th, constant);
				break;
			}
			case 0x56:
			{
				//newarray
				cout << "synt newarray" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("newArray"), th, constant);
				break;
			}
			case 0x57:
			{
				//newactivation
				cout << "synt newactivation" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("newActivation"), th);
				break;
			}
			case 0x58:
			{
				//newclass
				cout << "synt newclass" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("newClass"), th, constant);
				break;
			}
			case 0x5a:
			{
				//newcatch
				cout << "synt newcatch" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("newCatch"), th, constant);
				break;
			}
			case 0x5d:
			{
				//findpropstrict
				cout << "synt findpropstrict" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("findPropStrict"), th, constant);
				break;
			}
			case 0x5e:
			{
				//findproperty
				cout << "synt findproperty" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("findProperty"), th, constant);
				break;
			}
			case 0x60:
			{
				//getlex
				cout << "synt getlex" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("getLex"), th, constant);
				break;
			}
			case 0x61:
			{
				//setproperty
				cout << "synt setproperty" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("setProperty"), th, constant);
				break;
			}
			case 0x62:
			{
				//getlocal
				cout << "synt getlocal" << endl;
				u30 i;
				code >> i;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), i);
				Builder.CreateCall2(ex->FindFunctionNamed("getLocal"), th, constant);
				llvm::Value* t=Builder.CreateGEP(locals,constant);
				static_stack_push(static_stack,stack_entry(Builder.CreateLoad(t,"stack"),STACK_OBJECT));
				jitted=true;
				break;
			}
			case 0x63:
			{
				//setlocal
				cout << "synt setlocal" << endl;
				u30 i;
				code >> i;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), i);
				Builder.CreateCall2(ex->FindFunctionNamed("setLocal"), th, constant);
				llvm::Value* t=Builder.CreateGEP(locals,constant);
				stack_entry e=static_stack_pop(Builder,static_stack,m);
				if(e.second!=STACK_OBJECT)
					LOG(ERROR,"conversion not yet implemented");
				Builder.CreateStore(e.first,t);
				jitted=true;
				break;
			}
			case 0x65:
			{
				//getscopeobject
				cout << "synt getscopeobject" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("getScopeObject"), th, constant);
				break;
			}
			case 0x66:
			{
				//getproperty
				cout << "synt getproperty" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("getProperty"), th, constant);
				break;
			}
			case 0x68:
			{
				//initproperty
				cout << "synt initproperty" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("initProperty"), th, constant);
				break;
			}
			case 0x6c:
			{
				//getslot
				cout << "synt getslot" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("getSlot"), th, constant);
				break;
			}
			case 0x6d:
			{
				//setslot
				cout << "synt setslot" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("setSlot"), th, constant);
				break;
			}
			case 0x73:
			{
				//convert_i
				cout << "synt convert_i" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("convert_i"), th);
				break;
			}
			case 0x80:
			{
				//corce
				cout << "synt coerce" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("coerce"), th, constant);
				break;
			}
			case 0x85:
			{
				//coerce_s
				cout << "synt coerce_s" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("coerce_s"), th);
				break;
			}
			case 0x87:
			{
				//astypelate
				cout << "synt astypelate" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("asTypelate"), th);
				break;
			}
			case 0xa0:
			{
				//add
				cout << "synt add" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				Builder.CreateCall(ex->FindFunctionNamed("add"), th);
				break;
			}
			case 0xc2:
			{
				//inclocal_i
				cout << "synt inclocal_i" << endl;
				syncStacks(Builder,jitted,static_stack,m);
				jitted=false;
				u30 t;
				code >> t;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), t);
				Builder.CreateCall2(ex->FindFunctionNamed("incLocal_i"), th, constant);
				break;
			}
			case 0xd0:
			case 0xd1:
			case 0xd2:
			case 0xd3:
			{
				//getlocal_n
				cout << "synt getlocal" << endl;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), opcode&3);
				Builder.CreateCall2(ex->FindFunctionNamed("getLocal"), th, constant);
				llvm::Value* t=Builder.CreateGEP(locals,constant);
				static_stack_push(static_stack,stack_entry(Builder.CreateLoad(t,"stack"),STACK_OBJECT));
				jitted=true;

				break;
			}
			case 0xd5:
			case 0xd6:
			case 0xd7:
			{
				//setlocal_n
				cout << "synt setlocal" << endl;
				constant = llvm::ConstantInt::get(llvm::IntegerType::get(32), opcode&3);
				Builder.CreateCall2(ex->FindFunctionNamed("setLocal"), th, constant);
				llvm::Value* t=Builder.CreateGEP(locals,constant);
				stack_entry e=static_stack_pop(Builder,static_stack,m);
				if(e.second!=STACK_OBJECT)
					LOG(ERROR,"conversion not yet implemented");
				Builder.CreateStore(e.first,t);
				jitted=true;
				break;
			}
			default:
				LOG(ERROR,"Not implemented instruction @" << code.tellg());
				u8 a,b,c;
				code >> a >> b >> c;
				LOG(ERROR,"dump " << hex << (unsigned int)opcode << ' ' << (unsigned int)a << ' ' 
						<< (unsigned int)b << ' ' << (unsigned int)c);
				Builder.CreateRetVoid();
				return m->f;
		}
	}
	return m->f;
}

void ABCVm::Run()
{
	return;
	module=new llvm::Module("abc jit");
	if(!ex)
		ex=llvm::ExecutionEngine::create(module);

	registerFunctions();
	registerClasses();
	//Set register 0 to Global
	//registers[0]=SWFObject(&Global);
	//Take each script entry and run it
	for(int i=0;i<scripts.size();i++)
	{
	//	if(i>0)
	//		break;
		cout << "Script N: " << i << endl;
	/*	for(int j=0;j<scripts[i].trait_count;j++)
			printTrait(&scripts[i].traits[j]);*/
		method_info* m=get_method(scripts[i].init);
		cout << "Building entry script traits: " << scripts[i].trait_count << endl;
		for(int j=0;j<scripts[i].trait_count;j++)
			buildTrait(&Global,&scripts[i].traits[j]);
		//printMethod(m);
		synt_method(m);

		if(m->f)
		{
			Function::as_function FP=(Function::as_function)ex->getPointerToFunction(m->f);
			FP(&Global,NULL);
		}
	}
	//module.dump();
}

string ABCVm::getString(unsigned int s) const
{
	if(s)
		return constant_pool.strings[s];
	else
		return "";
}

void ABCVm::buildTrait(ISWFObject* obj, const traits_info* t)
{
	string name=getMultinameString(t->name);
	switch(t->kind)
	{
		case traits_info::Class:
		{
		//	LOG(CALLS,"Registering trait " << name);
			obj->setVariableByName(name, buildClass(t->classi));
			break;
		}
		case traits_info::Method:
		{
			LOG(NOT_IMPLEMENTED,"Method trait: " << name);
			//syntetize method and create a new LLVM function object
			method_info* m=&methods[t->method];
			llvm::Function* f=synt_method(m);
			Function::as_function f2=(Function::as_function)ex->getPointerToFunction(f);
			obj->setVariableByName(name, new Function(f2));
			break;
		}
		case traits_info::Slot:
		{
			if(t->vindex)
			{
				switch(t->vkind)
				{
					case 0x0c: //Null
					{
						if(!t->slot_id)
						{
							LOG(ERROR,"Should assign slot position");
							abort();
						}
						ISWFObject* ret=obj->setVariableByName(name, new Null);
						obj->setSlot(t->slot_id, ret);
						break;
					}
					default:
					{
						//fallthrough
						LOG(ERROR,"Slot kind " << hex << t->vkind);
						LOG(ERROR,"Trait not supported " << name << " " << t->kind);
						obj->setVariableByName(name, new Undefined);
						return;
					}
				}
			}
			else
			{
				//else fallthrough
				LOG(CALLS,"Slot vindex 0 "<<name<<" type "<<getMultinameString(t->type_name));
			//ISWFObject* ret=obj->setVariableByName(name, buildNamedClass(getMultinameString(t->type_name)));
				ISWFObject* ret=obj->setVariableByName(name, new ASObject);
				obj->setSlot(t->slot_id, ret);
				break;
			}
		}
		default:
			LOG(ERROR,"Trait not supported " << name << " " << t->kind);
			obj->setVariableByName(name, new Undefined);
	}
}

void ABCVm::printTrait(const traits_info* t) const
{
	printMultiname(t->name);
	switch(t->kind&0xf)
	{
		case traits_info::Slot:
			LOG(CALLS,"Slot trait");
			LOG(CALLS,"id: " << t->slot_id << " vindex " << t->vindex << " vkind " << t->vkind);
			break;
		case traits_info::Method:
			LOG(CALLS,"Method trait");
			LOG(CALLS,"method: " << t->method);
			break;
		case traits_info::Getter:
			LOG(CALLS,"Getter trait");
			LOG(CALLS,"method: " << t->method);
			break;
		case traits_info::Setter:
			LOG(CALLS,"Setter trait");
			LOG(CALLS,"method: " << t->method);
			break;
		case traits_info::Class:
			LOG(CALLS,"Class trait: slot "<< t->slot_id);
			printClass(t->classi);
			break;
		case traits_info::Function:
			LOG(CALLS,"Function trait");
			break;
		case traits_info::Const:
			LOG(CALLS,"Const trait");
			break;
	}
}

void ABCVm::printMultiname(int mi) const
{
	if(mi==0)
	{
		LOG(CALLS, "Any (*)");
		return;
	}
	const multiname_info* m=&constant_pool.multinames[mi];
//	LOG(CALLS, "NameID: " << m->name );
	switch(m->kind)
	{
		case 0x07:
			LOG(CALLS, "QName: " << getString(m->name) );
			printNamespace(m->ns);
			break;
		case 0x0d:
			LOG(CALLS, "QNameA");
			break;
		case 0x0f:
			LOG(CALLS, "RTQName");
			break;
		case 0x10:
			LOG(CALLS, "RTQNameA");
			break;
		case 0x11:
			LOG(CALLS, "RTQNameL");
			break;
		case 0x12:
			LOG(CALLS, "RTQNameLA");
			break;
		case 0x09:
		{
			LOG(CALLS, "Multiname: " << getString(m->name));
			const ns_set_info* s=&constant_pool.ns_sets[m->ns_set];
			printNamespaceSet(s);
			break;
		}
		case 0x0e:
			LOG(CALLS, "MultinameA");
			break;
		case 0x1b:
			LOG(CALLS, "MultinameL");
			break;
		case 0x1c:
			LOG(CALLS, "MultinameLA");
			break;
	}
}

void ABCVm::printNamespace(int n) const
{
	if(n==0)
	{
		LOG(CALLS,"Any (*)");
		return;
	}

	const namespace_info* m=&constant_pool.namespaces[n];
	switch(m->kind)
	{
		case 0x08:
			LOG(CALLS, "Namespace " << getString(m->name));
			break;
		case 0x16:
			LOG(CALLS, "PackageNamespace " << getString(m->name));
			break;
		case 0x17:
			LOG(CALLS, "PackageInternalNs " << getString(m->name));
			break;
		case 0x18:
			LOG(CALLS, "ProtectedNamespace " << getString(m->name));
			break;
		case 0x19:
			LOG(CALLS, "ExplicitNamespace " << getString(m->name));
			break;
		case 0x1a:
			LOG(CALLS, "StaticProtectedNamespace " << getString(m->name));
			break;
		case 0x05:
			LOG(CALLS, "PrivateNamespace " << getString(m->name));
			break;
	}
}

void ABCVm::printNamespaceSet(const ns_set_info* m) const
{
	for(int i=0;i<m->count;i++)
	{
		printNamespace(m->ns[i]);
	}
}

SWFObject ABCVm::buildClass(int m) 
{
	const class_info* c=&classes[m];
	const instance_info* i=&instances[m];
	string name=getString(constant_pool.multinames[i->name].name);

	if(c->trait_count)
		LOG(NOT_IMPLEMENTED,"Should add class traits for " << name);

/*	//Run class initialization
	method_info* mi=get_method(c->cinit);
	synt_method(mi);
	void* f_ptr=ex->getPointerToFunction(mi->f);
	void (*FP)() = (void (*)())f_ptr;
	LOG(CALLS,"Class init lenght" << mi->body->code_length);
	FP();*/

//	LOG(CALLS,"Building class " << name);
	if(i->supername)
	{
		string super=getString(constant_pool.multinames[i->supername].name);
//		LOG(NOT_IMPLEMENTED,"Inheritance not supported: super " << super);
	}
	if(i->trait_count)
	{
//		LOG(NOT_IMPLEMENTED,"Should add instance traits");
/*		for(int j=0;j<i->trait_count;j++)
			printTrait(&i->traits[j]);*/
	}
/*	//Run instance initialization
	method_info* mi=get_method(i->init);
	if(synt_method(mi))
	{
		void* f_ptr=ex->getPointerToFunction(mi->f);
		void (*FP)() = (void (*)())f_ptr;
		LOG(CALLS,"instance init lenght" << mi->body->code_length);
		FP();
	}*/
	return new ASObject();
}

void ABCVm::printClass(int m) const
{
	const instance_info* i=&instances[m];
	LOG(CALLS,"Class name: " << getMultinameString(i->name));
	LOG(CALLS,"Class supername:");
	printMultiname(i->supername);
	LOG(CALLS,"Flags " <<hex << i->flags);
	LOG(CALLS,"Instance traits n: " <<i->trait_count);
	for(int j=0;j<i->trait_count;j++)
		printTrait(&i->traits[j]);
	LOG(CALLS,"Instance init");
	const method_info* mi=&methods.at(i->init);
	printMethod(mi);

	const class_info* c=&classes[m];
	LOG(CALLS,"Class traits n: " <<c->trait_count);
	for(int j=0;j<c->trait_count;j++)
		printTrait(&c->traits[j]);
	LOG(CALLS,"Class init");
	mi=&methods[c->cinit];
	printMethod(mi);
}

void ABCVm::printMethod(const method_info* m) const
{
	string n=getString(m->name);
	LOG(CALLS,"Method " << n);
	LOG(CALLS,"Params n: " << m->param_count);
	LOG(CALLS,"Return " << m->return_type);
	LOG(CALLS,"Flags " << m->flags);
	if(m->body)
		LOG(CALLS,"Body Lenght " << m->body->code_length)
	else
		LOG(CALLS,"No Body")
}

istream& operator>>(istream& in, u32& v)
{
	int i=0;
	uint8_t t,t2;
	v.val=0;
	do
	{
		in.read((char*)&t,1);
		t2=t;
		t&=0x7f;

		v.val|=(t<<i);
		i+=7;
		if(i==35)
		{
			if(t>15)
				LOG(ERROR,"parsing u32");
			break;
		}
	}
	while(t2&0x80);
	return in;
}

istream& operator>>(istream& in, s32& v)
{
	int i=0;
	uint8_t t,t2;
	v.val=0;
	do
	{
		in.read((char*)&t,1);
		t2=t;
		t&=0x7f;

		v.val|=(t<<i);
		i+=7;
		if(i==35)
		{
			if(t>15)
				LOG(ERROR,"parsing s32");
			break;
		}
	}
	while(t2&0x80);
	if(t2&0x40)
	{
		//Sign extend
		for(i;i<32;i++)
			v.val|=(1<<i);
	}
	return in;
}

istream& operator>>(istream& in, s24& v)
{
	int i=0;
	v.val=0;
	uint8_t t;
	for(i=0;i<24;i+=8)
	{
		in.read((char*)&t,1);
		v.val|=(t<<i);
	}

	if(t&0x80)
	{
		//Sign extend
		for(i;i<32;i++)
			v.val|=(1<<i);
	}
	return in;
}

istream& operator>>(istream& in, u30& v)
{
	int i=0;
	uint8_t t,t2;
	v.val=0;
	do
	{
		in.read((char*)&t,1);
		t2=t;
		t&=0x7f;

		v.val|=(t<<i);
		i+=7;
		if(i>29)
			LOG(ERROR,"parsing u30");
	}
	while(t2&0x80);
	if(v.val&0xc0000000)
			LOG(ERROR,"parsing u30");
	return in;
}

istream& operator>>(istream& in, u8& v)
{
	uint8_t t;
	in.read((char*)&t,1);
	v.val=t;
	return in;
}

istream& operator>>(istream& in, u16& v)
{
	uint16_t t;
	in.read((char*)&t,2);
	v.val=t;
	return in;
}

istream& operator>>(istream& in, d64& v)
{
	//Should check if this is right
	in.read((char*)&v.val,8);
	return in;
}

istream& operator>>(istream& in, string_info& v)
{
	in >> v.size;
	//TODO: String are expected to be UTF-8 encoded.
	//This temporary implementation assume ASCII, so fail if high bit is set
	uint8_t t;
	v.val.reserve(v.size);
	for(int i=0;i<v.size;i++)
	{
		in.read((char*)&t,1);
		v.val.push_back(t);
		if(t&0x80)
			LOG(NOT_IMPLEMENTED,"Multibyte not handled");
	}
	return in;
}

istream& operator>>(istream& in, namespace_info& v)
{
	in >> v.kind >> v.name;
	if(v.kind!=0x05 && v.kind!=0x08 && v.kind!=0x16 && v.kind!=0x17 && v.kind!=0x18 && v.kind!=0x19 && v.kind!=0x1a)
		LOG(ERROR,"Unexpected namespace kind");
	return in;
}

istream& operator>>(istream& in, method_body_info& v)
{
	in >> v.method >> v.max_stack >> v.local_count >> v.init_scope_depth >> v.max_scope_depth >> v.code_length;
	v.code.resize(v.code_length);
	for(int i=0;i<v.code_length;i++)
		in.read(&v.code[i],1);

	in >> v.exception_count;
	v.exceptions.resize(v.exception_count);
	for(int i=0;i<v.exception_count;i++)
		in >> v.exceptions[i];

	in >> v.trait_count;
	v.traits.resize(v.trait_count);
	for(int i=0;i<v.trait_count;i++)
		in >> v.traits[i];
	return in;
}

istream& operator>>(istream& in, ns_set_info& v)
{
	in >> v.count;

	v.ns.resize(v.count);
	for(int i=0;i<v.count;i++)
	{
		in >> v.ns[i];
		if(v.ns[i]==0)
			LOG(ERROR,"0 not allowed");
	}
	return in;
}

istream& operator>>(istream& in, multiname_info& v)
{
	in >> v.kind;

	switch(v.kind)
	{
		case 0x07:
		case 0x0d:
			in >> v.ns >> v.name;
			break;
		case 0x0f:
		case 0x10:
			in >> v.name;
			break;
		case 0x11:
		case 0x12:
			break;
		case 0x09:
		case 0x0e:
			in >> v.name >> v.ns_set;
			break;
		case 0x1b:
		case 0x1c:
			in >> v.ns_set;
			break;
		default:
			LOG(ERROR,"Unexpected multiname kind");
			break;
	}
	return in;
}

istream& operator>>(istream& in, method_info& v)
{
	in >> v.param_count;
	in >> v.return_type;

	v.param_type.resize(v.param_count);
	for(int i=0;i<v.param_count;i++)
		in >> v.param_type[i];
	
	in >> v.name >> v.flags;
	if(v.flags&0x08)
	{
		in >> v.option_count;
		v.options.resize(v.option_count);
		for(int i=0;i<v.option_count;i++)
		{
			in >> v.options[i].val >> v.options[i].kind;
			if(v.options[i].kind>0x1a)
				LOG(ERROR,"Unexpected options type");
		}
	}
	if(v.flags&0x80)
	{
		LOG(ERROR,"Params names not supported");
		abort();
	}
	return in;
}

istream& operator>>(istream& in, script_info& v)
{
	in >> v.init >> v.trait_count;
	v.traits.resize(v.trait_count);
	for(int i=0;i<v.trait_count;i++)
		in >> v.traits[i];
	return in;
}

istream& operator>>(istream& in, class_info& v)
{
	in >> v.cinit >> v.trait_count;
	v.traits.resize(v.trait_count);
	for(int i=0;i<v.trait_count;i++)
		in >> v.traits[i];
	return in;
}

istream& operator>>(istream& in, metadata_info& v)
{
	in >> v.name;
	in >> v.item_count;

	v.items.resize(v.item_count);
	for(int i=0;i<v.item_count;i++)
	{
		in >> v.items[i].key >> v.items[i].value;
	}
	return in;
}

istream& operator>>(istream& in, traits_info& v)
{
	in >> v.name >> v.kind;
	switch(v.kind&0xf)
	{
		case traits_info::Slot:
		case traits_info::Const:
			in >> v.slot_id >> v.type_name >> v.vindex;
			if(v.vindex)
				in >> v.vkind;
			break;
		case traits_info::Class:
			in >> v.slot_id >> v.classi;
			break;
		case traits_info::Function:
			in >> v.slot_id >> v.function;
			break;
		case traits_info::Getter:
		case traits_info::Setter:
		case traits_info::Method:
			in >> v.disp_id >> v.method;
			break;
		default:
			LOG(ERROR,"Unexpected kind " << v.kind);
			break;
	}

	if(v.kind&traits_info::Metadata)
	{
		in >> v.metadata_count;
		v.metadata.resize(v.metadata_count);
		for(int i=0;i<v.metadata_count;i++)
			in >> v.metadata[i];
	}
	return in;
}

istream& operator>>(istream& in, exception_info& v)
{
	in >> v.from >> v.to >> v.target >> v.exc_type >> v.var_name;
	return in;
}

istream& operator>>(istream& in, instance_info& v)
{
	in >> v.name >> v.supername >> v.flags;
	if(v.flags&instance_info::ClassProtectedNs)
		in >> v.protectedNs;

	in >> v.interface_count;
	v.interfaces.resize(v.interface_count);
	for(int i=0;i<v.interface_count;i++)
	{
		in >> v.interfaces[i];
		if(v.interfaces[i]==0)
			abort();
	}

	in >> v.init;

	in >> v.trait_count;
	v.traits.resize(v.trait_count);
	for(int i=0;i<v.trait_count;i++)
		in >> v.traits[i];
	return in;
}

istream& operator>>(istream& in, cpool_info& v)
{
	in >> v.int_count;
	v.integer.resize(v.int_count);
	for(int i=1;i<v.int_count;i++)
		in >> v.integer[i];

	in >> v.uint_count;
	v.uinteger.resize(v.uint_count);
	for(int i=1;i<v.uint_count;i++)
		in >> v.uinteger[i];

	in >> v.double_count;
	v.doubles.resize(v.double_count);
	for(int i=1;i<v.double_count;i++)
		in >> v.doubles[i];

	in >> v.string_count;
	v.strings.resize(v.string_count);
	for(int i=1;i<v.string_count;i++)
		in >> v.strings[i];

	in >> v.namespace_count;
	v.namespaces.resize(v.namespace_count);
	for(int i=1;i<v.namespace_count;i++)
		in >> v.namespaces[i];

	in >> v.ns_set_count;
	v.ns_sets.resize(v.ns_set_count);
	for(int i=1;i<v.ns_set_count;i++)
		in >> v.ns_sets[i];

	in >> v.multiname_count;
	v.multinames.resize(v.multiname_count);
	for(int i=1;i<v.multiname_count;i++)
		in >> v.multinames[i];

	return in;
}

ISWFObject* parseInt(ISWFObject* obj,arguments* args)
{
	return new Integer(0);
}
