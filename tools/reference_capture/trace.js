// Diagnostic observation only. This code never participates in native playback.
window.drawTrace = [];
(() => {
    const proto=WebGLRenderingContext.prototype;
    const buffers=new Map();
    const textures=new Map();
    const original={};
    for (const name of ['bufferData','bufferSubData','createTexture','drawElements','drawArrays']) original[name]=proto[name];
    proto.bufferData=function(target,data,usage) {
        const binding=this.getParameter(target===this.ARRAY_BUFFER?this.ARRAY_BUFFER_BINDING:this.ELEMENT_ARRAY_BUFFER_BINDING);
        buffers.set(binding,typeof data==='number'?new Uint8Array(data):new Uint8Array(data.buffer,data.byteOffset,data.byteLength).slice());
        return original.bufferData.call(this,target,data,usage);
    };
    proto.bufferSubData=function(target,offset,data) {
        const binding=this.getParameter(target===this.ARRAY_BUFFER?this.ARRAY_BUFFER_BINDING:this.ELEMENT_ARRAY_BUFFER_BINDING);
        buffers.get(binding).set(new Uint8Array(data.buffer,data.byteOffset,data.byteLength),offset);
        return original.bufferSubData.call(this,target,offset,data);
    };
    proto.createTexture=function() {
        const t=original.createTexture.call(this);textures.set(t,textures.size+1);return t;
    };
    function observe(gl,mode,count,indexType,indexOffset,first) {
        if (!window.traceEnabled && !window.isolateRect) return true;
        const program=gl.getParameter(gl.CURRENT_PROGRAM);
        const indices=[];
        if (indexType) {
            const bytes=buffers.get(gl.getParameter(gl.ELEMENT_ARRAY_BUFFER_BINDING));
            const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
            for(let i=0;i<count;i++) indices.push(indexType===gl.UNSIGNED_SHORT?view.getUint16(indexOffset+i*2,true):view.getUint8(indexOffset+i));
        } else for(let i=0;i<count;i++) indices.push(first+i);
        const max=Math.max(...indices);
        const attributes={};
        for(let a=0;a<gl.getProgramParameter(program,gl.ACTIVE_ATTRIBUTES);a++) {
            const info=gl.getActiveAttrib(program,a);
            const loc=gl.getAttribLocation(program,info.name);
            const buffer=gl.getVertexAttrib(loc,gl.VERTEX_ATTRIB_ARRAY_BUFFER_BINDING);
            const bytes=buffers.get(buffer);
            if (!bytes) continue;
            const type=gl.getVertexAttrib(loc,gl.VERTEX_ATTRIB_ARRAY_TYPE);
            const size=gl.getVertexAttrib(loc,gl.VERTEX_ATTRIB_ARRAY_SIZE);
            const offset=gl.getVertexAttribOffset(loc,gl.VERTEX_ATTRIB_ARRAY_POINTER);
            const stride=gl.getVertexAttrib(loc,gl.VERTEX_ATTRIB_ARRAY_STRIDE)||size*(type===gl.FLOAT?4:type===gl.UNSIGNED_BYTE?1:2);
            const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
            const data=[];
            for(let i=0;i<=max;i++) {
                const values=[];
                for(let c=0;c<size;c++) values.push(type===gl.FLOAT?view.getFloat32(offset+i*stride+c*4,true):
                    type===gl.UNSIGNED_BYTE?view.getUint8(offset+i*stride+c):view.getInt16(offset+i*stride+c*2,true));
                data.push(values);
            }
            attributes[info.name]={type,size,stride,offset,data};
        }
        const uniforms={};
        for(let u=0;u<gl.getProgramParameter(program,gl.ACTIVE_UNIFORMS);u++) {
            const info=gl.getActiveUniform(program,u);
            const value=gl.getUniform(program,gl.getUniformLocation(program,info.name));
            uniforms[info.name]=ArrayBuffer.isView(value)?Array.from(value):value;
        }
        const record={mode,count,indices,attributes,uniforms,
            texture:textures.get(gl.getParameter(gl.TEXTURE_BINDING_2D)),
            framebuffer:!!gl.getParameter(gl.FRAMEBUFFER_BINDING),
            blend:[gl.getParameter(gl.BLEND_SRC_RGB),gl.getParameter(gl.BLEND_DST_RGB),gl.getParameter(gl.BLEND_SRC_ALPHA),gl.getParameter(gl.BLEND_DST_ALPHA)],
            shaders:gl.getAttachedShaders(program).map(s=>gl.getShaderSource(s))};
        if(window.traceEnabled) window.drawTrace.push(record);
        if(window.isolateRect && record.texture===2) {
            const r=window.isolateRect;
            const uv=indices.map(i=>attributes.a_texCoord.data[i]);
            const bounds=[Math.min(...uv.map(v=>v[0])),r.atlas_height-Math.max(...uv.map(v=>v[1])),
                Math.max(...uv.map(v=>v[0]))-Math.min(...uv.map(v=>v[0])),Math.max(...uv.map(v=>v[1]))-Math.min(...uv.map(v=>v[1]))];
            return (record.blend[0]===gl.SRC_ALPHA || record.blend[0]===gl.ONE_MINUS_DST_ALPHA) && bounds.every((v,i)=>Math.abs(v-[r.left,r.top,r.width,r.height][i])<0.01);
        }
        return true;
    }
    proto.drawElements=function(mode,count,type,offset) {if(!observe(this,mode,count,type,offset,0)) return;return original.drawElements.call(this,mode,count,type,offset);};
    proto.drawArrays=function(mode,first,count) {if(!observe(this,mode,count,0,0,first)) return;return original.drawArrays.call(this,mode,first,count);};
})();
