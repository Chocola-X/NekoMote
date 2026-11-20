import Clutter from 'gi://Clutter';
import Cogl from 'gi://Cogl';
import GObject from 'gi://GObject';
import Shell from 'gi://Shell';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

function offscreenExtent(size) {
    // Match Clutter's round-to-nearest-even paint box with three pixels of
    // padding. The allocation clip makes this independent of the child.
    const floor = Math.floor(size);
    const rounded = size - floor === 0.5 ? floor + floor % 2 : Math.round(size);
    return rounded + 3;
}

const RoundedClip = GObject.registerClass(
class NekoMoteRoundedClip extends Shell.GLSLEffect {
    vfunc_build_pipeline() {
        const fragment = Cogl.SnippetHook?.FRAGMENT ?? Shell.SnippetHook.FRAGMENT;
        this.add_glsl_snippet(fragment, `
            uniform vec2 texture_extent;
            uniform vec2 texture_origin;
            uniform vec2 viewport_extent;
            uniform vec2 corner_radii;
        `, `
            vec2 point = cogl_tex_coord0_in.st * texture_extent + texture_origin;
            vec2 corner = abs(point - viewport_extent * 0.5)
                          - (viewport_extent * 0.5 - corner_radii);
            if (corner.x > 0.0 && corner.y > 0.0) {
                float distance = length(corner / corner_radii) - 1.0;
                float pixel = max(fwidth(distance), 0.001);
                cogl_color_out *= clamp(0.5 - distance / pixel, 0.0, 1.0);
            }
        `, false);
    }

    setRadius(radius, workarea) {
        this._radius = radius;
        this._workWidth = workarea.width;
        this._workHeight = workarea.height;
        this._updateUniforms();
    }

    setSize(width, height) {
        if (width === this._width && height === this._height)
            return;
        this._width = width;
        this._height = height;
        this._updateUniforms();
    }

    _updateUniforms() {
        const width = this._width;
        const height = this._height;
        this.enabled = this._radius > 0 && this._workWidth > 0 && this._workHeight > 0 &&
            width > 0 && height > 0;
        if (!this.enabled)
            return;
        const texWidth = offscreenExtent(width);
        const texHeight = offscreenExtent(height);
        this.set_uniform_float(this.get_uniform_location('texture_extent'), 2,
            [texWidth, texHeight]);
        // Clutter rounds up the right and bottom paint-box edges after
        // adding 0.75 logical pixels.
        this.set_uniform_float(this.get_uniform_location('texture_origin'), 2,
            [Math.ceil(width + 0.75) - texWidth, Math.ceil(height + 0.75) - texHeight]);
        this.set_uniform_float(this.get_uniform_location('viewport_extent'), 2,
            [width, height]);
        this.set_uniform_float(this.get_uniform_location('corner_radii'), 2, [
            width * Math.min(this._radius / this._workWidth, 0.5),
            height * Math.min(this._radius / this._workHeight, 0.5),
        ]);
        this.queue_repaint();
    }
});

const WorkspacePreview = GObject.registerClass(
class NekoMoteWorkspacePreview extends Clutter.Actor {
    _init(source, monitor, clip) {
        super._init({x_expand: true, y_expand: true, clip_to_allocation: true});
        this._width = monitor.width;
        this._height = monitor.height;
        this._picture = new Clutter.Clone({source});
        this.add_child(this._picture);
        this._clip = clip;
        this.add_effect(clip);
    }

    // Keep the source's natural size out of the workspace layout.
    vfunc_get_preferred_width() {
        return [0, 0];
    }

    vfunc_get_preferred_height() {
        return [0, 0];
    }

    vfunc_allocate(box) {
        this.set_allocation(box);
        const [width, height] = box.get_size();
        this._clip.setSize(width, height);
        const scale = Math.max(width / this._width, height / this._height);
        const x = (width - this._width * scale) / 2;
        const y = (height - this._height * scale) / 2;
        // Allocate the clone at its native size. Scaling it explicitly keeps
        // both axes uniform and avoids Clutter's implicit stretch on GNOME 45.
        this._picture.set_scale(scale, scale);
        this._picture.allocate(new Clutter.ActorBox({
            x1: x, y1: y, x2: x + this._width, y2: y + this._height,
        }));
    }
});

export function addWorkspacePreview(background, source, monitor) {
    // Shell's bin already describes the work-area opening. Its first child
    // extends behind the panel and must keep its position in the layout.
    const bin = background.get_parent()?.get_parent();
    if (!(bin?.get_parent() instanceof Shell.WorkspaceBackground))
        return null;

    const clip = new RoundedClip();
    const preview = new WorkspacePreview(source, monitor, clip);
    bin.add_child(preview);
    background.bind_property('opacity', preview, 'opacity', GObject.BindingFlags.SYNC_CREATE);

    const content = background.content;
    const updateRadius = () => clip.setRadius(content.rounded_clip_radius,
        Main.layoutManager.getWorkAreaForMonitor(monitor.index));
    content.connectObject('notify::rounded-clip-radius', updateRadius, preview);
    global.display.connectObject('workareas-changed', updateRadius, preview);
    updateRadius();
    return preview;
}
