#pragma once
#include "stdafx.h"

using namespace v8;

Platform* gPlatform = nullptr;
Isolate* gIsolate = nullptr;
Isolate::Scope* gIsolateScope = nullptr;

PersistentHandleWrapper<Context>* gContext = nullptr;

class _NavObject : public ObjectWrap { };

template<typename T, bool IsNavObject>
struct _NavNew {};

template<typename T>
struct _NavNew <T, false> {
	typedef Handle<T> Type;
	static Handle<T> New() {
		return T::New(v8::Isolate::GetCurrent());
	}

	template<typename... Values>
	static Handle<T> New(Values... Args) {
		return T::New(v8::Isolate::GetCurrent(), Args...);
	}
};

template<typename T>
struct _NavNew<T, true> {
	typedef Handle<Object> Type;
	static Handle<Object> New() {
		return NavObjectWrap<T>::New(v8::Isolate::GetCurrent());
	}

	template<typename... Values>
	static Handle<Object> New(Values... Args) {
		return NavObjectWrap<T>::New(v8::Isolate::GetCurrent(), Args...);
	}
};

template<typename T>
typename _NavNew<T, std::is_base_of<_NavObject, T>::value>::Type NavNew() {
	return _NavNew<T, std::is_base_of<_NavObject, T>::value>::New();
}

template<typename T, typename... Values>
typename _NavNew<T, std::is_base_of<_NavObject, T>::value>::Type NavNew(Values... Args) {
	return _NavNew<T, std::is_base_of<_NavObject, T>::value>::New(Args...);
}

template<>
Handle<String> NavNew<String>() {
	return String::Empty(v8::Isolate::GetCurrent());
}

template<>
Handle<String> NavNew<String>(const char* str) {
	return String::NewFromUtf8(v8::Isolate::GetCurrent(), str);
}

template<size_t N>
Handle<String> NavNew(const char (&str)[N]) {
	return NavNew<String>(str);
}
Handle<String> NavNew(const char* str) {
	return NavNew<String>(str);
}

Handle<Number> NavNew(float value) {
	return NavNew<Number>(value);
}
Handle<Number> NavNew(double value) {
	return NavNew<Number>(value);
}
Handle<Integer> NavNew(int32_t value) {
	return NavNew<Integer>(value);
}
Handle<Integer> NavNew(uint32_t value) {
	return NavNew<Integer>(value);
}

Handle<Value> NavNull() {
	return v8::Null(v8::Isolate::GetCurrent());
}

Handle<Value> NavUndefined() {
	return v8::Undefined(v8::Isolate::GetCurrent());
}

Handle<Object> NavGlobal() {
	return gContext->Extract()->Global();
}

template<typename T, typename... Values>
Handle<T> NavFind(Handle<Object> obj, const char *key, Values... Rest) {
	Handle<Object> nextObj = obj->Get(NavNew<String>(key)).As<Object>();
	return NavFind<T>(nextObj, Rest...);
}

template<typename T>
Handle<T> NavFind(Handle<Object> obj, const char *key) {
	return obj->Get(NavNew<String>(key)).As<T>();
}

template<typename T>
void NavSetObjVal(Handle<Object>& obj, const char* name, T val) {
	obj->Set(NavNew<String>(name), val);
}
template<typename T>
void NavSetObjFunc(Handle<Object>& obj, const char* name, T func) {
	NavSetObjVal(obj, name, FunctionTemplate::New(gIsolate, func)->GetFunction());
}
template<typename T>
void NavSetObjVal(Handle<Object>& obj, const char* name, T val, PropertyAttribute attribs) {
	obj->ForceSet(NavNew<String>(name), val, attribs);
}
template<typename T>
void NavSetObjFunc(Handle<Object>& obj, const char* name, T func, PropertyAttribute attribs) {
	NavSetObjVal(obj, name, FunctionTemplate::New(gIsolate, func)->GetFunction(), attribs);
}
template<typename T>
void NavSetObjEnumVal(Handle<Object>& obj, const char* name, T val) {
	obj->Set(NavNew(name), NavNew((std::underlying_type<T>::type)val));
}
template<typename T>
void NavSetProtoVal(Handle<FunctionTemplate>& tpl, const char *name, T val) {
	tpl->PrototypeTemplate()->Set(NavNew<String>(name), val);
}
template<typename T>
void NavSetProtoFunc(Handle<FunctionTemplate>& tpl, const char *name, T func) {
	NavSetProtoVal(tpl, name, FunctionTemplate::New(gIsolate, func));
}

template<typename T>
T* NavUnwrap(Handle<Value> handle) {
	return ObjectWrap::Unwrap<T>(handle.As<Object>());
}

template<typename T, void(T::*F)(const v8::FunctionCallbackInfo<v8::Value>&)>
static void NavSetProtoMethod(Handle<FunctionTemplate> tpl, const char* name) {
	NavSetProtoFunc(tpl, name,
		[](const v8::FunctionCallbackInfo<v8::Value>& args) {
		(NavUnwrap<T>(args.This())->*F)(args);
	});
}

void NavCopyProps(Handle<Object> dest, Handle<Object> src) {
	EscapableHandleScope handleScope(gIsolate);

	Local<Array> propNames = src->GetOwnPropertyNames();
	for (uint32_t i = 0; i < propNames->Length(); ++i) {
		Handle<Value> propName = propNames->Get(i);
		dest->Set(propName, src->Get(propName));
	}
}

template<typename T>
class SharedV8Ref {
public:
	inline SharedV8Ref()
		: _pointer(nullptr) {
	}

