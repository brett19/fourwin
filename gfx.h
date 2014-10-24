#include "stdafx.h"
#include "math.h"

namespace gfx {
	enum class UniformType : uint32_t {
		Sampler2d,
		Vector4,
		Vector3,
		Vector2,
		Matrix3,
		Matrix4,
		MatrixModelView,
		MatrixProjection
	};

	enum class AttributeType : uint32_t {
		Vector4,
		Vector3,
		Vector2
	};

	enum class ObjectType : uint32_t {
		Object3d,
		Camera,
		Scene,
		Mesh
	};

	enum class Side : uint32_t {
		Front,
		Back,
		Double
	};

	enum class BufferType : uint32_t {
		Byte = GL_BYTE,
		UnsignedByte = GL_UNSIGNED_BYTE,
		Short = GL_SHORT,
		UnsignedShort = GL_UNSIGNED_SHORT,
		Int = GL_INT,
		UnsignedInt = GL_UNSIGNED_INT,
		Float = GL_FLOAT
	};

	namespace Renderer {
		math::Matrix4 projMatrix;
		math::Affine3 viewMatrix;
		math::Affine3 modelViewMatrix;
	}

	class Object3d {
	public:
		EIGEN_MAKE_ALIGNED_OPERATOR_NEW

		math::Vector3 _position;
		math::Quaternion _rotation;
		math::Vector3 _scale;
		bool _transformNeedsUpdate;
		math::Affine3 _matrix;
		math::Affine3 _worldMatrix;
		Object3d* _parent;
		std::vector<Object3d*> _children;

		Object3d()
			: _parent(nullptr) {
			printf("gfx::^Object3d\n");
			_position.setZero();
			_rotation.setIdentity();
			_scale.setOnes();
		}

		virtual ObjectType type() { return ObjectType::Object3d; }

		inline void updateTransform() {
			//if (_transformNeedsUpdate) {
				_matrix.setIdentity();
				_matrix.translate(_position);
				_matrix.rotate(_rotation);
				_matrix.scale(_scale);
				_transformNeedsUpdate = false;
			//}

			if (_parent) {
				_worldMatrix = _parent->_worldMatrix * _matrix;
			} else {
				_worldMatrix = _matrix;
			}
		}

		math::Vector3 localToWorld(const math::Vector3& vec) {
			math::Vector4 b = _worldMatrix * math::Vector4(vec.x(), vec.y(), vec.z(), 1.0f);
			return math::Vector3(b.x(), b.y(), b.z());
		}

		void addChild(Object3d *child) {
			_children.push_back(child);
			child->_parent = this;
		}

		void removeChild(Object3d *child) {
			auto foundI = std::find(_children.begin(), _children.end(), child);
			if (foundI != _children.end()) {
				_children.erase(foundI);
				child->_parent = nullptr;
			}
		}
	};

	class Scene : public Object3d {
	public:
		Scene() {
			printf("gfx::^Scene\n");
		}

		virtual ObjectType type() override { return ObjectType::Scene; }

	};

	class BufferAttribute {
	public:
		BufferAttribute()
			: _needsUpdate(false), _itemType(BufferType::Float) {
			printf("gfx::^BufferAttribute\n");
		}

		bool upload() {

			_needsUpdate = false;
			return true;
		}
		
		bool bind(GLuint slot) {
			if (_needsUpdate) {
				if (!upload()) {
					return false;
				}
			}

			glVertexAttribPointer(slot, _itemSize, (GLenum)_itemType, GL_FALSE, 0, &_data[0]);
			return true;
		}

		std::vector<uint8_t> _data;
		int32_t _itemSize;
		BufferType _itemType;
		bool _needsUpdate;
	};

	class BufferGeometry {
	public:
		BufferGeometry() {
			printf("gfx::^BufferGeometry\n");
		}

		std::unordered_map<std::string, BufferAttribute*> _attributes;
	};

	class Shader {
	public:
		enum class CompileState : uint32_t {
			UNCOMPILED,
			COMPILING,
			COMPILED,
			LINKING,
			LINKED,
			FAILED
		};

		struct Uniform {
			std::string name;
			UniformType type;
		};

		struct Attribute {
			std::string name;
			AttributeType type;
		};

		Shader()
			: _compileState(CompileState::UNCOMPILED), 
				_program(0), _vertexShader(0), _fragmentShader(0) {
			printf("gfx::^ShaderMaterial\n");
		}

		void initVars() {
			GLuint location = 0;
			for (auto& i : attributes) {
				GLuint attribLoc = location++;
				_locations.emplace(i.name, AttributeBindInfo(attribLoc));
			}

			for (auto& i : uniforms) {
				_uniformInfo.emplace(i.name, UniformBindInfo(i.type));
			}
		}

		static GLuint _compileShader(const char* source, GLuint type) {
			GLuint shader = glCreateShader(type);
			if (shader == 0) {
				return 0;
			}

			glShaderSource(shader, 1, &source, nullptr);

			glCompileShader(shader);

			return shader;
		}

