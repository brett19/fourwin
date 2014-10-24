#include "stdafx.h"
#include "Four.h"
#include "nav.h"
#include "math.h"
#include "gfx.h"

using namespace v8; 

std::vector<PersistentHandleWrapper<Function>> gAnimCallbacks;

namespace js {
	namespace Console {
		void write(const v8::FunctionCallbackInfo<v8::Value>& args) {
			v8::HandleScope handle_scope(args.GetIsolate());
			if (args.Length() >= 1) {
				v8::String::Utf8Value str(args[0]);
				printf("%s\n", *str);
				fflush(stdout);
			}
		}

		void Init(Handle<Object> targetObj) {
			Local<Object> consoleObj = Object::New(gIsolate);
			NavSetObjFunc(consoleObj, "write", write);
			NavSetObjVal(targetObj, "console", consoleObj);
		}
	}

	namespace FOUR {
		class Vector3Wrap {
			NAV_JSCLASS_WRAPPER(Vector3Wrap, "FOUR", "Vector3");
		public:
			typedef math::Vector3 BaseType;

			operator math::Vector3() {
				return math::Vector3(
					(float)_handle->Get(NavNew<String>("x"))->NumberValue(),
					(float)_handle->Get(NavNew<String>("y"))->NumberValue(),
					(float)_handle->Get(NavNew<String>("z"))->NumberValue()
					);
			}

			Vector3Wrap& operator=(const math::Vector3& v) {
				_handle->Set(NavNew<String>("x"), NavNew<Number>(v.x()));
				_handle->Set(NavNew<String>("y"), NavNew<Number>(v.y()));
				_handle->Set(NavNew<String>("z"), NavNew<Number>(v.z()));
				return *this;
			}

		};

		class Vector3Binder {
		public:
			void Bind(Handle<Object> baseObj, const char* propName, math::Vector3& value, std::function<void()> handler) {
				Handle<Object> obj = Vector3Wrap::New();
				NavSetObjVal(baseObj, propName, obj, v8::ReadOnly);
				_xBind.Bind(obj, "x", &value.x(), handler);
				_yBind.Bind(obj, "y", &value.y(), handler);
				_zBind.Bind(obj, "z", &value.z(), handler);
			}

		private:
			FloatBinder _xBind;
			FloatBinder _yBind;
			FloatBinder _zBind;

		};

		class QuaternionWrap {
			NAV_JSCLASS_WRAPPER(QuaternionWrap, "FOUR", "Quaternion");
		public:
			typedef math::Quaternion BaseType;

			operator math::Quaternion() {
				return math::Quaternion(
					(float)_handle->Get(NavNew<String>("x"))->NumberValue(),
					(float)_handle->Get(NavNew<String>("y"))->NumberValue(),
					(float)_handle->Get(NavNew<String>("z"))->NumberValue(),
					(float)_handle->Get(NavNew<String>("w"))->NumberValue()
				);
			}

			QuaternionWrap& operator=(const math::Quaternion& v) {
				_handle->Set(NavNew<String>("x"), NavNew<Number>(v.x()));
				_handle->Set(NavNew<String>("y"), NavNew<Number>(v.y()));
				_handle->Set(NavNew<String>("z"), NavNew<Number>(v.z()));
				_handle->Set(NavNew<String>("w"), NavNew<Number>(v.w()));
				return *this;
			}

		};

		class QuaternionBinder {
		public:
			void Bind(Handle<Object> baseObj, const char* propName, math::Quaternion& value, std::function<void()> handler) {
				Handle<Object> obj = QuaternionWrap::New();
				NavSetObjVal(baseObj, propName, obj, v8::ReadOnly);
				_xBind.Bind(obj, "x", &value.x(), handler);
				_yBind.Bind(obj, "y", &value.y(), handler);
				_zBind.Bind(obj, "z", &value.z(), handler);
				_wBind.Bind(obj, "w", &value.w(), handler);
			}

