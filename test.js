var jsverts = [
    -1.0, -1.0, -1.0,
    1.0, -1.0, -1.0,
    1.0,  1.0, -1.0,
    -1.0, 1.0, -1.0,
    -1.0, -1.0,  1.0,
    1.0, -1.0,  1.0,
    1.0,  1.0,  1.0,
    -1.0,  1.0,  1.0
];
var jscolors = [
    0.0,  1.0,  0.0,  1.0,
    0.0,  1.0,  0.0,  1.0,
    1.0,  0.5,  0.0,  1.0,
    1.0,  0.5,  0.0,  1.0,
    1.0,  0.0,  0.0,  1.0,
    1.0,  0.0,  0.0,  1.0,
    0.0,  0.0,  1.0,  1.0,
    1.0,  0.0,  1.0,  1.0
];

var jsindices = [
    0, 4, 5, 0, 5, 1,
    1, 5, 6, 1, 6, 2,
    2, 6, 7, 2, 7, 3,
    3, 7, 4, 3, 4, 0,
    4, 7, 6, 4, 6, 5,
    3, 0, 1, 3, 1, 2
];

console.log('Starting Script!');

/*
var x = new FOUR.Object3d(); x.name = "x";
var a = new FOUR.Object3d(); a.name = "a";
x.add(a);
var b = new FOUR.Object3d(); b.name = "b";
x.add(b);
var c = new FOUR.Object3d(); c.name = "c";
x.add(c);

console.log('---- Camera ----');
var z = new FOUR.Camera(); z.name = 'z';
x.add(z);
*/


var renderer = new FOUR.DefaultRenderer();

/*
var geom = new FOUR.BufferGeometry();
geom.name = 'test-geometry';
var verts = new FOUR.BufferAttribute(new Float32Array(jsindices.length), 3);
var colors = new FOUR.BufferAttribute(new Float32Array(jscolors.length), 4);
var indices = new FOUR.BufferAttribute(new Uint16Array(jsindices.length), 1);
for (var i = 0; i < jsverts.length; ++i) {
    verts.data[i] = jsverts[i];
}
verts.needsUpdate = true;
for (var i = 0; i < jscolors.length; ++i) {
    colors.data[i] = jscolors[i];
}
colors.needsUpdate = true;
for (var i = 0; i < jsindices.length; ++i) {
    indices.data[i] = jsindices[i];
}
indices.needsUpdate = true;
geom.setAttribute('position', verts);
geom.setAttribute('color', colors);
geom.setAttribute('index', indices);

var mat = new FOUR.ShaderMaterial({
    vertexShader:
        '' +
        '',
    fragmentShader: '',
    transparent: true,
    alphaTest: 0.5,
    depthTest: true,
    depthWrite: true,
    opacity: 0.8,
    uniforms: {
        test: {type: 'v2', value: new FOUR.Vector2(1, 2) }
    },
    name: 'test-material'
});

var mesh = new FOUR.Mesh(geom, mat);
mesh.position.x = 3;
mesh.name = 'test-mesh';
*/

var jstris = [
    0.0, 0.5, 0.0,
    -0.5, -0.5, 0.0,
    0.5, -0.5, 0.0
];

var geom = new FOUR.BufferGeometry();
geom.name = 'test-geometry';
var verts = new FOUR.BufferAttribute(new Float32Array(jstris.length), 3);
for (var i = 0; i < jstris.length; ++i) {
    verts.data[i] = jstris[i];
}
verts.needsUpdate = true;
geom.setAttribute('position', verts);

var vshader = [
    'attribute vec4 vPosition;',
    'void main()',
    '{',
    '  gl_Position = vPosition;',
    '}'
];
var fshader = [
    'precision mediump float;',
    'void main()',
    '{',
    '  glFragColor = vec4(1.0, 0.0, 0.0, 1.0)',
    '}'
];

FOUR.AttribType = {
    Vector3: 2
};

// Immutable once created
var shader = new FOUR.Shader({
    vertex: vshader.join('\n'),
    fragment: fshader.join('\n'),
    attributes: {
        'position': FOUR.AttribType.Vector3
    }
});

// Shader is not changable
var mat = new FOUR.ShaderMaterial(shader);
mat.transparent = true;
mat.depthWrite = true;
mat.depthTest = true;

var mesh = new FOUR.Mesh(geom, mat);

var WIN_WIDTH = 1920/2;
var WIN_HEIGHT = 1080/2;
var cam = new FOUR.Camera();
cam.name = 'test-camera';
cam.setPerspective(45.0, WIN_WIDTH/WIN_HEIGHT, 0.1, 100.0);

var trs = new FOUR.Object3d();
trs.name = 'test-object';
trs.position.x = 4;
trs.position.y = 2;
trs.add(mesh);

var scene = new FOUR.Scene();
scene.name = 'test-scene';
scene.add(trs);
scene.add(cam);

var colorVal = 0.0;
function renderFrame() {
    colorVal += 0.01;
    if (colorVal > 1) {
        colorVal -= 1;
    }

    renderer.setClearColor(colorVal, colorVal, colorVal, 1);
    renderer.clear(true);
    renderer.render(scene, cam);

    requestAnimationFrame(renderFrame);
}
requestAnimationFrame(renderFrame);
