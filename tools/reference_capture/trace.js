// Diagnostic observation only. This code never participates in native playback.
window.drawTrace = [];
window.stateTrace = [];
window.maskPixels = [];
(() => {
    const proto=WebGLRenderingContext.prototype;
    const buffers=new Map();
    const bufferOffsets=new Map();
    const textures=new Map();
    const framebuffers=new Map();
    const original={};
    for (const name of ['texImage2D','texParameteri','bufferData','bufferSubData','createTexture','drawElements','drawArrays','clear']) original[name]=proto[name];
    const textureInfo=new Map();
    proto.texImage2D=function(...args) {
        const texture=this.getParameter(this.TEXTURE_BINDING_2D);
        const info=textureInfo.get(texture)||{};
        if(args.length===9) Object.assign(info,{width:args[3],height:args[4],format:args[6],type:args[7]});
        else Object.assign(info,{width:args[5].width,height:args[5].height});
        textureInfo.set(texture,info);
        return original.texImage2D.apply(this,args);
    };
    proto.texParameteri=function(target,pname,param) {
        const texture=this.getParameter(this.TEXTURE_BINDING_2D);
        const info=textureInfo.get(texture)||{};
        info[pname]=param;textureInfo.set(texture,info);
        return original.texParameteri.call(this,target,pname,param);
    };
    proto.bufferData=function(target,data,usage) {
        const binding=this.getParameter(target===this.ARRAY_BUFFER?this.ARRAY_BUFFER_BINDING:this.ELEMENT_ARRAY_BUFFER_BINDING);
        buffers.set(binding,typeof data==='number'?new Uint8Array(data):new Uint8Array(data.buffer,data.byteOffset,data.byteLength).slice());
        bufferOffsets.set(binding,typeof data==='number'?null:data.byteOffset);
        return original.bufferData.call(this,target,data,usage);
    };
    proto.bufferSubData=function(target,offset,data) {
        const binding=this.getParameter(target===this.ARRAY_BUFFER?this.ARRAY_BUFFER_BINDING:this.ELEMENT_ARRAY_BUFFER_BINDING);
        buffers.get(binding).set(new Uint8Array(data.buffer,data.byteOffset,data.byteLength),offset);
        const base=data.byteOffset-offset;
        const previous=bufferOffsets.get(binding);
        bufferOffsets.set(binding,previous===null || previous===base?base:null);
        return original.bufferSubData.call(this,target,offset,data);
    };
    proto.createTexture=function() {
        const t=original.createTexture.call(this);textures.set(t,textures.size+1);return t;
    };
    function state(gl) {
        const framebuffer=gl.getParameter(gl.FRAMEBUFFER_BINDING);
        if(framebuffer && !framebuffers.has(framebuffer)) framebuffers.set(framebuffer,framebuffers.size+1);
        const active=gl.getParameter(gl.ACTIVE_TEXTURE);
        gl.activeTexture(gl.TEXTURE0);
        const bound0=gl.getParameter(gl.TEXTURE_BINDING_2D);
        const texture0=textures.get(bound0)||0;
        gl.activeTexture(gl.TEXTURE1);
        const bound1=gl.getParameter(gl.TEXTURE_BINDING_2D);
        const texture1=textures.get(bound1)||0;
        gl.activeTexture(active);
        return {
            framebufferId:framebuffer?(framebuffers.get(framebuffer)):0,
            attachment:framebuffer?(textures.get(gl.getFramebufferAttachmentParameter(
                gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME))||0):0,
            textureUnits:[texture0,texture1],
            textureInfo:[textureInfo.get(bound0)||{},textureInfo.get(bound1)||{}],
            scissorBox:Array.from(gl.getParameter(gl.SCISSOR_BOX)),
            viewport:Array.from(gl.getParameter(gl.VIEWPORT)),
            colorBits:[gl.RED_BITS,gl.GREEN_BITS,gl.BLUE_BITS,gl.ALPHA_BITS].map(p=>gl.getParameter(p)),
            colorMask:Array.from(gl.getParameter(gl.COLOR_WRITEMASK)),
            blendEnabled:gl.isEnabled(gl.BLEND),
            ditherEnabled:gl.isEnabled(gl.DITHER),
            stencilEnabled:gl.isEnabled(gl.STENCIL_TEST),
            depthEnabled:gl.isEnabled(gl.DEPTH_TEST),
            scissorEnabled:gl.isEnabled(gl.SCISSOR_TEST)
        };
    }
    proto.clear=function(mask) {
        if(window.traceEnabled) window.stateTrace.push({
            beforeDraw:window.drawTrace.length,mask,
            clearColor:Array.from(this.getParameter(this.COLOR_CLEAR_VALUE)),...state(this)});
        return original.clear.call(this,mask);
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
            attributes[info.name]={type,size,stride,offset,data,
                bufferByteOffset:bufferOffsets.get(buffer)};
        }
        const uniforms={};
        for(let u=0;u<gl.getProgramParameter(program,gl.ACTIVE_UNIFORMS);u++) {
            const info=gl.getActiveUniform(program,u);
            const value=gl.getUniform(program,gl.getUniformLocation(program,info.name));
            uniforms[info.name]=ArrayBuffer.isView(value)?Array.from(value):value;
        }
        const record={mode,count,indices,attributes,uniforms,glState:state(gl),
            texture:textures.get(gl.getParameter(gl.TEXTURE_BINDING_2D)),
            framebuffer:!!gl.getParameter(gl.FRAMEBUFFER_BINDING),
            blend:[gl.getParameter(gl.BLEND_SRC_RGB),gl.getParameter(gl.BLEND_DST_RGB),gl.getParameter(gl.BLEND_SRC_ALPHA),gl.getParameter(gl.BLEND_DST_ALPHA)],
            blendEquation:[gl.getParameter(gl.BLEND_EQUATION_RGB),gl.getParameter(gl.BLEND_EQUATION_ALPHA)],
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
    function maskReadback(gl) {
        if(!window.traceEnabled || !window.traceMaskPixels) return;
        const framebuffer=gl.getParameter(gl.FRAMEBUFFER_BINDING);
        if(!framebuffer) return;
        const texture=gl.getFramebufferAttachmentParameter(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,
                                                          gl.FRAMEBUFFER_ATTACHMENT_OBJECT_NAME);
        if(textures.get(texture)!==3) return;
        const size=gl.getParameter(gl.VIEWPORT);
        const pixels=new Uint8Array(size[2]*size[3]*4);
        gl.readPixels(0,0,size[2],size[3],gl.RGBA,gl.UNSIGNED_BYTE,pixels);
        const nonzero=[],histogram=new Array(256).fill(0);
        for(let i=0;i<pixels.length/4;i++) {
            const value=pixels[i*4+1];histogram[value]++;
            if(value) nonzero.push([i,value]);
        }
        window.maskPixels.push({draw:window.drawTrace.length-1,width:size[2],height:size[3],
                               nonzero,histogram});
    }
    proto.drawElements=function(mode,count,type,offset) {
        if(!observe(this,mode,count,type,offset,0)) return;
        const result=original.drawElements.call(this,mode,count,type,offset);maskReadback(this);return result;
    };
    proto.drawArrays=function(mode,first,count) {
        if(!observe(this,mode,count,0,0,first)) return;
        const result=original.drawArrays.call(this,mode,first,count);maskReadback(this);return result;
    };
})();
