#include "stdafx.h"
#include "math.h"

namespace gfx {
	enum class UniformType : uint32_t {
		Sampler2d,
		Vector4,
		Vector3,
		Vector2,
		Matrix3,
		Matrix4
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
			: _parent(nullptr), _scale(1,1,1) {
			printf("gfx::^Object3d\n");
		}

		virtual ObjectType type() { return ObjectType::Object3d; }

		inline void updateTransform() {
			if (_transformNeedsUpdate) {
				_matrix.setIdentity();
				_matrix.translate(_position);
				_matrix.rotate(_rotation);
				_matrix.scale(_scale);
				_transformNeedsUpdate = false;
			}

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
		struct Uniform {
			std::string name;
			UniformType type;
		};

		struct Attribute {
			std::string name;
			AttributeType type;
		};

		Shader() {
			printf("gfx::^ShaderMaterial\n");
		}

		std::string vertexSource;
		std::string fragmentSrc;
		std::vector<Uniform> uniforms;
		std::vector<Attribute> attributes;

	};

	class ShaderMaterial {
	public:
		ShaderMaterial() {
			printf("gfx::^ShaderMaterial\n");
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

		void _recurseScene(Object3d *obj, int depth = 0) {
			obj->updateTransform();
			
			math::Vector3 pos = obj->localToWorld(math::Vector3(0, 0, 0));
			for (int i = 0; i < depth; ++i) printf("  ");
			printf("%08x: %d %d", obj, obj->type(), obj->_transformNeedsUpdate ? 1 : 0);
			for (int i = 0; i < 4-depth; ++i) printf("  ");
			printf("L:(%.2f, %.2f, %.2f) W:(%.2f, %.2f, %.2f)\n",
				obj->_position.x(), obj->_position.y(), obj->_position.z(),
				pos.x(), pos.y(), pos.z());

			if (obj->type() == ObjectType::Mesh) {
				auto mesh = reinterpret_cast<Mesh*>(obj);
				printf("Mesh: %08x %08x\n", mesh->_geometry, mesh->_material);
				for (auto i : mesh->_geometry->_attributes) {
					printf("Attrib: %s (%d)\n", i.first.c_str(), i.second->_data.size());
				}
			}

			for (auto& i : obj->_children) {
				_recurseScene(i, depth + 1);
			}
		}

		void render(Scene *scene, Camera *camera) {
			_recurseScene(scene);
		}
	}
}