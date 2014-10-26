#include "stdafx.h"
#include "Four.h"
#include "nav.h"
#include "math.h"
#include "gfx.h"
#include "uvpp.h"

using namespace v8; 

std::vector<PersistentHandleWrapper<Function>> gAnimCallbacks;

namespace http {
	struct Response {
		uint32_t statusCode;
		std::vector<std::pair<std::string, std::string>> headers;
		std::vector<uint8_t> body;
	};

	class ResponseParser {
	public:
		ResponseParser() {
			http_parser_init(&_parser, HTTP_RESPONSE);
			_parser.data = this;
			_settings.on_message_begin = &_parserCb < &ResponseParser::_onMessageBegin > ;
			_settings.on_url = &_parserCb < &ResponseParser::_onUrl > ;
			_settings.on_status = &_parserCb < &ResponseParser::_onStatus > ;
			_settings.on_header_field = &_parserCb < &ResponseParser::_onHeaderField > ;
			_settings.on_header_value = &_parserCb < &ResponseParser::_onHeaderValue > ;
			_settings.on_headers_complete = &_parserCb < &ResponseParser::_onHeadersComplete > ;
			_settings.on_body = &_parserCb < &ResponseParser::_onBody > ;
			_settings.on_message_complete = &_parserCb < &ResponseParser::_onMessageComplete > ;
		}

		void parse(const char *data, size_t len) {
			http_parser_execute(&_parser, &_settings, data, len);
		}

		void finish() {
			http_parser_execute(&_parser, &_settings, nullptr, 0);
		}

		virtual void onComplete(const Response& response) {
			printf("HttpResponseParser::onComplete\n");
		}

		virtual void onError(uint32_t code) {
			printf("HttpResponseParser::onError(%d)\n", code);
		}

	private:
		enum class HeaderState : uint32_t {
			Name,
			Value
		};

		template<int(ResponseParser::*F)()>
		static int _parserCb(http_parser *parser) {
			return (((ResponseParser*)parser->data)->*F)();
		}
		template<int(ResponseParser::*F)(const char*, size_t)>
		static int _parserCb(http_parser *parser, const char *at, size_t length) {
			return (((ResponseParser*)parser->data)->*F)(at, length);
		}

		int _onMessageBegin() {
			return 0;
		}
		int _onUrl(const char *at, size_t len) {
			return 0;
		}
		int _onStatus(const char *at, size_t len) {
			_response.statusCode = _parser.status_code;
			return 0;
		}
		int _onHeaderField(const char *at, size_t len) {
			if (_headerState == HeaderState::Value) {
				_response.headers.emplace_back(_headerName, _headerValue);
				_headerName.resize(0);
				_headerValue.resize(0);
			}
			_headerName += std::string(at, len);
			_headerState = HeaderState::Name;
			return 0;
		}
		int _onHeaderValue(const char *at, size_t len) {
			_headerValue += std::string(at, len);
			_headerState = HeaderState::Value;
			return 0;
		}
		int _onHeadersComplete() {
			if (_headerState == HeaderState::Value) {
				_response.headers.emplace_back(_headerName, _headerValue);
				_headerName.resize(0);
				_headerValue.resize(0);
			}

			return 0;
		}
		int _onBody(const char *at, size_t len) {
			size_t offset = _response.body.size();
			_response.body.resize(offset + len);
			memcpy(&_response.body[offset], at, len);
			return 0;
		}
		int _onMessageComplete() {
			this->onComplete(_response);
			_response = Response();
			return 0;
		}

		http_parser _parser;
		http_parser_settings _settings;
		Response _response;
		HeaderState _headerState;
		std::string _headerName;
		std::string _headerValue;

	};

	class Socket : public uvpp::TcpSocket {
	public:
		Socket(uvpp::EventLoop& loop)
			: TcpSocket(loop), _parser(*this) {
		}

		void request(const std::string& host, const std::string& path) {
			std::string out;
			out += "GET " + path + " HTTP/1.1\r\n";
			out += "Host: " + host + "\r\n";
			out += "\r\n";
			send(out.c_str(), out.size());
		}

		virtual void onConnect() override {
			printf("HttpSocket::onConnect\n");
		}

		virtual void onClose() override {
			printf("HttpSocket::onClose\n");
			_parser.finish();
		}

		virtual void onResponse(const Response& response) {
			printf("HttpSocket::onResponse()\n");
		}

		virtual void onError(uint32_t code) override {
			printf("HttpSocket::onError(%d)\n", code);
		}

	private:
		virtual void onRecv(const char *data, size_t len) override {
			printf("HttpSocket::onRecv(%p, %d)\n", data, len);
			_parser.parse(data, len);
		}