		private:
			FloatBinder _xBind;
			FloatBinder _yBind;
			FloatBinder _zBind;
			FloatBinder _wBind;

		};

		class BufferAttribute : public NavObject<gfx::BufferAttribute> {
		public:
			NAV_CLASS_WRAPPER(gfx::BufferAttribute)

			static void buildPrototype(Local<FunctionTemplate> tpl) {
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^BufferAttribute\n");

				if (args.Length() < 2) {
					return;
				}

				args.This()->Set(NavNew("data"), args[0]);
				args.This()->Set(NavNew("itemSize"), args[1]);
				updateWatch.Bind(args.This(), "needsUpdate", std::bind(&BufferAttribute::update, this));
			}

			void update() {
				Handle<TypedArray> dataObj = handle()->Get(NavNew("data")).As<TypedArray>();
				size_t dataLen = dataObj->ByteLength();
				data()->_data.resize(dataLen);
				uint8_t *dataBuf = (uint8_t*)dataObj->Buffer()->BaseAddress();
				dataBuf += dataObj->ByteOffset();
				memcpy(&data()->_data[0], dataBuf, dataLen);
				data()->_itemSize = handle()->Get(NavNew("itemSize"))->Int32Value();
				if (dataObj->IsFloat32Array()) {
					data()->_itemType = gfx::BufferType::Float;
				} else if (dataObj->IsInt8Array()) {
					data()->_itemType = gfx::BufferType::Byte;
				} else if (dataObj->IsUint8Array()) {
					data()->_itemType = gfx::BufferType::UnsignedByte;
				} else if (dataObj->IsInt16Array()) {
					data()->_itemType = gfx::BufferType::Short;
				} else if (dataObj->IsUint16Array()) {
					data()->_itemType = gfx::BufferType::UnsignedShort;
				} else if (dataObj->IsInt32Array()) {
					data()->_itemType = gfx::BufferType::Int;
				} else if (dataObj->IsUint32Array()) {
					data()->_itemType = gfx::BufferType::UnsignedInt;
				}
				data()->_needsUpdate = true;
			}

			NavWatcher updateWatch;

		};

		class Object3d : public NavObject<gfx::Object3d> {
		public:
			NAV_CLASS_WRAPPER(gfx::Object3d)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				NavSetProtoMethod<Object3d, &add>(tpl, "add");
				NavSetProtoMethod<Object3d, &remove>(tpl, "remove");
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^Object3d\n");

				NavSetObjVal(args.This(), "name", NavNew<String>());
				_position.Bind(args.This(), "position", data()->_position, std::bind(&Object3d::transformChanged, this));
				_quaternion.Bind(args.This(), "quaternion", data()->_rotation, std::bind(&Object3d::transformChanged, this));
				_scale.Bind(args.This(), "scale", data()->_scale, std::bind(&Object3d::transformChanged, this));
			}

			void transformChanged() {
				data()->_transformNeedsUpdate = true;
			}
			Vector3Binder _position;
			QuaternionBinder _quaternion;
			Vector3Binder _scale;

			void add(const v8::FunctionCallbackInfo<v8::Value>& args) {
				if (args.Length() < 1) {
					return;
				}

				Object3d* child = NavUnwrap<Object3d>(args[0]);
				data()->addChild(child->data());

				args.GetReturnValue().Set(args.This());
			}

			void remove(const v8::FunctionCallbackInfo<v8::Value>& args) {
				if (args.Length() < 1) {
					return;
				}

				Object3d* child = NavUnwrap<Object3d>(args[0]);
				data()->removeChild(child->data());

				args.GetReturnValue().Set(args.This());
			}

		};

		class Camera : public Object3d {
		public:
			NAV_CLASS_WRAPPER(gfx::Camera)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				tpl->Inherit(NavObjectWrap<Object3d>::Template());

