console.log = function() {
    var strOut = '';
    for (var i = 0; i < arguments.length; ++i) {
        if (i > 0) strOut += ' ';
        strOut += util.inspect(arguments[i]);
    }
    console.write(strOut);
};

/**
 * @property Object3d
 */
var FOUR;

FOUR.Vector2 = function Vector2() {
    this.x = 0;
    this.y = 0;
};

FOUR.Vector3 = function Vector3() {
    this.x = 0;
    this.y = 0;
    this.z = 0;
};

FOUR.Vector3.prototype.set = function(x, y, z) {
    this.x = x;
    this.y = y;
    this.z = z;
};

FOUR.Vector3.prototype.multiplyScalar = function (scale) {
    this.x *= scale;
    this.y *= scale;
    this.z *= scale;
};

FOUR.Vector3.prototype.clone = function () {
    var n = new FOUR.Vector3();
    n.x = this.x;
    n.y = this.y;
    n.z = this.z;
    return n;
};

FOUR.Vector4 = function Vector4() {
    this.x = 0;
    this.y = 0;
    this.z = 0;
    this.w = 0;
};

FOUR.Quaternion = function Quaternion() {
    this.x = 0;
    this.y = 0;
    this.z = 0;
    this.w = 1;
};

FOUR.DefaultRenderer = FOUR.Renderer;