		static bool _checkShader(GLuint shader) {
			GLint compiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

			if (!compiled) {
				GLint infoLen = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);

				if (infoLen > 0) {
					char * infoLog = new char[infoLen];
					glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
					printf("Shader Compile Failed:\n%s", infoLog);
					delete[] infoLog;
				}

				return false;
			}
			return true;
		}

		bool compile() {
			_vertexShader = _compileShader(vertexSrc.c_str(), GL_VERTEX_SHADER);
			if (_vertexShader == 0) {
				_compileState = CompileState::FAILED;
				return false;
			}

			_fragmentShader = _compileShader(fragmentSrc.c_str(), GL_FRAGMENT_SHADER);
			if (_fragmentShader == 0) {
				_compileState = CompileState::FAILED;
				return false;
			}

			_compileState = CompileState::COMPILING;
			return true;
		}

		bool checkCompile() {
			if (!_checkShader(_vertexShader)) {
				_compileState = CompileState::FAILED;
				return false;
			}

			if (!_checkShader(_fragmentShader)) {
				_compileState = CompileState::FAILED;
				return false;
			}

			_compileState = CompileState::COMPILED;
			return true;
		}

		bool link() {
			_program = glCreateProgram();
			if (_program == 0) {
				_compileState = CompileState::FAILED;
				return false;
			}

			glAttachShader(_program, _vertexShader);
			glAttachShader(_program, _fragmentShader);

			for (auto& i : _locations) {
				glBindAttribLocation(_program, i.second.location, i.first.c_str());
			}

			glLinkProgram(_program);

			_compileState = CompileState::LINKING;
			return true;
		}

		bool checkLink() {
			GLint linked;
			glGetProgramiv(_program, GL_LINK_STATUS, &linked);

			if (!linked) {
				GLint infoLen = 0;
				glGetShaderiv(_program, GL_INFO_LOG_LENGTH, &infoLen);

				if (infoLen > 0) {
					char * infoLog = new char[infoLen];
					glGetProgramInfoLog(_program, infoLen, NULL, infoLog);
					printf("Shader Link Failed:\n%s", infoLog);
					delete[] infoLog;
				}

				_compileState = CompileState::FAILED;
				return false;
			}

			if (!bindVars()) {
				_compileState = CompileState::FAILED;
				return false;
			}

			_compileState = CompileState::LINKED;
			return true;
		}

		bool bindVars() {
			for (auto& i : _uniformInfo) {
				GLint location = glGetUniformLocation(_program, i.first.c_str());
				if (location == -1) {
					return false;
				}
				i.second.location = location;
			}
			return true;
		}

		bool _bind() {
			if (_compileState == CompileState::UNCOMPILED) {
				compile();
			}
			if (_compileState == CompileState::COMPILING) {
				checkCompile();
			}
			if (_compileState == CompileState::COMPILED) {
				link();
			}
			if (_compileState == CompileState::LINKING) {
				checkLink();
			}
			if (_compileState == CompileState::LINKED) {
				glUseProgram(_program);

				return true;
			}

			return false;
		}

		bool bind() {
			return _bind();
		}

		bool bindFor(BufferGeometry* geom) {
			if (_bind()) {
				bool bindSuccess = true;
				for (auto& i : _uniformInfo) {
					const UniformBindInfo& bindInfo = i.second;
					if (bindInfo.type == UniformType::Vector2) {
						glUniform2fv(bindInfo.location, 1, (GLfloat*)bindInfo.data);
					} else if (bindInfo.type == UniformType::Vector3) {
						glUniform3fv(bindInfo.location, 1, (GLfloat*)bindInfo.data);
					} else if (bindInfo.type == UniformType::Vector4) {
						glUniform4fv(bindInfo.location, 1, (GLfloat*)bindInfo.data);
					} else if (bindInfo.type == UniformType::Matrix3) {
						glUniformMatrix3fv(bindInfo.location, 1, false, (GLfloat*)bindInfo.data);
					} else if (bindInfo.type == UniformType::Matrix4) {
						glUniformMatrix4fv(bindInfo.location, 1, false, (GLfloat*)bindInfo.data);
					} else if (bindInfo.type == UniformType::MatrixProjection) {
						glUniformMatrix4fv(bindInfo.location, 1, false, (GLfloat*)Renderer::projMatrix.data());
					} else if (bindInfo.type == UniformType::MatrixModelView) {
						glUniformMatrix4fv(bindInfo.location, 1, false, (GLfloat*)Renderer::modelViewMatrix.data());
					} else {
						printf("Encountered unknown uniform bind type.\n");
						bindSuccess = false;
						break;
					}
				}
				if (bindSuccess) {
					for (auto& i : geom->_attributes) {
						BufferAttribute *attrib = i.second;

						auto foundLoc = _locations.find(i.first);
						if (foundLoc != _locations.end()) {
							const AttributeBindInfo& bindInfo = foundLoc->second;

							if (!attrib->bind(bindInfo.location)) {
								bindSuccess = false;
								break;
							}
							glEnableVertexAttribArray(bindInfo.location);
						}
					}
				}
				return bindSuccess;
			}
			return false;
		}

