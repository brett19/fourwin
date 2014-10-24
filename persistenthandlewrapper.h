#pragma once

template<typename T>
class PersistentHandleWrapper {
public:
	inline PersistentHandleWrapper()
		: _isolate(nullptr) { }

	inline PersistentHandleWrapper(v8::Isolate* isolate, v8::Local<T> value)
		: _isolate(isolate),
		_value(isolate, value) { }

	inline ~PersistentHandleWrapper() {
		Dispose();
	}

	inline PersistentHandleWrapper(PersistentHandleWrapper&& other) {
		_value = other._value;
		_isolate = other._isolate;
		other.Dispose();
	}

	inline PersistentHandleWrapper(PersistentHandleWrapper const& other) {
		_value = other._value;
		_isolate = other._isolate;
	}

	inline PersistentHandleWrapper& operator=(PersistentHandleWrapper&& other) {
		if (this == &other) return *this;
		_value = other._value;
		_isolate = other._isolate;
		return *this;
	}

	inline PersistentHandleWrapper& operator=(PersistentHandleWrapper const& other) {
		if (this == &other) return *this;
		_value = other._value;
		_isolate = other._isolate;
		return *this;
	}

	template <typename ...Args> inline void MakeWeak(Args&& ...args) {
		_value.SetWeak(std::forward<Args>(args)...);
	}

	inline void ClearWeak() {
		_value.ClearWeak();
	}

	inline bool IsEmpty() {
		return _value.IsEmpty();
	}

	inline v8::Local<T> Extract() {
		v8::EscapableHandleScope scope(_isolate);
		return scope.Escape(v8::Local<T>::New(_isolate, _value));
	}

	inline v8::Isolate* GetIsolate() {
		return _isolate;
	}

private:

	inline void Dispose() {
		_isolate = nullptr;
	}

	v8::Isolate* _isolate;
	v8::Persistent<T, v8::CopyablePersistentTraits<T>> _value;
};