				NavSetProtoMethod<Camera, &lookAt>(tpl, "lookAt");
				NavSetProtoMethod<Camera, &setPerspective>(tpl, "setPerspective");
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^Camera\n");

				Object3d::constructor(args);
			}

			void lookAt(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("Camera::lookAt\n");
				if (args.Length() < 3) {
					return;
				}

				math::Vector3 position = Vector3Wrap(args[0]);
				math::Vector3 target = Vector3Wrap(args[1]);
				math::Vector3 up = Vector3Wrap(args[2]);
				data()->lookAt(position, target, up);
			}

			void setPerspective(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("Camera::setPerspective\n");
				if (args.Length() < 4) {
					return;
				}

				float fovY = (float)args[0]->NumberValue();
				float aspect = (float)args[1]->NumberValue();
				float dnear = (float)args[2]->NumberValue();
				float dfar = (float)args[3]->NumberValue();
				data()->setPerspective(fovY, aspect, dnear, dfar);
			}

		};

		class Shader : public NavObject < gfx::Shader > {
		public:
			NAV_CLASS_WRAPPER(gfx::Shader)

				static void buildPrototype(Handle<FunctionTemplate> tpl) {
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^Shader\n");
				if (args.Length() < 1) {
					return;
				}

				Handle<Object> optsObj = args[0].As<Object>();

				Handle<Value> vertexVal = optsObj->Get(NavNew("vertex"));
				if (!vertexVal.IsEmpty()) {
					String::Utf8Value vertexStr(vertexVal);
					data()->vertexSrc = *vertexStr;
				}

				Handle<Value> fragmentVal = optsObj->Get(NavNew("fragment"));
				if (!fragmentVal.IsEmpty()) {
					String::Utf8Value fragmentStr(fragmentVal);
					data()->fragmentSrc = *fragmentStr;
				}

				Handle<Value> uniformsVal = optsObj->Get(NavNew("uniforms"));
				if (!uniformsVal.IsEmpty() && uniformsVal->IsObject()) {
					Handle<Object> uniformsObj = uniformsVal.As<Object>();
					Handle<Array> uniformsKeys = uniformsObj->GetOwnPropertyNames();
					for (size_t i = 0; i < uniformsKeys->Length(); ++i) {
						Handle<Value> uniformNameVal = uniformsKeys->Get(i);
						String::Utf8Value uniformName(uniformNameVal);
						int32_t uniformVal = uniformsObj->Get(uniformNameVal)->Int32Value();
						gfx::Shader::Uniform uniform;
						uniform.name = *uniformName;
						uniform.type = (gfx::UniformType)uniformVal;
						data()->uniforms.emplace_back(uniform);
					}
				}

				Handle<Value> attributesVal = optsObj->Get(NavNew("attributes"));
				if (!attributesVal.IsEmpty() && attributesVal->IsObject()) {
					Handle<Object> attributesObj = attributesVal.As<Object>();
					Handle<Array> attributesKeys = attributesObj->GetOwnPropertyNames();
					for (size_t i = 0; i < attributesKeys->Length(); ++i) {
						Handle<Value> attributeNameVal = attributesKeys->Get(i);
						String::Utf8Value attributeName(attributeNameVal);
						int32_t attributeVal = attributesObj->Get(attributeNameVal)->Int32Value();
						gfx::Shader::Attribute attribute;
						attribute.name = *attributeName;
						attribute.type = (gfx::AttributeType)attributeVal;
						data()->attributes.emplace_back(attribute);
					}
				}

				data()->initVars();
			}

		};

		class ShaderMaterial : public NavObject<gfx::ShaderMaterial> {
		public:
			NAV_CLASS_WRAPPER(gfx::ShaderMaterial)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^ShaderMaterial\n");
				if (args.Length() < 1) {
					return;
				}

				Shader* shader = NavUnwrap<Shader>(args[0]);

				data()->_shader = shader->data();

				_transparentBind.Bind(args.This(), "transparent", &data()->_transparent);
				_depthWriteBind.Bind(args.This(), "depthWrite", &data()->_depthWrite);
				_depthTestBind.Bind(args.This(), "depthTest", &data()->_depthTest);
			}
			BoolBinder _transparentBind;
			BoolBinder _depthWriteBind;
			BoolBinder _depthTestBind;

		};

		class BufferGeometry : public NavObject<gfx::BufferGeometry> {
		public:
			NAV_CLASS_WRAPPER(gfx::BufferGeometry)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				NavSetProtoMethod<BufferGeometry, &setAttribute>(tpl, "setAttribute");
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^BufferGeometry\n");

				
			}

			void setAttribute(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("BufferGeometry::setAttribute\n");
				if (args.Length() < 2) {
					return;
				}

				String::Utf8Value name(args[0]);
				BufferAttribute* attribute = NavUnwrap<BufferAttribute>(args[1]);

				auto existingI = data()->_attributes.emplace(std::string(*name), attribute->data());
				if (!existingI.second) {
					existingI.first->second = attribute->data();
				}
			}

		};

		class Scene : public Object3d {
		public:
			NAV_CLASS_WRAPPER(gfx::Scene)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				tpl->Inherit(NavObjectWrap<Object3d>::Template());
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^Scene\n");

				Object3d::constructor(args);
			}

		};

		class Mesh : public Object3d {
		public:
			NAV_CLASS_WRAPPER(gfx::Mesh)

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				tpl->Inherit(NavObjectWrap<Object3d>::Template());
			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				printf("^Mesh\n");
				Object3d::constructor(args);

				if (args.Length() < 2) {
					return;
				}

				BufferGeometry *geometry = NavObject::Unwrap<BufferGeometry>(args[0].As<Object>());
				ShaderMaterial *material = NavObject::Unwrap<ShaderMaterial>(args[1].As<Object>());
				data()->setGeometry(geometry->data());
				data()->setMaterial(material->data());
			}

		};

		struct _Renderer {};
		class Renderer : public NavObject<_Renderer>{
		public:
			NAV_CLASS_WRAPPER(_Renderer);

			static void buildPrototype(Handle<FunctionTemplate> tpl) {
				NavSetProtoMethod<Renderer, &render>(tpl, "render");
				NavSetProtoMethod<Renderer, &clear>(tpl, "clear");
				NavSetProtoMethod<Renderer, &setClearColor>(tpl, "setClearColor");
				NavSetProtoMethod<Renderer, &test>(tpl, "test");
			}

			void test(const v8::FunctionCallbackInfo<v8::Value>& args) {

			}

			void constructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
			}

			void setClearColor(const v8::FunctionCallbackInfo<v8::Value>& args) {
				float r, g, b, a;
				if (args.Length() >= 1) r = (float)args[0]->NumberValue();
				if (args.Length() >= 2) g = (float)args[1]->NumberValue();
				if (args.Length() >= 3) b = (float)args[2]->NumberValue();
				if (args.Length() >= 4) a = (float)args[3]->NumberValue();
				gfx::Renderer::setClearColor(r, g, b, a);
			}

			void clear(const v8::FunctionCallbackInfo<v8::Value>& args) {
				bool clearColor = false;
				if (args.Length() >= 1 && args[0]->BooleanValue() == true) {
					clearColor = true;
				}
				bool clearDepth = false;
				if (args.Length() >= 2 && args[1]->BooleanValue() == true) {
					clearDepth = true;
				}
				bool clearStencil = false;
				if (args.Length() >= 3 && args[2]->BooleanValue() == true) {
					clearStencil = true;
				}
				gfx::Renderer::clear(clearColor, clearDepth, clearStencil);
			}

			void render(const v8::FunctionCallbackInfo<v8::Value>& args) {
				if (args.Length() < 2) {
					return;
				}

				Scene *scene = NavObject::Unwrap<Scene>(args[0].As<Object>());
				Camera *camera = NavObject::Unwrap<Camera>(args[1].As<Object>());
				gfx::Renderer::render(scene->data(), camera->data());
			}
		};

		void InitConstants(Handle<Object> targetObj) {
			Local<Object> valueObj;
			
			valueObj = NavNew<Object>();
			NavSetObjEnumVal(valueObj, "Front", gfx::Side::Front);
			NavSetObjEnumVal(valueObj, "Back", gfx::Side::Back);
			NavSetObjEnumVal(valueObj, "Double", gfx::Side::Double);
			NavSetObjVal(targetObj, "Side", valueObj);

			valueObj = NavNew<Object>();
			NavSetObjEnumVal(valueObj, "Sampler2d", gfx::UniformType::Sampler2d);
			NavSetObjEnumVal(valueObj, "Vector4", gfx::UniformType::Vector4);
			NavSetObjEnumVal(valueObj, "Vector3", gfx::UniformType::Vector3);
			NavSetObjEnumVal(valueObj, "Vector2", gfx::UniformType::Vector2);
			NavSetObjEnumVal(valueObj, "Matrix3", gfx::UniformType::Matrix3);
			NavSetObjEnumVal(valueObj, "Matrix4", gfx::UniformType::Matrix4);
			NavSetObjEnumVal(valueObj, "MatrixModelView", gfx::UniformType::MatrixModelView);
			NavSetObjEnumVal(valueObj, "MatrixProjection", gfx::UniformType::MatrixProjection);
			NavSetObjVal(targetObj, "UniformType", valueObj);

			valueObj = NavNew<Object>();
			NavSetObjEnumVal(valueObj, "Vector4", gfx::AttributeType::Vector4);
			NavSetObjEnumVal(valueObj, "Vector3", gfx::AttributeType::Vector3);
			NavSetObjEnumVal(valueObj, "Vector2", gfx::AttributeType::Vector2);
			NavSetObjVal(targetObj, "AttributeType", valueObj);
		}

		void Init(Handle<Object> targetObj) {
			Local<Object> fourObj = NavNew<Object>();
			InitConstants(fourObj);
			NavObjectWrap<BufferAttribute>::Init(fourObj, "BufferAttribute");
			NavObjectWrap<BufferGeometry>::Init(fourObj, "BufferGeometry");
			NavObjectWrap<Shader>::Init(fourObj, "Shader"); 
			NavObjectWrap<ShaderMaterial>::Init(fourObj, "ShaderMaterial");
			NavObjectWrap<Object3d>::Init(fourObj, "Object3d");
			NavObjectWrap<Scene>::Init(fourObj, "Scene");
			NavObjectWrap<Camera>::Init(fourObj, "Camera");
			NavObjectWrap<Mesh>::Init(fourObj, "Mesh");
			NavObjectWrap<Renderer>::Init(fourObj, "Renderer");

			NavSetObjVal(fourObj, "DefaultRenderer", NavObjectWrap<Renderer>::Constructor());
			NavSetObjVal(targetObj, "FOUR", fourObj);
		}

		void Shutdown() {
			NavObjectWrap<Renderer>::Shutdown();
			NavObjectWrap<Mesh>::Shutdown();
			NavObjectWrap<Camera>::Shutdown(); 
			NavObjectWrap<Scene>::Shutdown();
			NavObjectWrap<Object3d>::Shutdown();
			NavObjectWrap<ShaderMaterial>::Shutdown();
			NavObjectWrap<Shader>::Shutdown();
			NavObjectWrap<BufferGeometry>::Shutdown();
			NavObjectWrap<BufferAttribute>::Shutdown();
		}
	}

	void requestAnimationFrame(const v8::FunctionCallbackInfo<v8::Value>& args) {
		v8::HandleScope scope(args.GetIsolate());
		v8::Local<v8::Function> cb = v8::Local<v8::Function>::Cast(args[0]);
		gAnimCallbacks.push_back(PersistentHandleWrapper<Function>(gIsolate, cb));
	}

	void Init(Handle<Object> globalObj) {
		NavSetObjFunc(globalObj, "requestAnimationFrame", requestAnimationFrame);

		Console::Init(globalObj);
		FOUR::Init(globalObj);
	}

	void Shutdown() {
		FOUR::Shutdown();
	}
}