	inline SharedV8Ref(Handle<Value> handle_) {
		Handle<Object> thisObj = handle_.As<Object>();
		_persistent = PersistentHandleWrapper<Object>(gIsolate, thisObj);
		_pointer = NavUnwrap<T>(handle_);
	}

	inline ~SharedV8Ref() {
		_persistent = PersistentHandleWrapper<Object>();
		_pointer = nullptr;
	}

	inline v8::Local<Object> handle() {
		return _persistent.Extract();
	}

	inline T& operator*() {
		return *_pointer;
	}

	inline T* operator->() const {
		return _pointer;
	}

	inline operator T*() const {
		return _pointer;
	}

	inline operator bool() const {
		return _pointer != nullptr;
	}

private:
	T* _pointer;
	PersistentHandleWrapper<Object> _persistent;

};
template<typename T>
void NavSetObjVal(Handle<Object>& obj, const char* name, SharedV8Ref<T> val) {
	NavSetObjVal(obj, name, val.handle());
}

template<typename T>
class NavObject : public _NavObject {
public:
	typedef T BaseType;
	T* _data;
};

template<class T>
class NavObjectWrap {
public:
	static_assert(std::is_base_of<_NavObject, T>::value, "NavObjectWrap must be used only on NavObject's");

	static Local<Function> Constructor() {
		return _constructor.Extract();
	}

	static Local<FunctionTemplate> Template() {
		return _template.Extract();
	}

	static Local<Object> New(Isolate *isolate = nullptr) {
		return Constructor()->NewInstance();
	}

	static void _Construct(const v8::FunctionCallbackInfo<v8::Value>& args) {
		EscapableHandleScope handleScope(gIsolate);
		T *hw = new T();
		hw->_data = T::_NavCreateData();
		hw->constructor(args);
		hw->Wrap(args.This());
		args.GetReturnValue().Set(args.This());
	}

	static void Init(Handle<String> className) {
		Local<FunctionTemplate> t = FunctionTemplate::New(gIsolate, _Construct);
		t->InstanceTemplate()->SetInternalFieldCount(1);
		t->SetClassName(className);
		T::buildPrototype(t);
		_template = PersistentHandleWrapper<FunctionTemplate>(gIsolate, t);
		Local<Function> construct = t->GetFunction();
		_constructor = PersistentHandleWrapper<Function>(gIsolate, construct);
	}

	static void Init(Handle<Object> targetObj, const char *name) {
		auto className = NavNew(name);
		Init(className);
		targetObj->Set(className, Constructor());
	}

	static void Shutdown() {
		_constructor = PersistentHandleWrapper<Function>();
		_template = PersistentHandleWrapper<FunctionTemplate>();
	}

protected:
	static PersistentHandleWrapper<FunctionTemplate> _template;
	static PersistentHandleWrapper<Function> _constructor;

};
template<typename T>
PersistentHandleWrapper<Function> NavObjectWrap<T>::_constructor;
template<typename T>
PersistentHandleWrapper<FunctionTemplate> NavObjectWrap<T>::_template;

// This should probably call cls::Create later.
#define NAV_CLASS_WRAPPER(cls) \
	static_assert(std::is_base_of<BaseType, cls>::value, "Wrapped objects must have a common base class."); \
	static cls* _NavCreateData() { return new cls(); } \
	cls* data() const { return reinterpret_cast<cls*>(_data); } 


#define NAV_JSCLASS_WRAPPER(cls, ...) \
	protected: Handle<Object> _handle; \
	public: cls(Handle<Value> handle) : _handle(handle.As<Object>()) {} \
			static Handle<Object> New() { return NavFind<Function>(NavGlobal(), ##__VA_ARGS__)->NewInstance(); } \
			static void Init() { } \
			static void Shutdown() { }


class NavWatcher {
public:
	void Bind(Handle<Object> obj, const char* propName, std::function<void()> handler) {
		_handler = handler;
		obj->SetAccessor(NavNew(propName), nullptr,
			[](Local<String> property, Local<Value> value, const PropertyCallbackInfo<void>& info)
		{
			NavWatcher *self = (NavWatcher*)info.Data().As<External>()->Value();
			self->_handler();
		}, NavNew<External>(this));
	}

private:
	std::function<void()> _handler;
};

template<typename T, typename V, typename R, R(Value::*F)() const>
class NavBinder {
public:
	void Bind(Handle<Object> obj, const char* propName, T* value, std::function<void()> handler=nullptr) {
		_value = value;
		_handler = handler;
		obj->SetAccessor(NavNew(propName), [](Local<String> property, const PropertyCallbackInfo<Value>& info) {
			NavBinder *self = (NavBinder*)info.Data().As<External>()->Value();
			info.GetReturnValue().Set(NavNew<V>(*self->_value));
		}, [](Local<String> property, Local<Value> value, const PropertyCallbackInfo<void>& info) {
			NavBinder *self = (NavBinder*)info.Data().As<External>()->Value();
			self->_Changed((T)((*value)->*F)());
		}, NavNew<External>(this));
	}

private:
	T* _value;
	std::function<void()> _handler;
	void _Changed(T value) {
		*_value = value;
		if (_handler) {
			_handler();
		}
	}
};
typedef NavBinder<float, Number, double, &Value::NumberValue> FloatBinder;
typedef NavBinder<bool, Boolean, bool, &Value::BooleanValue> BoolBinder;
typedef NavBinder<int32_t, Int32, int32_t, &Value::Int32Value> Int32Binder;