		class _ResponseParser : public ResponseParser {
		public:
			_ResponseParser(Socket& owner)
				: _owner(owner) {
			}
			void onComplete(const Response& response) override {
				_owner.onResponse(response);
			}
			void onError(uint32_t code) override {
				_owner.onError(code);
			}
		private:
			Socket& _owner;
		};

		std::string _uri;
		_ResponseParser _parser;

	};
}

namespace iothread {
	class WorkerRequest {
	public:
		virtual void execute() = 0;
		virtual void onComplete() = 0;

	};

	class _WorkerThread : public uvpp::Thread {
		friend class WorkerRequest;

	public:
		_WorkerThread()
			: _signalEvent(*this, _eventLoop) {
		}

		void dispatch(WorkerRequest *request) {
			{
				uvpp::ScopedLock lock(_inMutex);
				_requestsIn.push_back(request);
			}
			_signalEvent.signal();
		}

		void poll() {
			std::vector<WorkerRequest*> requests;
			{
				uvpp::ScopedLock lock(_outMutex);
				requests = std::move(_requestsOut);
			}

			for (auto& i : requests) {
				i->onComplete();
			}
		}

		uvpp::EventLoop& loop() {
			return _eventLoop;
		}

		void _dispatchCompletion(WorkerRequest *request) {
			uvpp::ScopedLock lock(_outMutex);
			_requestsOut.push_back(request);
		}

	private:
		void threadExec() override {
			_eventLoop.run();
		}

		void _grabRequests() {
			std::vector<WorkerRequest*> requests;
			{
				uvpp::ScopedLock lock(_inMutex);
				requests = std::move(_requestsIn);
			}
			for (auto& i : requests) {
				i->execute();
			}
		}

		class _NewRequestEvent : public uvpp::Event {
		public:
			_NewRequestEvent(_WorkerThread& owner, uvpp::EventLoop& loop)
				: uvpp::Event(loop), _owner(owner) {
			}
			virtual void onSignal() {
				_owner._grabRequests();
			}
		private:
			_WorkerThread& _owner;
		};

		uvpp::EventLoop _eventLoop;
		uvpp::Mutex _inMutex;
		uvpp::Mutex _outMutex;
		_NewRequestEvent _signalEvent;
		std::vector<class WorkerRequest*> _requestsIn;
		std::vector<class WorkerRequest*> _requestsOut;

	};
	_WorkerThread *_thread = nullptr;

	void Init() {
		_thread = new _WorkerThread();
		_thread->start();
	}

	void Shutdown() {
	}

	void dispatch(WorkerRequest *request) {
		_thread->dispatch(request);
	}

	void poll() {
		_thread->poll();
	}

	class UriRequest : public WorkerRequest {
	public:
		UriRequest(const std::string& uri)
			: _proc(nullptr) {
		}

	protected:
		int32_t _errorCode;
		http::Response _response;

	private:
		class HttpProc : http::Socket {
		public:
			HttpProc(UriRequest& owner, const std::string& host, uint16_t port, const std::string& path)
				: http::Socket(_thread->loop()), _owner(owner), _host(host), _port(port), _path(path) {
			}
			void start() {
				connect(_host, _port);
			}
			virtual void onConnect() override {
				request(_host, _path);
			}
			virtual void onResponse(const http::Response& response) {
				_owner.onComplete(response);
			}
			virtual void onError(uint32_t code) override {
				_owner.onError(code);
			}
		private:
			UriRequest& _owner;
			std::string _host;
			uint16_t _port;
			std::string _path;
		};
		HttpProc *_proc;

		void execute() override {
			printf("HttpProc execute\n");
			_proc = new HttpProc(*this, "127.0.0.1", 80, "/");
			_proc->start();
		}

		virtual void onComplete(const http::Response& response) {
			_errorCode = 0;
			_response = response;
			_thread->_dispatchCompletion(this);
		}

		virtual void onError(int32_t code) {
			_errorCode = code;
			_thread->_dispatchCompletion(this);
		}

	};