void runFsScript(const std::string& path) {
	std::ifstream myReadFile;
	myReadFile.open(path);
	std::string output;
	if (myReadFile.is_open()) {
		output = std::string((std::istreambuf_iterator<char>(myReadFile)),
			std::istreambuf_iterator<char>());
	}
	myReadFile.close();

	TryCatch trycatch;
	Local<String> source = String::NewFromUtf8(gIsolate, output.c_str());
	Local<Script> script = Script::Compile(source, String::NewFromUtf8(gIsolate, path.c_str()));
	Local<Value> v = script->Run();
	if (v.IsEmpty()) {
		Local<Value> stackTrace = trycatch.StackTrace();
		if (!stackTrace.IsEmpty()) {
			String::Utf8Value trace_str(stackTrace);
			printf("%s\n", *trace_str);
		} else {
			Local<Value> exception = trycatch.Exception();
			String::Utf8Value exception_str(exception);
			printf("Exception: %s\n", *exception_str);
		}
	}
}

class MallocArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
public:
	virtual void* Allocate(size_t length) { return malloc(length); }
	virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
	virtual void Free(void* data, size_t length) { free(data); }
};

bool fourSetup() {
	// Initialize V8
	gPlatform = v8::platform::CreateDefaultPlatform();
	v8::V8::InitializePlatform(gPlatform);
	v8::V8::Initialize();
	v8::V8::SetArrayBufferAllocator(new MallocArrayBufferAllocator);

	// Create a new Isolate and make it the current one.
	gIsolate = Isolate::New();
	gIsolate->Enter();

	HandleScope handleScope(gIsolate);

	Local<ObjectTemplate> globalTpl = ObjectTemplate::New();

	// Create a new context.
	Local<Context> context = Context::New(gIsolate, NULL, globalTpl);
	context->Enter();

	gContext = new PersistentHandleWrapper<Context>(gIsolate, context);
	
	Local<Object> globalObj = context->Global();
	js::Init(globalObj);

	// Enter the context for compiling and running the hello world script.
	Context::Scope contextScope(context);
	runFsScript("node_util.js"); 
	runFsScript("four.js");

	runFsScript("test.js");


	return true;
}

bool fourResize(int w, int h) {

	return true;
}

void fourDestroy() {
	{
		HandleScope handleScope(gIsolate);

		gAnimCallbacks.clear();

		js::Shutdown();

		gContext->Extract()->Exit();
		if (gContext) {
			delete gContext;
			gContext = nullptr;
		}
	}
	if (gIsolateScope) {
		delete gIsolateScope;
		gIsolateScope = nullptr;
	}
	gIsolate->Exit();
	gIsolate->Dispose();
	gIsolate = nullptr;
	v8::V8::ShutdownPlatform();
	v8::V8::Dispose();
	if (gPlatform) {
		delete gPlatform;
		gPlatform = nullptr;
	}
}

void fourRender() {
	std::vector<PersistentHandleWrapper<Function>> animCallbacks = gAnimCallbacks;
	gAnimCallbacks.clear();

	HandleScope handleScope(gIsolate);

	Local<Value> animCallbackArgs[] = {
		Number::New(gIsolate, 0.0)
	};
	for (auto i = animCallbacks.begin(); i != animCallbacks.end(); ++i) {
		Handle<Function> animCallback = i->Extract();
		animCallback->Call(gIsolate->GetCurrentContext()->Global(), 1, animCallbackArgs);
	}
}
