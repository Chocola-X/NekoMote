import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';

const MAX_PENDING = 256;
const FIXED_UNIT = 256;

// One asynchronous write at a time. The Shell never waits for the renderer.
export class DesktopPointer {
    constructor(stream, onError) {
        this._stream = stream;
        this._onError = onError;
        this._cancel = new Gio.Cancellable();
        this._encoder = new TextEncoder();
        this._pending = [];
        this._writing = false;
        this._inside = false;
        this._pressed = false;
    }

    close() {
        this._cancel.cancel();
        this._pending.length = 0;
    }

    _send(kind, x = 0, y = 0) {
        if (this._cancel.is_cancelled())
            return;
        const time = Math.floor(GLib.get_monotonic_time() / 1000);
        const line = `${kind} ${Math.round(x * FIXED_UNIT)} ${Math.round(y * FIXED_UNIT)} ${time}\n`;
        // Hover needs the latest position, but a drag needs its movement history.
        if (kind === 'm' && !this._pressed && this._pending.at(-1)?.startsWith('m '))
            this._pending[this._pending.length - 1] = line;
        else
            this._pending.push(line);
        if (this._pending.length > MAX_PENDING) {
            this.close();
            this._onError(new Error('desktop input queue overflow'));
        } else {
            this._write();
        }
    }

    _write() {
        if (this._writing || !this._pending.length || this._cancel.is_cancelled())
            return;
        const batch = this._encoder.encode(this._pending.join(''));
        this._pending.length = 0;
        this._writing = true;
        this._stream.write_all_async(batch, GLib.PRIORITY_DEFAULT, this._cancel, (stream, result) => {
            this._writing = false;
            try {
                stream.write_all_finish(result);
            } catch (error) {
                if (!this._cancel.is_cancelled()) {
                    this.close();
                    this._onError(error);
                }
                return;
            }
            this._write();
        });
    }

    leave() {
        if (!this._inside)
            return;
        this._inside = false;
        this._pressed = false;
        this._send('l');
    }

    setCovered(covered) {
        if (covered) {
            this.leave();
            this.clearGaze();
        }
        this._send('h', covered ? 1 : 0);
    }

    gaze(x, y) {
        if (this._inside || (x === this._gazeX && y === this._gazeY))
            return;
        this._gazeX = x;
        this._gazeY = y;
        this._send('g', x, y);
    }

    clearGaze() {
        if (this._gazeX === undefined)
            return;
        this._gazeX = this._gazeY = undefined;
        this._send('c');
    }

    topInset(height) {
        this._send('i', height);
    }

    event(actor, event) {
        const type = event.type();
        if (type === Clutter.EventType.LEAVE) {
            this.leave();
            return Clutter.EVENT_PROPAGATE;
        }
        const button = type === Clutter.EventType.BUTTON_PRESS ||
            type === Clutter.EventType.BUTTON_RELEASE;
        if (button && event.get_button() !== 1)
            return Clutter.EVENT_PROPAGATE;
        if (!button && type !== Clutter.EventType.ENTER && type !== Clutter.EventType.MOTION &&
            type !== Clutter.EventType.SCROLL)
            return Clutter.EVENT_PROPAGATE;
        const [sx, sy] = event.get_coords();
        const [inside, x, y] = actor.transform_stage_point(sx, sy);
        if (!inside)
            return Clutter.EVENT_PROPAGATE;
        if (!this._inside) {
            this.clearGaze();
            this._inside = true;
            this._send('e', x, y);
        }
        switch (type) {
        case Clutter.EventType.MOTION:
            this._send('m', x, y);
            return Clutter.EVENT_PROPAGATE;
        case Clutter.EventType.BUTTON_PRESS:
            this._pressed = true;
            this._send('p', x, y);
            return Clutter.EVENT_STOP;
        case Clutter.EventType.BUTTON_RELEASE:
            if (!this._pressed)
                return Clutter.EVENT_PROPAGATE;
            this._send('r', x, y);
            this._pressed = false;
            return Clutter.EVENT_STOP;
        case Clutter.EventType.SCROLL: {
            let amount = 0;
            switch (event.get_scroll_direction()) {
            case Clutter.ScrollDirection.UP:
                amount = 1;
                break;
            case Clutter.ScrollDirection.DOWN:
                amount = -1;
                break;
            case Clutter.ScrollDirection.SMOOTH:
                amount = -event.get_scroll_delta()[1];
                break;
            }
            if (!amount)
                return Clutter.EVENT_PROPAGATE;
            this._send('m', x, y);
            this._send('s', amount);
            return Clutter.EVENT_STOP;
        }
        default:
            return Clutter.EVENT_PROPAGATE;
        }
    }
}