		struct AttributeBindInfo {
			AttributeBindInfo(GLuint location_)
				: location(location_) {}

			GLuint location;
		};
		struct UniformBindInfo {
			UniformBindInfo(UniformType type_)
				: type(type_), location(0) {
				memset(data, 0, sizeof(data));
			}

			GLuint location;
			UniformType type;
			uint8_t data[4 * 16];
		};

		CompileState _compileState;
		GLuint _program;
		GLuint _vertexShader;
		GLuint _fragmentShader;
		std::map<std::string, AttributeBindInfo> _locations;
		std::map<std::string, UniformBindInfo> _uniformInfo;

		std::string vertexSrc;
		std::string fragmentSrc;
		std::vector<Uniform> uniforms;
		std::vector<Attribute> attributes;

	};

	class ShaderMaterial {
	public:
		ShaderMaterial() {
			printf("gfx::^ShaderMaterial\n");
		}

		void _bind() {
			// TODO:  Fuck it.
		}

		bool bind() {
			printf("ShaderMaterial::bind\n");
			_bind();
			return _shader->bind();
		}

		bool bindFor(BufferGeometry* geom) {
			printf("ShaderMaterial::bindFor\n");
			_bind();
			return _shader->bindFor(geom);
		}

		Shader *_shader;
		bool _transparent;
		bool _depthTest;
		bool _depthWrite;
		Side _side;

	};

	class Mesh : public Object3d {
	public:
		BufferGeometry *_geometry;
		ShaderMaterial *_material;

		Mesh()
			:_geometry(nullptr), _material(nullptr) {
			printf("gfx::^Mesh\n");
		}

		virtual ObjectType type() override { return ObjectType::Mesh; }

		void setGeometry(BufferGeometry *geometry) {
			_geometry = geometry;
		}

		void setMaterial(ShaderMaterial *material) {
			_material = material;
		}

		void render() {
			if (_material->bindFor(_geometry)) {
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
		}
	};

	class Camera : public Object3d {
	public:
		math::Matrix4 _viewMatrix;
		math::Matrix4 _projMatrix;

		Camera() {
			printf("gfx::^Camera\n");
		}

		virtual ObjectType type() override { return ObjectType::Camera; }

		void lookAt(const math::Vector3& position, const math::Vector3& target, const math::Vector3& up) {
			math::Matrix3 R;
			R.col(2) = (position - target).normalized();
			R.col(0) = up.cross(R.col(2)).normalized();
			R.col(1) = R.col(2).cross(R.col(0));

			_viewMatrix.topLeftCorner<3, 3>() = R.transpose();
			_viewMatrix.topRightCorner<3, 1>() = -R.transpose() * position;
			_viewMatrix(3, 3) = 1.0f;
		}

		void setPerspective(float fovY, float aspect, float dnear, float dfar) {
			float theta = fovY * 0.5f;
			float range = dfar - dnear;
			float invtan = 1.0f / tan(theta);

			_projMatrix(0, 0) = invtan / aspect;
			_projMatrix(1, 1) = invtan;
			_projMatrix(2, 2) = -(dnear + dfar) / range;
			_projMatrix(3, 2) = -1;
			_projMatrix(2, 3) = -2 * dnear * dfar / range;
			_projMatrix(3, 3) = 0.0f;
		}
	};

	namespace Renderer {
		void setClearColor(float r, float g, float b, float a) {
			glClearColor(r, g, b, a);
		}

		void clear(bool color, bool depth, bool stencil) {
			GLbitfield clearBits = 0;
			clearBits |= color ? GL_COLOR_BUFFER_BIT : 0;
			clearBits |= depth ? GL_DEPTH_BUFFER_BIT : 0;
			clearBits |= stencil ? GL_STENCIL_BUFFER_BIT : 0;
			glClear(clearBits);
		}

		void _recurseProjectScene(Object3d *obj, int depth = 0) {
			obj->updateTransform();
		 
			for (auto& i : obj->_children) {
				_recurseProjectScene(i, depth + 1);
			}
		}

		void _recurseRenderScene(Object3d *obj, int depth = 0) {
			if (obj->type() == ObjectType::Mesh) {
				auto mesh = reinterpret_cast<Mesh*>(obj);
				modelViewMatrix = mesh->_worldMatrix * viewMatrix;
				mesh->render();
			}

			for (auto& i : obj->_children) {
				_recurseRenderScene(i, depth + 1);
			}
		}

		void render(Scene *scene, Camera *camera) {
			projMatrix.setIdentity();
			//projMatrix = camera->_projMatrix;
			viewMatrix = camera->_worldMatrix.inverse();

			_recurseProjectScene(scene);
			_recurseRenderScene(scene);
		}
	}
}