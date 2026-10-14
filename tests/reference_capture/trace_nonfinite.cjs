// Exercise the observer through submitted buffer bytes, without any model core.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
class FakeGL {
    constructor() {
        for(const name of ['ARRAY_BUFFER', 'ARRAY_BUFFER_BINDING', 'ELEMENT_ARRAY_BUFFER_BINDING',
            'CURRENT_PROGRAM', 'ACTIVE_ATTRIBUTES', 'ACTIVE_UNIFORMS', 'FLOAT',
            'VERTEX_ATTRIB_ARRAY_BUFFER_BINDING', 'VERTEX_ATTRIB_ARRAY_TYPE',
            'VERTEX_ATTRIB_ARRAY_SIZE', 'VERTEX_ATTRIB_ARRAY_STRIDE', 'VERTEX_ATTRIB_ARRAY_POINTER',
            'SCISSOR_BOX', 'VIEWPORT', 'COLOR_WRITEMASK', 'BLEND_COLOR']) this[name]=name;
        this.buffer={};
        this.draws=0;
    }
    getParameter(name) {
        if(name===this.ARRAY_BUFFER_BINDING) return this.buffer;
        if(['SCISSOR_BOX','VIEWPORT','COLOR_WRITEMASK','BLEND_COLOR'].includes(name)) return [0,0,32,32];
        return null;
    }
    getProgramParameter(program,name) {return name===this.ACTIVE_ATTRIBUTES?1:0;}
    getActiveAttrib() {return {name:'a_pos'};}
    getAttribLocation() {return 0;}
    getVertexAttrib(location,name) {
        return ({VERTEX_ATTRIB_ARRAY_BUFFER_BINDING:this.buffer,
            VERTEX_ATTRIB_ARRAY_TYPE:this.FLOAT, VERTEX_ATTRIB_ARRAY_SIZE:3,
            VERTEX_ATTRIB_ARRAY_STRIDE:0})[name];
    }
    getVertexAttribOffset() {return 0;}
    getAttachedShaders() {throw new Error('shader inspection forbidden');}
    activeTexture() {}
    isEnabled() {return false;}
    texImage2D() {}
    texParameteri() {}
    bufferData() {}
    bufferSubData() {}
    createTexture() {return {};}
    drawElements() {this.draws++;}
    drawArrays() {this.draws++;}
    clear() {}
}
const window={traceEnabled:true,omitShaderSources:true};
vm.runInNewContext(fs.readFileSync(process.argv[2],'utf8'),
                   {window,WebGLRenderingContext:FakeGL});
const gl=new FakeGL();
const submitted=new Float32Array([NaN,Infinity,-Infinity,1,2,3]);
gl.bufferData(gl.ARRAY_BUFFER,submitted,0);
submitted.fill(99); // Changing the caller's buffer must not alter captured uploads.
gl.drawArrays(0,0,2);
const captured=JSON.parse(JSON.stringify(window.drawTrace[0]));
assert.deepEqual(captured.attributes.a_pos.nonfinite,
                 {nan:1,positiveInfinity:1,negativeInfinity:1});
assert.deepEqual(captured.attributes.a_pos.data,[[null,null,null],[1,2,3]]);
assert.deepEqual(captured.shaders,[]);
gl.bufferSubData(gl.ARRAY_BUFFER,0,new Float32Array([4,5,6]));
gl.drawArrays(0,0,2);
const finite=JSON.parse(JSON.stringify(window.drawTrace[1]));
assert.equal(finite.attributes.a_pos.nonfinite,undefined);
assert.deepEqual(finite.attributes.a_pos.data,[[4,5,6],[1,2,3]]);
assert.equal(gl.draws,2);