	/*
	class Request;
	void _publishComplete(Request *req);
	
	uv_loop_t _uvLoop;
	uv_thread_t _uvThread;
	uv_async_t _uvAsync;
	uv_mutex_t _uvMutexIn;
	uv_mutex_t _uvMutexOut;
	std::vector<class Request*> _requestsIn;
	std::vector<class Request*> _requestsOut;

	class Request {
	public:
		enum class HeaderState : uint32_t {
			Name,
			Value
		};

		std::string reqPath;
		uv_tcp_t _uvConn;
		http_parser _parser;
		http_parser_settings _parserSettings;
		std::string host;
		int port;
		std::string path;
		std::string reqData;
		unsigned int respStatus;
		std::vector<std::pair<std::string, std::string>> respHeaders;
		std::vector<uint8_t> respData;
		HeaderState _curHeaderState;
		std::string _curHeaderName;
		std::string _curHeaderValue;

		static void _uvAllocCb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
			buf->base = new char[suggested_size];
			buf->len = suggested_size;
		}

		static void _uvConnectCb(uv_connect_t *req, int status) {
			((Request*)req->data)->_onConnect(req, status);
		}

		static void _uvReadCb(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
			((Request*)stream->data)->_onRead(nread, buf->base);
			if (nread == UV_EOF) {
				uv_close((uv_handle_t*)stream, nullptr);
			}
		}

		static void _uvWriteCb(uv_write_t *req, int status) {
			delete req;
		}

		void _onConnect(uv_connect_t *req, int status) {
			if (status == -1) {
				printf("CONNECT ERROR\n");
				return;
			}

			uv_read_start(req->handle, _uvAllocCb, _uvReadCb);

			reqData = _buildRequest();

			uv_buf_t buf;
			buf.base = (char*)reqData.c_str();
			buf.len = reqData.size();

			uv_write_t *wreq = new uv_write_t;
			uv_write(wreq, req->handle, &buf, 1, _uvWriteCb);
		}

		void _onRead(ssize_t nread, char *buf) {
			if (nread == UV_EOF) {
				nread = 0;
			}
			size_t pread = http_parser_execute(&_parser, &_parserSettings, buf, nread);
			if (pread != nread) {
				printf("Parser error\n");
			}
			delete buf;
		}

		std::string _buildRequest() {
			std::string res;
			res += "GET " + path + " HTTP/1.1\r\n";
			res += "Host: " + host + "\r\n";
			res += "\r\n";
			return res;
		}

		template<int(Request::*F)()>
		static int _parserCb(http_parser *parser) {
			return (((Request*)parser->data)->*F)();
		}
		template<int(Request::*F)(const char*, size_t)>
		static int _parserCb(http_parser *parser, const char *at, size_t length) {
			return (((Request*)parser->data)->*F)(at, length);
		}

		int _parserOnMessageBegin() {
			_curHeaderState = HeaderState::Name;
			return 0;
		}
		int _parserOnUrl(const char *at, size_t len) {
			return 0;
		}
		int _parserOnStatus(const char *at, size_t len) {
			respStatus = _parser.status_code;
			return 0;
		}
		int _parserOnHeaderField(const char *at, size_t len) {
			if (_curHeaderState == HeaderState::Value) {
				respHeaders.emplace_back(_curHeaderName, _curHeaderValue);
				_curHeaderName.resize(0);
				_curHeaderValue.resize(0);
			}
			_curHeaderName += std::string(at, len);
			_curHeaderState = HeaderState::Name;
			return 0;
		}
		int _parserOnHeaderValue(const char *at, size_t len) {
			_curHeaderValue += std::string(at, len);
			_curHeaderState = HeaderState::Value;
			return 0;
		}
		int _parserOnHeadersComplete() {
			if (_curHeaderState == HeaderState::Value) {
				respHeaders.emplace_back(_curHeaderName, _curHeaderValue);
				_curHeaderName.resize(0);
				_curHeaderValue.resize(0);
			}

			return 0;
		}
		int _parserOnBody(const char *at, size_t len) {
			size_t offset = respData.size();
			respData.resize(offset + len);
			memcpy(&respData[offset], at, len);
			return 0;
		}
		int _parserOnMessageComplete() {
			printf("Message Complete\n");
			_publishComplete(this);
			return 0;
		}

		void Start() {
			printf("Request Start\n");

			http_parser_url url;
			http_parser_parse_url(reqPath.c_str(), reqPath.size(), 0, &url);

			std::string urlHost = "";
			uint16_t urlPort;
			std::string urlPath = "/";
			if (url.field_set & (1 << UF_HOST)) {
				auto& hostField = url.field_data[UF_HOST];
				urlHost = std::string(&reqPath[hostField.off], hostField.len);
			}
			if (url.field_set & (1 << UF_PORT)) {
				urlPort = url.port;
			}
			if (url.field_set & (1 << UF_PATH)) {
				auto& pathField = url.field_data[UF_PATH];
				urlPath = std::string(&reqPath[pathField.off], pathField.len);
			}

			_parserSettings.on_message_begin = &_parserCb<&Request::_parserOnMessageBegin>;
			_parserSettings.on_url = &_parserCb<&Request::_parserOnUrl>;
			_parserSettings.on_status = &_parserCb<&Request::_parserOnStatus>;
			_parserSettings.on_header_field = &_parserCb<&Request::_parserOnHeaderField>;
			_parserSettings.on_header_value = &_parserCb<&Request::_parserOnHeaderValue>;
			_parserSettings.on_headers_complete = &_parserCb<&Request::_parserOnHeadersComplete>;
			_parserSettings.on_body = &_parserCb<&Request::_parserOnBody>;
			_parserSettings.on_message_complete = &_parserCb<&Request::_parserOnMessageComplete>;

			http_parser_init(&_parser, HTTP_RESPONSE);
			_parser.data = this;

			uv_tcp_init(&_uvLoop, &_uvConn);
			_uvConn.data = (void*)this;

			uv_connect_t *conn= new uv_connect_t;
			conn->data = this;

			struct sockaddr_in bindAddr;
			uv_ip4_addr(host.c_str(), port, &bindAddr);
			
			uv_tcp_connect(conn, &_uvConn, (const sockaddr*)&bindAddr, &_uvConnectCb);
		}

		void Complete() {
			printf("Request Complete\n");
		}

	};

	static void _uvAsyncCb(uv_async_t* handle) {
		std::vector<Request*> requests;
		uv_mutex_lock(&_uvMutexIn);
		requests = std::move(_requestsIn);
		uv_mutex_unlock(&_uvMutexIn);

		for (auto& i : requests) {
			i->Start();
		}
	}

	void _uvThreadCb(void* arg) {
		while (true) {
			uv_run(&_uvLoop, UV_RUN_DEFAULT);
			printf("UV LOOP EXITED\n");
			Sleep(100);
		}
	}

	void _publishComplete(Request *req) {
		uv_mutex_lock(&_uvMutexOut);
		_requestsOut.push_back(req);
		uv_mutex_unlock(&_uvMutexOut);
	}

	void Send(Request *req) {
		uv_mutex_lock(&_uvMutexIn);
		_requestsIn.push_back(req);
		uv_mutex_unlock(&_uvMutexIn);
		uv_async_send(&_uvAsync);
	}

	void Poll() {
		std::vector<Request*> requests;

		uv_mutex_lock(&_uvMutexOut);
		requests = std::move(_requestsOut);
		uv_mutex_unlock(&_uvMutexOut);

		for (auto& i : requests) {
			i->Complete();
			delete i;
		}
	}

	void Init() {
		uv_loop_init(&_uvLoop);
		uv_mutex_init(&_uvMutexIn);
		uv_mutex_init(&_uvMutexOut);
		uv_async_init(&_uvLoop, &_uvAsync, _uvAsyncCb);
		uv_thread_create(&_uvThread, _uvThreadCb, nullptr);
	}

	void Shutdown() {
	}
	*/
}

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

		namespace io {
			class UriRequest : public iothread::UriRequest {
			public:
				UriRequest(const std::string& uri, PersistentHandleWrapper<Function> callback)
					: iothread::UriRequest(uri), _callback(callback) {
				}

			private:
				void _invokeCallback(uint32_t error, const char *data, size_t len) {
					HandleScope handleScope(gIsolate);
					Handle<Function> callback = _callback.Extract();
					Handle<Value> args[2];
					if (error == 0) {
						args[0] = NavNull();
						args[1] = NavNew(data, len);
					} else {
						args[0] = NavNew<Integer>(error);
						args[1] = NavNull();
					}
					callback->Call(NavGlobal(), 2, args);
				}
				void onComplete() override {
					printf("io::UriRequest::onComplete()\n");
					if (_errorCode == 0) {
						_invokeCallback(0, (const char*)&_response.body[0], _response.body.size());
					} else {
						_invokeCallback(_errorCode, nullptr, 0);
					}
				}

				void onError(int32_t code) {
					_invokeCallback(code, nullptr, 0);
				}

				PersistentHandleWrapper<Function> _callback;

			};
			void load(const v8::FunctionCallbackInfo<v8::Value>& args) {
				if (args.Length() < 2) {
					return;
				}

				String::Utf8Value hostStr(args[0]);
				PersistentHandleWrapper<Function> callback(gIsolate, args[1].As<Function>());

				auto req = new UriRequest(*hostStr, callback);
				iothread::dispatch(req);
			}

			void Init(Handle<Object> targetObj) {
				Handle<Object> ioObj = NavNew<Object>();
				NavSetObjFunc(ioObj, "load", load);
				NavSetObjVal(targetObj, "io", ioObj);
			}

			void Shutdown() {
			}
		}

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
			io::Init(fourObj);

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
			io::Shutdown();
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
	iothread::Init();

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

	iothread::Shutdown();
}

void fourRender() {
	std::vector<PersistentHandleWrapper<Function>> animCallbacks = gAnimCallbacks;
	gAnimCallbacks.clear();

	HandleScope handleScope(gIsolate);

	iothread::poll();

	Local<Value> animCallbackArgs[] = {
		Number::New(gIsolate, 0.0)
	};
	for (auto i = animCallbacks.begin(); i != animCallbacks.end(); ++i) {
		Handle<Function> animCallback = i->Extract();
		animCallback->Call(gIsolate->GetCurrentContext()->Global(), 1, animCallbackArgs);
	}
}
